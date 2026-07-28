#include "msm/msmsys.h"
#include "msm/msmfio.h"
#include "msm/msmmem.h"
#include "msm/msmmus.h"
#include "msm/msmse.h"
#include "msm/msmstream.h"
#include "musyx/seq.h"
#include "musyx/stream.h"
#include "musyx/synthdata.h"
#include "musyx/hardware.h"

#include <stdint.h>
#include <string.h>

static MSM_SYS sys;

typedef struct MSMRawSDir_s {
    u16 id;
    u16 refCnt;
    u32 offset;
    u32 addr;
    SAMPLE_HEADER header;
    u32 extraData;
} MSM_RAW_SDIR;

_Static_assert(sizeof(MSM_RAW_SDIR) == 0x20, "unexpected on-disc SDIR layout");

static BOOL msmSysRangeValid(u32 offset, u32 size, u32 limit)
{
    return offset <= limit && size <= limit - offset;
}

static void msmSysSwapRefList(u8 *project, u32 projectSize, u32 offset)
{
    u16 *entry;
    u16 value;
    u32 pos;

    /* An offset of zero is the on-disc representation of an absent list. */
    if (offset == 0 || !msmSysRangeValid(offset, sizeof(u16), projectSize)) {
        return;
    }
    pos = offset;
    while (msmSysRangeValid(pos, sizeof(u16), projectSize)) {
        entry = (u16 *) (project + pos);
        value = __builtin_bswap16(*entry);
        *entry = value;
        if (value == 0xFFFF) {
            return;
        }
        pos += sizeof(u16);
    }
}

static void msmSysSwapPageTable(u8 *project, u32 projectSize, u32 offset)
{
    PAGE *page;

    if (offset == 0) {
        return;
    }
    while (msmSysRangeValid(offset, sizeof(PAGE), projectSize)) {
        page = (PAGE *) (project + offset);
        page->macro = __builtin_bswap16(page->macro);
        if (page->index == 0xFF) {
            return;
        }
        offset += sizeof(PAGE);
    }
}

static void msmSysSwapMidiSetup(u8 *project, u32 projectSize, u32 offset)
{
    MIDISETUP *setup;

    if (offset == 0) {
        return;
    }
    while (msmSysRangeValid(offset, sizeof(MIDISETUP), projectSize)) {
        setup = (MIDISETUP *) (project + offset);
        setup->songId = __builtin_bswap16(setup->songId);
        setup->reserved = __builtin_bswap16(setup->reserved);
        if (setup->songId == 0xFFFF) {
            return;
        }
        offset += sizeof(MIDISETUP);
    }
}

static void msmSysSwapFxTable(u8 *project, u32 projectSize, u32 offset)
{
    FX_DATA *data;
    u16 count;
    u32 i;

    if (offset == 0 || !msmSysRangeValid(offset, sizeof(u16) * 2, projectSize)) {
        return;
    }
    data = (FX_DATA *) (project + offset);
    count = __builtin_bswap16(data->num);
    data->num = count;
    data->reserverd = __builtin_bswap16(data->reserverd);
    if (count > (projectSize - offset - 4) / sizeof(FX_TAB)) {
        count = (projectSize - offset - 4) / sizeof(FX_TAB);
    }
    for (i = 0; i < count; i++) {
        data->fx[i].id = __builtin_bswap16(data->fx[i].id);
        data->fx[i].macro = __builtin_bswap16(data->fx[i].macro);
    }
}

static BOOL msmSysSwapProject(u8 *project, u32 projectSize)
{
    GROUP_DATA *group;
    u32 next;
    u32 offset;
    u32 guard;

    offset = 0;
    guard = 0;
    while (msmSysRangeValid(offset, sizeof(u32), projectSize) && guard++ < 256) {
        group = (GROUP_DATA *) (project + offset);
        next = __builtin_bswap32(group->nextOff);
        group->nextOff = next;
        if (next == 0xFFFFFFFF) {
            return TRUE;
        }
        /*
         * A regular project entry is a complete GROUP_DATA, while the final
         * list sentinel is only a four-byte nextOff word.
         */
        if (!msmSysRangeValid(offset, sizeof(GROUP_DATA), projectSize)) {
            return FALSE;
        }

        group->id = __builtin_bswap16(group->id);
        group->type = __builtin_bswap16(group->type);
        group->macroOff = __builtin_bswap32(group->macroOff);
        group->sampleOff = __builtin_bswap32(group->sampleOff);
        group->curveOff = __builtin_bswap32(group->curveOff);
        group->keymapOff = __builtin_bswap32(group->keymapOff);
        group->layerOff = __builtin_bswap32(group->layerOff);
        group->data.song.normpageOff = __builtin_bswap32(group->data.song.normpageOff);
        group->data.song.drumpageOff = __builtin_bswap32(group->data.song.drumpageOff);
        group->data.song.midiSetupOff = __builtin_bswap32(group->data.song.midiSetupOff);

        msmSysSwapRefList(project, projectSize, group->macroOff);
        msmSysSwapRefList(project, projectSize, group->sampleOff);
        msmSysSwapRefList(project, projectSize, group->curveOff);
        msmSysSwapRefList(project, projectSize, group->keymapOff);
        msmSysSwapRefList(project, projectSize, group->layerOff);
        if (group->type == 0) {
            msmSysSwapPageTable(project, projectSize, group->data.song.normpageOff);
            msmSysSwapPageTable(project, projectSize, group->data.song.drumpageOff);
            msmSysSwapMidiSetup(project, projectSize, group->data.song.midiSetupOff);
        } else if (group->type == 1) {
            msmSysSwapFxTable(project, projectSize, group->data.fx.tableOff);
        }

        if (next == 0 || !msmSysRangeValid(next, sizeof(u32), projectSize)) {
            return FALSE;
        }
        offset = next;
    }
    return FALSE;
}

enum {
    MSM_POOL_MACRO,
    MSM_POOL_CURVE,
    MSM_POOL_KEYMAP,
    MSM_POOL_LAYER,
};

static BOOL msmSysSwapPoolChain(u8 *pool, u32 poolSize, u32 offset, s32 kind)
{
    MEM_DATA *node;
    u32 dataSize;
    u32 i;
    u32 next;

    /*
     * Zero means that this pool type is absent.  Treating it as a node would
     * reinterpret and mutate the POOL_DATA header itself.
     */
    if (offset == 0) {
        return TRUE;
    }
    while (msmSysRangeValid(offset, sizeof(u32), poolSize)) {
        node = (MEM_DATA *) (pool + offset);
        next = __builtin_bswap32(node->nextOff);
        node->nextOff = next;
        if (next == 0xFFFFFFFF) {
            return TRUE;
        }
        /* Like project lists, a pool chain may end with only a u32 sentinel. */
        if (!msmSysRangeValid(offset, 8, poolSize)) {
            return FALSE;
        }
        if (next < 8 || !msmSysRangeValid(offset, next, poolSize)) {
            return FALSE;
        }
        node->id = __builtin_bswap16(node->id);
        node->reserved = __builtin_bswap16(node->reserved);
        dataSize = next - 8;
        if (kind == MSM_POOL_MACRO) {
            u32 *word = (u32 *) &node->data;
            for (i = 0; i < dataSize / sizeof(u32); i++) {
                word[i] = __builtin_bswap32(word[i]);
            }
        } else if (kind == MSM_POOL_KEYMAP) {
            KEYMAP *map = (KEYMAP *) &node->data;
            for (i = 0; i < dataSize / sizeof(KEYMAP); i++) {
                map[i].id = __builtin_bswap16(map[i].id);
                map[i].prioOffset = __builtin_bswap16(map[i].prioOffset);
            }
        } else if (kind == MSM_POOL_LAYER) {
            u32 count = __builtin_bswap32(node->data.layer.num);
            LAYER *layer = node->data.layer.entry;
            u32 maxCount = dataSize >= sizeof(u32) ? (dataSize - sizeof(u32)) / sizeof(LAYER) : 0;
            node->data.layer.num = count;
            if (count > maxCount) {
                count = maxCount;
            }
            for (i = 0; i < count; i++) {
                layer[i].id = __builtin_bswap16(layer[i].id);
                layer[i].prioOffset = __builtin_bswap16(layer[i].prioOffset);
            }
        }
        offset += next;
    }
    return FALSE;
}

static BOOL msmSysSwapPool(u8 *pool, u32 poolSize)
{
    POOL_DATA *data;

    if (poolSize < sizeof(POOL_DATA)) {
        return FALSE;
    }
    data = (POOL_DATA *) pool;
    data->macroOff = __builtin_bswap32(data->macroOff);
    data->curveOff = __builtin_bswap32(data->curveOff);
    data->keymapOff = __builtin_bswap32(data->keymapOff);
    data->layerOff = __builtin_bswap32(data->layerOff);
    return msmSysSwapPoolChain(pool, poolSize, data->macroOff, MSM_POOL_MACRO)
        && msmSysSwapPoolChain(pool, poolSize, data->curveOff, MSM_POOL_CURVE)
        && msmSysSwapPoolChain(pool, poolSize, data->keymapOff, MSM_POOL_KEYMAP)
        && msmSysSwapPoolChain(pool, poolSize, data->layerOff, MSM_POOL_LAYER);
}

static void msmSysSwapAdpcmInfo(SNDADPCMinfo *info)
{
    u32 i;

    info->numCoef = __builtin_bswap16(info->numCoef);
    info->loopY0 = __builtin_bswap16(info->loopY0);
    info->loopY1 = __builtin_bswap16(info->loopY1);
    for (i = 0; i < 8; i++) {
        info->coefTab[i][0] = __builtin_bswap16(info->coefTab[i][0]);
        info->coefTab[i][1] = __builtin_bswap16(info->coefTab[i][1]);
    }
}

static SDIR_DATA *msmSysConvertSDir(const u8 *rawData, u32 rawSize)
{
    const MSM_RAW_SDIR *raw;
    SDIR_DATA *host;
    u32 count;
    u32 extraOffset;
    u32 hostEntrySize;
    u32 i;
    u32 rawEntrySize;
    u32 totalSize;

    count = 0;
    while (msmSysRangeValid(count * sizeof(MSM_RAW_SDIR), sizeof(MSM_RAW_SDIR), rawSize)) {
        raw = (const MSM_RAW_SDIR *) (rawData + count * sizeof(MSM_RAW_SDIR));
        count++;
        if (__builtin_bswap16(raw->id) == 0xFFFF) {
            break;
        }
    }
    if (count == 0 || count * sizeof(MSM_RAW_SDIR) > rawSize
        || __builtin_bswap16(((const MSM_RAW_SDIR *) rawData)[count - 1].id) != 0xFFFF) {
        return NULL;
    }

    rawEntrySize = count * sizeof(MSM_RAW_SDIR);
    hostEntrySize = count * sizeof(SDIR_DATA);
    totalSize = hostEntrySize + rawSize - rawEntrySize;
    host = msmMemAlloc(totalSize);
    if (host == NULL) {
        return NULL;
    }
    memset(host, 0, hostEntrySize);
    memcpy((u8 *) host + hostEntrySize, rawData + rawEntrySize, rawSize - rawEntrySize);

    for (i = 0; i < count; i++) {
        raw = (const MSM_RAW_SDIR *) (rawData + i * sizeof(MSM_RAW_SDIR));
        host[i].id = __builtin_bswap16(raw->id);
        host[i].ref_cnt = __builtin_bswap16(raw->refCnt);
        host[i].offset = __builtin_bswap32(raw->offset);
        host[i].addr = (void *) (uintptr_t) __builtin_bswap32(raw->addr);
        host[i].header.info = __builtin_bswap32(raw->header.info);
        host[i].header.length = __builtin_bswap32(raw->header.length);
        host[i].header.loopOffset = __builtin_bswap32(raw->header.loopOffset);
        host[i].header.loopLength = __builtin_bswap32(raw->header.loopLength);
        extraOffset = __builtin_bswap32(raw->extraData);
        if (extraOffset >= rawEntrySize && extraOffset + sizeof(SNDADPCMinfo) <= rawSize) {
            host[i].extraData = hostEntrySize + extraOffset - rawEntrySize;
            msmSysSwapAdpcmInfo((SNDADPCMinfo *) ((u8 *) host + host[i].extraData));
        } else {
            host[i].extraData = 0;
        }
    }
    return host;
}

static s32 msmSysPrepareGroup(MSM_GRP_HEAD *group, u32 dataSize, s32 groupId, void **sdirHost)
{
    u8 *base;
    u32 poolOfs;
    u32 projOfs;
    u32 sdirOfs;
    u32 sngOfs;

    base = (u8 *) group;
    poolOfs = __builtin_bswap32(group->poolOfs);
    projOfs = __builtin_bswap32(group->projOfs);
    sdirOfs = __builtin_bswap32(group->sdirOfs);
    sngOfs = __builtin_bswap32(group->sngOfs);
    if (!(sizeof(MSM_GRP_HEAD) <= poolOfs && poolOfs < projOfs && projOfs < sdirOfs
          && sdirOfs < sngOfs && sngOfs <= dataSize)) {
        return MSM_ERR_INVALIDFILE;
    }

    group->poolOfs = poolOfs;
    group->projOfs = projOfs;
    group->sdirOfs = sdirOfs;
    group->sngOfs = sngOfs;
    if (!msmSysSwapPool(base + poolOfs, projOfs - poolOfs)
        || !msmSysSwapProject(base + projOfs, sdirOfs - projOfs)) {
        return MSM_ERR_INVALIDFILE;
    }
    *sdirHost = msmSysConvertSDir(base + sdirOfs, sngOfs - sdirOfs);
    if (*sdirHost == NULL) {
        return MSM_ERR_OUTOFMEM;
    }
    msmMusPrepareGroup(group, dataSize, groupId);
    return 0;
}

static void msmSysFreeStackSDir(MSM_GRP_STACK *group)
{
    if (group->sdirHost != NULL) {
        msmMemFree(group->sdirHost);
        group->sdirHost = NULL;
    }
}

static void msmSysServer(void)
{
    if (sndIsInstalled() == 1) {
        if (--sys.timer == 0) {
            sys.timer = 3;
            msmMusPeriodicProc();
            msmSePeriodicProc();
            msmStreamPeriodicProc();
        }
    }
    if (sys.oldAIDCallback != NULL) {
        sys.oldAIDCallback();
    }
}

static s32 msmSysSetAuxParam(s32 auxA, s32 auxB)
{
    s32 unused_1[2];
    SND_AUX_CALLBACK auxcb[2];
    s32 unused_2[2];
    MSM_AUXPARAM *auxParam;
    MSM_AUX *aux;
    u32 result;
    s32 i;

    if (sys.auxParamNo[0] != MSM_AUXNO_NULL && auxA >= 0) {
        sys.auxParamNo[0] = auxA;
    }
    if (sys.auxParamNo[1] != MSM_AUXNO_NULL && auxB >= 0) {
        sys.auxParamNo[1] = auxB;
    }
    if (sys.auxParamNo[0] < 0 && sys.auxParamNo[1] < 0) {
        return 0;
    }
    for (i = 0; i < 2; i++) {
        if (sys.auxParamNo[i] < 0) {
            auxcb[i] = NULL;
            continue;
        }
        auxParam = &sys.auxParam[sys.auxParamNo[i]];
        aux = &sys.aux[i];
        switch (auxParam->type) {
            case MSM_AUX_REVERBHI:
                auxcb[i] = sndAuxCallbackReverbHI;
                aux->revHi.tempDisableFX = auxParam->revHi.tempDisableFX;
                aux->revHi.coloration = auxParam->revHi.coloration;
                aux->revHi.mix = auxParam->revHi.mix;
                aux->revHi.time = auxParam->revHi.time;
                aux->revHi.damping = auxParam->revHi.damping;
                aux->revHi.preDelay = auxParam->revHi.preDelay;
                aux->revHi.crosstalk = auxParam->revHi.crosstalk;
                result = sndAuxCallbackPrepareReverbHI(&aux->revHi);
                break;
                
            case MSM_AUX_REVERBSTD:
                auxcb[i] = sndAuxCallbackReverbSTD;
                aux->revStd.tempDisableFX = auxParam->revStd.tempDisableFX;
                aux->revStd.coloration = auxParam->revStd.coloration;
                aux->revStd.mix = auxParam->revStd.mix;
                aux->revStd.time = auxParam->revStd.time;
                aux->revStd.damping = auxParam->revStd.damping;
                aux->revStd.preDelay = auxParam->revStd.preDelay;
                result = sndAuxCallbackPrepareReverbSTD(&aux->revStd);
                break;
                
            case MSM_AUX_CHORUS:
                auxcb[i] = sndAuxCallbackChorus;
                aux->chorus.baseDelay = auxParam->chorus.baseDelay;
                aux->chorus.variation = auxParam->chorus.variation;
                aux->chorus.period = auxParam->chorus.period;
                result = sndAuxCallbackPrepareChorus(&aux->chorus);
                break;
                
            case MSM_AUX_DELAY:
                auxcb[i] = sndAuxCallbackDelay;
                aux->delay.delay[0] = auxParam->delay.delay[0];
                aux->delay.feedback[0] = auxParam->delay.feedback[0];
                aux->delay.output[0] = auxParam->delay.output[0];
                aux->delay.delay[1] = auxParam->delay.delay[1];
                aux->delay.feedback[1] = auxParam->delay.feedback[1];
                aux->delay.output[1] = auxParam->delay.output[1];
                aux->delay.delay[2] = auxParam->delay.delay[2];
                aux->delay.feedback[2] = auxParam->delay.feedback[2];
                aux->delay.output[2] = auxParam->delay.output[2];
                result = sndAuxCallbackPrepareDelay(&aux->delay);
                break;
        }
        if (result == FALSE) {
            return TRUE;
        }
    }
    sndSetAuxProcessingCallbacks(0, auxcb[0], &sys.aux[0], 0xFF, 0, auxcb[1], &sys.aux[1], 0xFF, 0);
    return FALSE;
}

static s32 msmSysLoadBaseGroup(void *buf)
{
    DVDFileInfo file;
    s32 i;
    s32 result;
    MSM_GRP_HEAD *grpData;
    MSM_GRP_INFO *grpInfo;

    if (msmFioOpen(sys.msmEntryNum, &file) != TRUE) {
        return MSM_ERR_OPENFAIL;
    }
    for(i = 0; i < sys.baseGrpNum; i++) {
        grpData = sys.grpData[i];
        grpInfo = &sys.grpInfo[sys.info->baseGrp[i]];
        if (msmFioRead(&file, grpData, grpInfo->dataSize, grpInfo->dataOfs + sys.header->grpDataOfs) < 0) {
            msmFioClose(&file);
            return MSM_ERR_READFAIL;
        }
        if (msmFioRead(&file, buf, grpInfo->sampSize, grpInfo->sampOfs + sys.header->sampOfs) < 0) {
            msmFioClose(&file);
            return MSM_ERR_READFAIL;
        }
        result = msmSysPrepareGroup(grpData, grpInfo->dataSize, sys.info->baseGrp[i],
            &sys.grpSdir[i]);
        if (result != 0) {
            msmFioClose(&file);
            return result;
        }
        if (!sndPushGroup((void*) ((uintptr_t)grpData->projOfs + (uintptr_t) grpData), grpInfo->gid, buf,
            sys.grpSdir[i], (void*) ((uintptr_t)grpData->poolOfs + (uintptr_t) grpData)))
        {
            msmFioClose(&file);
            return MSM_ERR_GRP_FAILPUSH;
        }
        sys.aramP += grpInfo->sampSize;
    }
    msmFioClose(&file);
    return 0;
}

s32 msmSysSearchGroupStack(s32 grpId, s32 no)
{
    MSM_GRP_STACK *stack;
    u32 stackNo;
    s32 i;
    s32 stackNoB;
    s32 stackNoA;
    s32 maxNo;
    s32 stackMax;

    stackNoA = -1;
    maxNo = 0;
    if (sys.grpInfo[grpId].stackNo == 0) {
        stack = sys.grpStackA;
        stackMax = sys.grpStackAMax;
    } else {
        stack = sys.grpStackB;
        stackMax = sys.grpStackBMax;
    }
    for (i = 0; i < stackMax; stack++, i++) {
        if (i == no) {
            continue;
        }
        if ((stackNo = stack->num) != 0) {
            if (stack->baseGrpF == 0 && stackNo > maxNo) {
                maxNo = stackNo;
                stackNoB = -(i + 1);
            }
        } else {
            stackNoA = i;
        }
    }
    return (stackNoA < 0) ? stackNoB : stackNoA;
}

s32 msmSysGroupInit(DVDFileInfo *file)
{
    s32 i;
    MSM_GRP_STACK *stack;
    MSM_GRP_INFO *grpInfo;

    sys.grpMax = sys.info->grpMax;
    sys.grpNum = 1;
    sys.baseGrpNum = sys.info->baseGrpNum;
    sys.grpStackAMax = sys.info->stackDepthA;
    sys.grpStackADepth = 0;
    sys.grpStackAOfs = 0;
    sys.grpStackBMax = sys.info->stackDepthB;
    sys.grpStackBDepth = 0;
    sys.grpStackBOfs = 0;
    if ((sys.grpInfo = msmMemAlloc(sys.header->grpInfoSize)) == NULL) {
        return MSM_ERR_OUTOFMEM;
    }
    if (msmFioRead(file, sys.grpInfo, sys.header->grpInfoSize, sys.header->grpInfoOfs) < 0) {
        return MSM_ERR_READFAIL;
    }
    /* byte-swap MSM_GRP_INFO array (big-endian -> host LE). */
    {
        s32 gi;
        for (gi = 0; gi < sys.grpMax; gi++) {
            MSM_GRP_INFO* g = &sys.grpInfo[gi];
            g->gid = __builtin_bswap16(g->gid);
            g->dataOfs = __builtin_bswap32(g->dataOfs);
            g->dataSize = __builtin_bswap32(g->dataSize);
            g->sampOfs = __builtin_bswap32(g->sampOfs);
            g->sampSize = __builtin_bswap32(g->sampSize);
        }
    }
    if ((sys.grpBufA = msmMemAlloc(sys.info->grpBufSizeA * sys.grpStackAMax)) == NULL) {
        return MSM_ERR_OUTOFMEM;
    }
    if ((sys.grpBufB = msmMemAlloc(sys.info->grpBufSizeB * sys.grpStackBMax)) == NULL) {
        return MSM_ERR_OUTOFMEM;
    }
    if (sys.header->grpSetSize) {
        if ((sys.grpSet = msmMemAlloc(sys.header->grpSetSize)) == NULL) {
            return MSM_ERR_OUTOFMEM;
        }
        if (msmFioRead(file, sys.grpSet, sys.header->grpSetSize, sys.header->grpSetOfs) < 0) {
            return MSM_ERR_READFAIL;
        }
    } else {
        sys.grpSet = NULL;
    }
    for (i = 0; i < sys.grpStackAMax; i++) {
        stack = &sys.grpStackA[i];
        stack->grpId = stack->baseGrpF = 0;
        stack->num = 0;
        stack->buf = (void*) ((uintptr_t) sys.grpBufA + sys.info->grpBufSizeA * i);
        stack->sdirHost = NULL;
    }
    for (i = 0; i < sys.grpStackBMax; i++) {
        stack = &sys.grpStackB[i];
        stack->grpId = stack->baseGrpF = 0;
        stack->num = 0;
        stack->buf = (void*) ((uintptr_t) sys.grpBufB + sys.info->grpBufSizeB * i);
        stack->sdirHost = NULL;
    }
    sys.sampSize = 0;
    for (i = 0; i < sys.baseGrpNum; i++) {
        grpInfo = &sys.grpInfo[sys.info->baseGrp[i]];
        if ((sys.grpData[i] = msmMemAlloc(grpInfo->dataSize)) == NULL) {
            return MSM_ERR_OUTOFMEM;
        }
        if (sys.sampSize < grpInfo->sampSize) {
            sys.sampSize = grpInfo->sampSize;
        }
        grpInfo->sampSize *= -1;
    }
    sys.sampSizeBase = 0;
    for (i = 1; i < sys.grpMax; i++) {
        grpInfo = &sys.grpInfo[i];
        if (grpInfo->sampSize < 0) {
            grpInfo->sampSize *= -1;
        } else if (sys.sampSizeBase < grpInfo->sampSize) {
            sys.sampSizeBase = grpInfo->sampSize;
        }
    }
    return 0;
}

void msmSysIrqDisable(void)
{
    hwIRQEnterCritical();
    sys.irqDepth++;
}

void msmSysIrqEnable(void)
{
    if (sys.irqDepth != 0) {
        sys.irqDepth--;
        hwIRQLeaveCritical();
    }
}

static BOOL msmSysCheckBaseGroupNo(s32 grpId)
{
    s32 i;

    for (i = 0; i < sys.baseGrpNum + sys.grpStackAOfs + sys.grpStackBOfs; i++) {
        if (sys.info->baseGrp[i] == grpId) {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL msmSysCheckBaseGroup(s32 grpId)
{
    s32 i;

    for (i = 0; i < sys.baseGrpNum + sys.grpStackAOfs + sys.grpStackBOfs; i++) {
        if (sys.grpInfo[sys.info->baseGrp[i]].gid == grpId) {
            return TRUE;
        }
    }
    return FALSE;
}

void *msmSysGetGroupDataPtr(s32 grpId)
{
    MSM_GRP_STACK *grp;
    s32 i;

    for (i = 0; i < sys.baseGrpNum; i++) {
        if (sys.info->baseGrp[i] == grpId) {
            return sys.grpData[i];
        }
    }
    for (i = 0; i < sys.grpStackAMax; i++) {
        grp = &sys.grpStackA[i];
        if (grp->num != 0 && grp->grpId == grpId) {
            return grp->buf;
        }
    }
    for (i = 0; i < sys.grpStackBMax; i++) {
        grp = &sys.grpStackB[i];
        if (grp->num != 0 && grp->grpId == grpId) {
            return grp->buf;
        }
    }
    return NULL;
}

BOOL msmSysCheckLoadGroupID(s32 grpId)
{
    MSM_GRP_STACK *grp;
    s32 i;

    for (i = 0; i < sys.baseGrpNum + sys.grpStackAOfs + sys.grpStackBOfs; i++) {
        if (sys.grpInfo[sys.info->baseGrp[i]].gid == grpId) {
            return TRUE;
        }
    }
    for (i = 0; i < sys.grpStackAMax; i++) {
        grp = &sys.grpStackA[i];
        if (grp->num != 0 && sys.grpInfo[grp->grpId].gid == grpId) {
            return TRUE;
        }
    }
    for (i = 0; i < sys.grpStackBMax; i++) {
        grp = &sys.grpStackB[i];
        if (grp->num != 0 && sys.grpInfo[grp->grpId].gid == grpId) {
            return TRUE;
        }
    }
    return FALSE;
}

void msmSysRegularProc(void)
{
}

s32 msmSysGetOutputMode(void)
{
    return sys.outputMode;
}

BOOL msmSysSetOutputMode(SND_OUTPUTMODE mode)
{
    SND_OUTPUTMODE var_r3;
    BOOL failF;

    failF = 0;
    sys.outputMode = mode;
    switch (mode) {
        case SND_OUTPUTMODE_MONO:
            var_r3 = SND_OUTPUTMODE_MONO;
            break;
        case SND_OUTPUTMODE_SURROUND:
            if (sys.info->surroundF != 0) {
                var_r3 = SND_OUTPUTMODE_SURROUND;
            } else {
                sys.outputMode = SND_OUTPUTMODE_STEREO;
                var_r3 = SND_OUTPUTMODE_STEREO;
                failF = 1;
            }
            break;
        case SND_OUTPUTMODE_STEREO:
        default:
            var_r3 = SND_OUTPUTMODE_STEREO;
            break;
    }
    sndOutputMode(var_r3);
    msmStreamSetOutputMode(sys.outputMode);
    OSSetSoundMode((mode != SND_OUTPUTMODE_MONO) ? 1 : 0);
    return failF;
}

s32 msmSysSetAux(s32 auxA, s32 auxB)
{
    s32 i;

    sndSetAuxProcessingCallbacks(0, NULL, NULL, 0, 0, NULL, NULL, 0, 0);
    for (i = 1; i >= 0; i--) {
        if (sys.auxParamNo[i] < 0) {
            continue;
        }
        switch (sys.auxParam[sys.auxParamNo[i]].type) {
            case 0:
                sndAuxCallbackShutdownReverbHI(&sys.aux[i].revHi);
                break;
            case 1:
                sndAuxCallbackShutdownReverbSTD(&sys.aux[i].revStd);
                break;
            case 2:
                sndAuxCallbackShutdownChorus(&sys.aux[i].chorus);
                break;
            case 3:
                sndAuxCallbackShutdownDelay(&sys.aux[i].delay);
                break;
            }
    }
    if (msmSysSetAuxParam(auxA, auxB) != 0) {
        return MSM_ERR_INVALID_AUXPARAM;
    }
    return 0;
}

s32 msmSysGetSampSize(BOOL baseGrp)
{
    if (baseGrp != 0) {
        return sys.sampSizeBase;
    }
    return sys.sampSize;
}

s32 msmSysDelGroupAll(void)
{
    MSM_GRP_STACK *grp;
    s32 i;

    for (i = 0; i < sys.grpStackBMax; i++) {
        grp = &sys.grpStackB[i];
        if (grp->num != 0 && grp->baseGrpF == 0) {
            grp->num = 0;
            sndPopGroup();
            msmSysFreeStackSDir(grp);
            sys.aramP -= sys.grpInfo[grp->grpId].sampSize;
            sys.grpStackBDepth--;
        }
    }
    for (i = 0; i < sys.grpStackAMax; i++) {
        grp = &sys.grpStackA[i];
        if (grp->num != 0 && grp->baseGrpF == 0) {
            grp->num = 0;
            sndPopGroup();
            msmSysFreeStackSDir(grp);
            sys.aramP -= sys.grpInfo[grp->grpId].sampSize;
            sys.grpStackADepth--;
        }
    }
    return 0;
}

s32 msmSysDelGroupBase(s32 grpNum)
{
    s32 j;
    MSM_GRP_STACK *grp;
    s32 i;
    s8 level;
    s8 stackBF;
    s32 grpMaxNum;

    if (sys.grpStackAOfs + sys.grpStackBOfs == 0) {
        return 0;
    }
    if (grpNum >= sys.grpStackAOfs + sys.grpStackBOfs) {
        grpNum = 0;
    }
    if (grpNum != 0) {
        msmSysDelGroupAll();
        for (i = 0; i < grpNum; i++) {
            grpMaxNum = 0;
            for (j = 0; j < sys.grpStackAMax; j++) {
                grp = &sys.grpStackA[j];
                if (grp->num > grpMaxNum) {
                    grpMaxNum = grp->num;
                    level = j;
                    stackBF = FALSE;
                }
            }
            for (j = 0; j < sys.grpStackBMax; j++) {
                grp = &sys.grpStackB[j];
                if (grp->num > grpMaxNum) {
                    grpMaxNum = grp->num;
                    level = j;
                    stackBF = TRUE;
                }
            }
            if (stackBF == FALSE) {
                grp = &sys.grpStackA[level];
                sys.grpStackADepth--;
                sys.grpStackAOfs--;
            } else {
                grp = &sys.grpStackB[level];
                sys.grpStackBDepth--;
                sys.grpStackBOfs--;
            }
            sndPopGroup();
            msmSysFreeStackSDir(grp);
            sys.aramP -= sys.grpInfo[grp->grpId].sampSize;
            grp->baseGrpF = 0;
            grp->num = 0;
        }
    } else {
        for (i = 0; i < sys.grpStackAMax; i++) {
            grp = &sys.grpStackA[i];
            if (grp->num != 0) {
                sndPopGroup();
                msmSysFreeStackSDir(grp);
                sys.aramP -= sys.grpInfo[grp->grpId].sampSize;
                grp->baseGrpF = 0;
                grp->num = 0;
            }
        }
        for (i = 0; i < sys.grpStackBMax; i++) {
            grp = &sys.grpStackB[i];
            if (grp->num != 0) {
                sndPopGroup();
                msmSysFreeStackSDir(grp);
                sys.aramP -= sys.grpInfo[grp->grpId].sampSize;
                grp->baseGrpF = 0;
                grp->num = 0;
            }
        }
        sys.grpStackBOfs = 0;
        sys.grpStackBDepth = 0;
        sys.grpStackAOfs = 0;
        sys.grpStackADepth = 0;
    }
    return 0;
}

static s32 msmSysPushGroup(DVDFileInfo *file, void *buf, MSM_GRP_STACK *grp, s32 grpId)
{
    s32 result;
    MSM_GRP_INFO *grpInfo;
    MSM_GRP_HEAD *grpBuf;

    grpInfo = &sys.grpInfo[grpId];
    msmSysFreeStackSDir(grp);
    if (msmFioRead(file, grp->buf, grpInfo->dataSize, grpInfo->dataOfs + sys.header->grpDataOfs) < 0) {
        return MSM_ERR_READFAIL;
    }
    if (msmFioRead(file, buf, grpInfo->sampSize, grpInfo->sampOfs + sys.header->sampOfs) < 0) {
        return MSM_ERR_READFAIL;
    }
    grpBuf = grp->buf;
    result = msmSysPrepareGroup(grpBuf, grpInfo->dataSize, grpId, &grp->sdirHost);
    if (result != 0) {
        return result;
    }
    if (!sndPushGroup((void*) ((uintptr_t)grpBuf + grpBuf->projOfs), grpInfo->gid, buf,
        grp->sdirHost, (void*) ((uintptr_t)grpBuf + grpBuf->poolOfs)))
    {
        msmSysFreeStackSDir(grp);
        return MSM_ERR_GRP_FAILPUSH;
    }
    sys.aramP += grpInfo->sampSize;
    grp->grpId = grpId;
    grp->num = sys.grpNum++;
    return 0;
}

s32 msmSysLoadGroupBase(s32 grpId, void *buf)
{
    s32 temp_r29;
    s32 temp_r28;
    s32 var_r23;
    s32 temp_r3_2;
    MSM_GRP_STACK *var_r24;
    DVDFileInfo sp10;

    if (grpId < 1 || grpId >= sys.grpMax) {
        return MSM_ERR_64;
    }
    var_r23 = msmSysDelGroupAll();
    if (var_r23 != 0) {
        return var_r23;
    }
    temp_r29 = sys.baseGrpNum + sys.grpStackAOfs + sys.grpStackBOfs;
    if (msmSysCheckBaseGroupNo(grpId)) {
        return 0;
    }
    if (temp_r29 >= 0xF) {
        return MSM_ERR_STACK_OVERFLOW;
    }
    temp_r3_2 = msmSysSearchGroupStack(grpId, -1);
    if (temp_r3_2 < 0) {
        return MSM_ERR_STACK_OVERFLOW;
    }
    temp_r28 = sys.grpInfo[grpId].stackNo;
    if (!temp_r28) {
        var_r24 = &sys.grpStackA[temp_r3_2];
    } else {
        var_r24 = &sys.grpStackB[temp_r3_2];
    }
    if (msmFioOpen(sys.msmEntryNum, &sp10) != 1) {
        return MSM_ERR_OPENFAIL;
    }
    var_r23 = msmSysPushGroup(&sp10, buf, var_r24, grpId);
    if (var_r23 != 0) {
        msmFioClose(&sp10);
        return var_r23;
    }
    msmFioClose(&sp10);
    sys.info->baseGrp[temp_r29] = grpId;
    var_r24->baseGrpF = 1;
    if (temp_r28 == 0) {
        sys.grpStackAOfs++;
        sys.grpStackADepth++;
    } else {
        sys.grpStackBOfs++;
        sys.grpStackBDepth++;
    }
    return 0;
}

static s32 msmSysGetNumGroupSet(void)
{
    if (sys.grpSet != NULL) {
        return sys.grpSet->numGrpSet;
    }
    return 0;
}

s32 msmSysLoadGroupSet(s32 arg0, void *arg1)
{
    s8 grpId[10];
    DVDFileInfo file;
    s32 result;
    s32 stackLevel;
    s32 pushResult;
    s32 i;
    s32 grpSetNum;
    s8 *grpSet;

    if (msmSysGetNumGroupSet() == 0) {
        return 0;
    }
    result = msmSysDelGroupAll();
    if (result != 0) {
        return result;
    }
    grpSet = &sys.grpSet->data[sys.grpSet->grpSetW * arg0];
    if (msmFioOpen(sys.msmEntryNum, &file) != TRUE) {
        return MSM_ERR_OPENFAIL;
    }
    sys.grpStackADepth = sys.grpStackAOfs;
    grpSetNum = 0;
    for (; *grpSet != 0; grpSet++) {
        if (msmSysCheckBaseGroupNo(*grpSet)) {
            continue;
        }
        if (sys.grpInfo[(s8) *grpSet].stackNo == 1) {
            grpId[grpSetNum++] = *grpSet;
        } else {
            stackLevel = msmSysSearchGroupStack(*grpSet, -1);
            if (stackLevel < 0) {
                msmFioClose(&file);
                return MSM_ERR_STACK_OVERFLOW;
            }
            pushResult = msmSysPushGroup(&file, arg1, &sys.grpStackA[stackLevel], *grpSet);
            if (pushResult != 0) {
                msmFioClose(&file);
                return pushResult;
            }
            sys.grpStackADepth++;
        }
    }
    sys.grpStackBDepth = sys.grpStackBOfs;
    for (i = 0; i < grpSetNum; i++) {
        stackLevel = msmSysSearchGroupStack(grpId[i], -1);
        if (stackLevel < 0) {
            msmFioClose(&file);
            return MSM_ERR_STACK_OVERFLOW;
        }
        pushResult = msmSysPushGroup(&file, arg1, &sys.grpStackB[stackLevel], grpId[i]);
        if (pushResult != 0) {
            msmFioClose(&file);
            return pushResult;
        }
        sys.grpStackBDepth++;
    }
    msmFioClose(&file);
    return 0;
}

static s32 msmSysLoadGroupSub(DVDFileInfo *file, s32 grpId, void *buf)
{
    s32 grpIdResult;
    s32 i;
    s32 stackLevel;
    s32 result;
    u8 *stackDepth;
    MSM_GRP_STACK *grpStack;
    MSM_GRP_INFO *temp_r23;

    grpIdResult = 0;
    temp_r23 = &sys.grpInfo[grpId];
    if (temp_r23->stackNo == 0) {
        grpStack = sys.grpStackA;
        stackDepth = &sys.grpStackADepth;
    } else {
        grpStack = sys.grpStackB;
        stackDepth = &sys.grpStackBDepth;
    }
    if (temp_r23->subGrpId != 0) {
        if (!msmSysCheckBaseGroup(sys.grpInfo[temp_r23->subGrpId].gid)) {
            stackLevel = -1;
            for (i = 0; i < 2; i++) {
                stackLevel = msmSysSearchGroupStack(temp_r23->subGrpId, stackLevel);
                if (stackLevel < 0) {
                    stackLevel = -(stackLevel + 1);
                    (*stackDepth)--;
                    sndPopGroup();
                    msmSysFreeStackSDir(&grpStack[stackLevel]);
                    sys.aramP -= sys.grpInfo[grpStack[stackLevel].grpId].sampSize;
                    grpIdResult = grpStack[stackLevel].grpId;
                    grpStack[stackLevel].num = 0;
                }
            }
            result = msmSysPushGroup(file, buf, &grpStack[stackLevel], temp_r23->subGrpId);
            if (result != 0) {
                return result;
            }
            (*stackDepth)++;
        }
    }
    stackLevel = msmSysSearchGroupStack(grpId, -1);
    if (stackLevel < 0) {
        stackLevel = -(stackLevel + 1);
        (*stackDepth)--;
        sndPopGroup();
        msmSysFreeStackSDir(&grpStack[stackLevel]);
        sys.aramP -= sys.grpInfo[grpStack[stackLevel].grpId].sampSize;
        grpIdResult = grpStack[stackLevel].grpId;
    }
    result = msmSysPushGroup(file, buf, &grpStack[stackLevel], grpId);
    if (result == 0) {
        result = grpIdResult;
    }
    (*stackDepth)++;
    return result;
}

static void msmSysPopGroup(s32 no)
{
    MSM_GRP_STACK *grp;

    grp = &sys.grpStackB[no];
    if (grp->num != 0 && grp->baseGrpF == 0) {
        sndPopGroup();
        msmSysFreeStackSDir(grp);
        sys.aramP -= sys.grpInfo[grp->grpId].sampSize;
    }
}

s32 msmSysLoadGroup(s32 grpId, void *buf)
{
    MSM_GRP_STACK *grpStack;
    MSM_GRP_INFO *grpInfo;
    s32 pushResult;
    s32 i;
    s32 result;
    DVDFileInfo file;
    s32 unused;

    if (buf == NULL) {
        return 0;
    }
    if (grpId == 0) {
        return msmSysLoadBaseGroup(buf);
    }
    grpInfo = &sys.grpInfo[grpId];
    if (msmSysCheckLoadGroupID(grpInfo->gid)) {
        return 0;
    }
    if (msmFioOpen(sys.msmEntryNum, &file) != TRUE) {
        return MSM_ERR_OPENFAIL;
    }
    if (grpInfo->stackNo == 0) {
        for (i = 0; i < sys.grpStackBMax; i++) {
            msmSysPopGroup(i);
        }
        result = msmSysLoadGroupSub(&file, grpId, buf);
        for (i = 0; i < sys.grpStackBMax; i++) {
            grpStack = &sys.grpStackB[i];
            if (grpStack->num != 0 && grpStack->baseGrpF == 0) {
                pushResult = msmSysPushGroup(&file, buf, grpStack, grpStack->grpId);
                if (pushResult != 0) {
                    msmFioClose(&file);
                    return pushResult;
                }
            }
        }
    } else {
        result = msmSysLoadGroupSub(&file, grpId, buf);
    }
    msmFioClose(&file);
    return result;
}

void msmSysCheckInit(void)
{
    sndIsInstalled();
}

s32 msmSysInit(MSM_INIT *init, MSM_ARAM *aram)
{
    s32 result;
    void *temp;

    SND_HOOKS sndHooks = { msmMemAlloc, msmMemFree };
    DVDFileInfo sp10;
    if (sndIsInstalled() == 1) {
        return MSM_ERR_INSTALLED;
    }
    result = 0; // retErr
    sys.irqDepth = 0;
    msmMemInit(init->heap, init->heapSize);
    msmFioInit(init->open, init->read, init->close);
    sys.msmEntryNum = DVDConvertPathToEntrynum(init->msmPath);
    if (sys.msmEntryNum < 0) {
        return MSM_ERR_OPENFAIL;
    }
    if (msmFioOpen(sys.msmEntryNum, &sp10) != 1) {
        return MSM_ERR_OPENFAIL;
    }
    if ((sys.header = msmMemAlloc(0x60)) == NULL) {
        msmFioClose(&sp10);
        return MSM_ERR_OUTOFMEM;
    }
    if (msmFioRead(&sp10, sys.header, 0x60, 0) < 0) {
        msmFioClose(&sp10);
        return MSM_ERR_READFAIL;
    }
    /* MSM data is GameCube big-endian; byte-swap the all-u32 header on LE hosts. */
    {
        u32* p = (u32*)sys.header;
        u32 n = 0x60 / 4;
        while (n--) { p[n] = __builtin_bswap32(p[n]); }
    }
    if (sys.header->version != MSM_FILE_VERSION) {
        msmFioClose(&sp10);
        return MSM_ERR_INVALIDFILE;
    }
    if ((sys.info = msmMemAlloc(sys.header->infoSize)) == NULL) {
        msmFioClose(&sp10);
        return MSM_ERR_OUTOFMEM;
    }
    if (msmFioRead(&sp10, sys.info, sys.header->infoSize, sys.header->infoOfs) < 0) {
        msmFioClose(&sp10);
        return MSM_ERR_READFAIL;
    }
    /* byte-swap MSM_INFO multi-byte fields (GameCube big-endian -> host LE). */
    sys.info->musMax = __builtin_bswap16(sys.info->musMax);
    sys.info->seMax = __builtin_bswap16(sys.info->seMax);
    sys.info->minMem = __builtin_bswap32(sys.info->minMem);
    sys.info->aramSize = __builtin_bswap32(sys.info->aramSize);
    sys.info->grpBufSizeA = __builtin_bswap32(sys.info->grpBufSizeA);
    sys.info->grpBufSizeB = __builtin_bswap32(sys.info->grpBufSizeB);
    sys.info->dummyMusSize = __builtin_bswap32(sys.info->dummyMusSize);
    sys.info->unk24 = __builtin_bswap32(sys.info->unk24);
    if (aram != NULL) {
        if (aram->skipARInit == 0) {
            ARInit(aram->stackIndex, aram->aramEnd);
            ARQInit();
            aram = (MSM_ARAM *)ARAlloc(sys.info->aramSize);
            if ((u32)aram != ARGetBaseAddress()) {
                msmFioClose(&sp10);
                return MSM_ERR_OUTOFAMEM;
            }
            sys.arInitF = FALSE;
        }
        else {
            if ((sys.info->aramSize + ARGetBaseAddress()) > aram->aramEnd) {
                msmFioClose(&sp10);
                return MSM_ERR_OUTOFAMEM;
            }
            ARInit(NULL, 0);
            ARQInit();
            sys.arInitF = TRUE;
        }
    }
    result = msmSysGroupInit(&sp10);
    if (result != 0) {
        msmFioClose(&sp10);
        return result;
    }
    result = msmMusInit(&sys, &sp10);
    if (result != 0) {
        msmFioClose(&sp10);
        return result;
    }
    result = msmSeInit(&sys, &sp10);
    if (result != 0) {
        msmFioClose(&sp10);
        return result;
    }
    sys.auxParamNo[0] = sys.info->auxParamA == MSM_AUXNO_NULL ? MSM_AUXNO_NULL : MSM_AUXNO_UNSET;
    sys.auxParamNo[1] = sys.info->auxParamB == MSM_AUXNO_NULL ? MSM_AUXNO_NULL : MSM_AUXNO_UNSET;
    if ((s32)sys.header->auxParamSize == 0) {
        result = 0;
    }
    else {
        if ((sys.auxParam = msmMemAlloc(sys.header->auxParamSize)) == NULL) {
            result = MSM_ERR_OUTOFMEM;
        }
        else {
            if (msmFioRead(&sp10, sys.auxParam, sys.header->auxParamSize, sys.header->auxParamOfs) < 0) {
                result = MSM_ERR_READFAIL;
            }
            else {
                /* byte-swap MSM_AUXPARAM union fields (big-endian -> host LE).
                 * Each struct: s8 type + pad[3] (4B, no swap) + union of u32/f32 (9 words). */
                u8* abase = (u8*)sys.auxParam;
                u32 acount = sys.header->auxParamSize / 40;
                for (u32 ai = 0; ai < acount; ai++) {
                    u32* uw = (u32*)(abase + ai * 40 + 4);
                    for (int aj = 0; aj < 9; aj++) uw[aj] = __builtin_bswap32(uw[aj]);
                }
                result = 0;
            }
        }
    }
    if (result != 0) {
        msmFioClose(&sp10);
        return result;
    }
    msmFioClose(&sp10);
    result = msmStreamInit(init->pdtPath);
    if (result < 0) {
        return result;
    }
    AIInit(NULL);
    sndSetHooks(&sndHooks);
    if (sndInit(sys.info->voices, sys.info->music, sys.info->sfx, 1, 0, sys.info->aramSize) != 0) {
        return MSM_ERR_INITFAIL;
    }
    sys.oldAIDCallback = AIRegisterDMACallback(msmSysServer);
    sys.timer = 1;
    result = msmStreamAmemAlloc();
    if (result < 0) {
        sndQuit();
        return result;
    }
    sys.aramP = result + 0x500;
    if ((int)sys.info->minMem != 0) {
        temp = msmMemAlloc(sys.info->minMem + 0x100);
        if (temp == NULL) {
            msmStreamAmemFree();
            sndQuit();
            return MSM_ERR_OUTOFMEM;
        }
        msmMemFree(temp);
    }
    if (msmSysSetAuxParam(sys.info->auxParamA, sys.info->auxParamB) != 0) {
        msmStreamAmemFree();
        sndQuit();
        return MSM_ERR_INVALID_AUXPARAM;
    }
    msmSysSetOutputMode(OSGetSoundMode() == 0 ? SND_OUTPUTMODE_MONO : SND_OUTPUTMODE_STEREO);
    sndVolume(0x7F, 0, 0xFF);
    return 0;
}
