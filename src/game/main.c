#include "game/data.h"
#include "game/dvd.h"
#include "game/gamework.h"
#include "game/gamework_data.h"
#include "game/hsfformat.h"
#include "game/hu3d.h"
#include "game/init.h"
#include "game/minigame_seq.h"
#include "game/msm.h"
#include "game/object.h"
#include "game/pad.h"
#include "game/perf.h"
#include "game/printfunc.h"
#include "game/process.h"
#include "game/sprite.h"
#include "game/sreset.h"
#include "game/wipe.h"
#include "version.h"

#ifdef TARGET_PC
#include "game/disp.h"
#include "port/settings.h"
#include "port/imgui.h"
#include "port/main.h"
#include "port/dolassets.h"
#include "port/ui.h"
#include "aurora/dvd.h"
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <SDL3/SDL_gamepad.h>
#include <stdlib.h>

const char *__asan_default_options()
{
    return "new_delete_type_mismatch=0,sleep_before_dying=5,allocator_may_return_null=1";
}

bool PartyBoard_IsRunning = TRUE;
bool PartyBoard_IsShuttingDown = FALSE;
bool PartyBoard_IsGameLaunched = FALSE;
bool PartyBoard_RestartRequested = FALSE;

bool disableFrameLimiter = FALSE;

typedef struct PartyBoardExitChord {
    SDL_JoystickID joystick;
    bool selectHeld;
    bool startHeld;
} PartyBoardExitChord;

static bool PartyBoard_HandleExitChord(const SDL_Event *event)
{
    static PartyBoardExitChord pads[4];
    PartyBoardExitChord *pad = NULL;
    s32 i;

    if (event->type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (i = 0; i < 4; i++) {
            if (pads[i].joystick == event->gdevice.which) {
                memset(&pads[i], 0, sizeof(pads[i]));
                break;
            }
        }
        return FALSE;
    }
    if (event->type != SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
        event->type != SDL_EVENT_GAMEPAD_BUTTON_UP) {
        return FALSE;
    }
    if (event->gbutton.button != SDL_GAMEPAD_BUTTON_BACK &&
        event->gbutton.button != SDL_GAMEPAD_BUTTON_START) {
        return FALSE;
    }

    for (i = 0; i < 4; i++) {
        if (pads[i].joystick == event->gbutton.which) {
            pad = &pads[i];
            break;
        }
        if (pad == NULL && pads[i].joystick == 0) {
            pad = &pads[i];
        }
    }
    if (pad == NULL) {
        return FALSE;
    }
    if (pad->joystick == 0) {
        pad->joystick = event->gbutton.which;
    }

    if (event->gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
        pad->selectHeld = event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    } else {
        pad->startHeld = event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    }
    if (!pad->selectHeld || !pad->startHeld) {
        return FALSE;
    }

    OSReport("[input] Select + Start exit requested\n");
    PartyBoard_IsShuttingDown = TRUE;
    PartyBoard_IsRunning = FALSE;
    return TRUE;
}
#endif

extern FileListEntry _ovltbl[];
SHARED_SYM u32 GlobalCounter;
static u32 vcheck;
static u32 vmiss;
static u32 vstall;
static u32 top_pixels_in;
static u32 top_pixels_out;
static u32 bot_pixels_in;
static u32 bot_pixels_out;
static u32 clr_pixels_in;
static u32 total_copy_clks;
static u32 cp_req;
static u32 tc_req;
static u32 cpu_rd_req;
static u32 cpu_wr_req;
static u32 dsp_req;
static u32 io_req;
static u32 vi_req;
static u32 pe_req;
static u32 rf_req;
static u32 fi_req;
s32 HuDvdErrWait;
SHARED_SYM s32 SystemInitF;

#ifdef TARGET_PC
void PartyBoard_RequestRestart(void)
{
    PartyBoard_RestartRequested = SUPPORTS_PROCESS_RESTART;
    PartyBoard_IsRunning = FALSE;
}

/*
 * Dev-only frame anatomy, enabled with PARTYBOARD_PERF_SPLIT=1: accumulates the
 * recording-thread time of each main-loop phase and reports averages every 300
 * frames.  Complements aurora's [fps] stall split (which covers the aurora side).
 */
#include <time.h>
enum {
    PB_SPLIT_EVENTS,
    PB_SPLIT_LOGIC,
    PB_SPLIT_HU3D,
    PB_SPLIT_DONE_RENDER,
    PB_SPLIT_END_FRAME,
    PB_SPLIT_LIMITER,
    PB_SPLIT_COUNT
};
static s32 pbSplitEnabled = -1;
static s64 pbSplitNs[PB_SPLIT_COUNT];
static u32 pbSplitFrames;
static s64 pbSplitMark;

static s64 PartyBoard_PerfNow(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (s64)ts.tv_sec * 1000000000ll + ts.tv_nsec;
}

static void PartyBoard_PerfSplitPhase(s32 phase)
{
    s64 now;

    if (pbSplitEnabled < 0) {
        const char *env = getenv("PARTYBOARD_PERF_SPLIT");
        pbSplitEnabled = env != NULL && env[0] == '1';
    }
    if (!pbSplitEnabled) {
        return;
    }
    now = PartyBoard_PerfNow();
    if (phase >= 0) {
        pbSplitNs[phase] += now - pbSplitMark;
    }
    pbSplitMark = now;
    if (phase == PB_SPLIT_LIMITER && ++pbSplitFrames == 300) {
        const double inv = 1.0 / (300.0 * 1.0e6);
        OSReport("[perf-split] events %.1f logic %.1f hu3d %.1f done %.1f end %.1f limit %.1f ms/frame\n",
            pbSplitNs[PB_SPLIT_EVENTS] * inv, pbSplitNs[PB_SPLIT_LOGIC] * inv, pbSplitNs[PB_SPLIT_HU3D] * inv,
            pbSplitNs[PB_SPLIT_DONE_RENDER] * inv, pbSplitNs[PB_SPLIT_END_FRAME] * inv,
            pbSplitNs[PB_SPLIT_LIMITER] * inv);
        memset(pbSplitNs, 0, sizeof(pbSplitNs));
        pbSplitFrames = 0;
    }
}
#endif

#ifdef TARGET_PC
int game_main(void)
#else
void main(void)
#endif
{
    u32 met0;
    u32 met1;
    s16 i;
    s32 retrace;
#if VERSION_PAL
    s16 temp = 0;
#endif

    HuDvdErrWait = 0;
    SystemInitF = 0;
#if VERSION_NTSC
    HuSysInit(&GXNtsc480IntDf);
#else
    HuSysInit(&GXPal528IntDf);
#endif
    HuPrcInit();
    HuPadInit();
    GWInit();
    pfInit();
    GlobalCounter = 0;
    HuSprInit();
    Hu3DInit();
    HuDataInit();
    HuPerfInit();
    HuPerfCreate("USR0", 0xFF, 0xFF, 0xFF, 0xFF);
    HuPerfCreate("USR1", 0, 0xFF, 0xFF, 0xFF);
    WipeInit(RenderMode);

    for (i = 0; i < 4; i++) {
        GWPlayerCfg[i].character = -1;
    }
    
    omMasterInit(0, _ovltbl, DLL_MAX, DLL_bootdll);
    VIWaitForRetrace();

    if (VIGetNextField() == 0) {
        OSReport("VI_FIELD_BELOW\n");
        VIWaitForRetrace();
    }
#ifdef TARGET_PC
    while (PartyBoard_IsRunning) {
#else
    while (1) {
#endif
#ifdef TARGET_PC
        PartyBoard_PerfSplitPhase(-1);
        const AuroraEvent *event = aurora_update();
        bool exiting = false;
        while (event != NULL && event->type != AURORA_NONE) {
            if (event->type == AURORA_EXIT) {
                exiting = true;
                break;
            }
            if (event->type == AURORA_SDL_EVENT) {
                if (PartyBoard_HandleExitChord(&event->sdl)) {
                    exiting = true;
                    break;
                }
                ui_handle_sdl_event(&event->sdl);
                if (partyboard_settings_enableTurboKeybind()) {
                    if (event->sdl.type == SDL_EVENT_KEY_DOWN) {
                        if (event->sdl.key.scancode == SDL_SCANCODE_TAB) {
                            disableFrameLimiter = TRUE;
                        }
                    } else if (event->sdl.type == SDL_EVENT_KEY_UP) {
                        if (event->sdl.key.scancode == SDL_SCANCODE_TAB) {
                            disableFrameLimiter = FALSE;
                        }
                    }
                }
            }
            ++event;
        }
        if (exiting) {
            break;
        }
#endif
        retrace = VIGetRetraceCount();
        if (HuSoftResetButtonCheck() != 0 || HuDvdErrWait != 0) {
            continue;
        }
        HuPerfZero();

        HuPerfBegin(2);
#ifdef TARGET_PC
        aurora_begin_frame();
#endif
        HuSysBeforeRender();
#ifdef TARGET_PC
        PartyBoard_PerfSplitPhase(PB_SPLIT_EVENTS);
#endif
        GXSetGPMetric(GX_PERF0_CLIP_VTX, GX_PERF1_VERTICES);
        GXClearGPMetric();
        GXSetVCacheMetric(GX_VC_ALL);
        GXClearVCacheMetric();
        GXClearPixMetric();
        GXClearMemMetric();

        HuPerfBegin(0);
        Hu3DPreProc();
        HuPadRead();
        pfClsScr();

        HuPrcCall(1);
        MGSeqMain();
#ifdef TARGET_PC
        PartyBoard_PerfSplitPhase(PB_SPLIT_LOGIC);
#endif
        HuPerfBegin(1);
        Hu3DExec();
        HuDvdErrorWatch();
        WipeExecAlways();
        HuPerfEnd(0);

        pfDrawFonts();
        HuPerfEnd(1);
#ifdef TARGET_PC
        PartyBoard_PerfSplitPhase(PB_SPLIT_HU3D);
#endif

        msmMusFdoutEnd();
        HuSysDoneRender(retrace);
        GXReadGPMetric(&met0, &met1);
        GXReadVCacheMetric(&vcheck, &vmiss, &vstall);
        GXReadPixMetric(&top_pixels_in, &top_pixels_out, &bot_pixels_in, &bot_pixels_out, &clr_pixels_in, &total_copy_clks);
        GXReadMemMetric(&cp_req, &tc_req, &cpu_rd_req, &cpu_wr_req, &dsp_req, &io_req, &vi_req, &pe_req, &rf_req, &fi_req);
        HuPerfEnd(2);
        GlobalCounter++;

#ifdef TARGET_PC
        PartyBoard_PerfSplitPhase(PB_SPLIT_DONE_RENDER);
        ui_update();
        aurora_end_frame();
        PartyBoard_PerfSplitPhase(PB_SPLIT_END_FRAME);
        if (!disableFrameLimiter) {
            frame_limiter();
        }
        PartyBoard_PerfSplitPhase(PB_SPLIT_LIMITER);
#endif
    }

#ifdef TARGET_PC
    return 0;
#endif
}

void HuSysVWaitSet(s16 vcount)
{
    minimumVcount = vcount;
    minimumVcountf = vcount;
}

s16 HuSysVWaitGet(s16 param)
{
    return (s16)minimumVcount;
}

s32 rnd_seed = 0x0000D9ED;

s32 rand8(void)
{
    rnd_seed = (rnd_seed * 0x41C64E6D) + 0x3039;
    return (u8)(((rnd_seed + 1) >> 16) & 0xFF);
}
