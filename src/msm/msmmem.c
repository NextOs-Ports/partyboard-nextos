#include "msm/msmmem.h"
#include <stdint.h>

typedef struct MSMBlock_s {
    struct MSMBlock_s* prev;
    struct MSMBlock_s* next;
    u32 freeSize;
    u32 size;
    void* ptr;
    char pad[12];
} MSMBLOCK;

typedef struct MSMMem_s {
    void *ptr;
    u32 size;
    MSMBLOCK *head;
    MSMBLOCK first;
} MSM_MEM;

static MSM_MEM mem;

#define MSM_BLOCK_ALIGNMENT 0x20
#define MSM_BLOCK_HEADER_SIZE ((sizeof(MSMBLOCK) + MSM_BLOCK_ALIGNMENT - 1) & ~(MSM_BLOCK_ALIGNMENT - 1))

void msmMemFree(void* ptr) {
    MSMBLOCK* block;
    MSMBLOCK* blockPrev;
    MSMBLOCK* blockNext;
    MSMBLOCK* blockHead;

    if (ptr == NULL) {
        return;
    }
    block = (MSMBLOCK*)((uintptr_t)ptr - MSM_BLOCK_HEADER_SIZE);
    blockPrev = block->prev;
    blockNext = block->next;
    if ((uintptr_t)mem.ptr > (uintptr_t)block
        || (uintptr_t)mem.ptr + mem.size <= (uintptr_t)block) {
        return;
    }

    if (blockPrev == NULL || (blockPrev->next != block) || (block->ptr != ptr)
        || (blockNext && (blockNext->prev != block))) {
        return;
    }
    
    blockPrev->size += block->freeSize + block->size;
    blockPrev->next = blockNext;
    blockHead = mem.head;
    if ((blockHead == block) || (blockHead->size < blockPrev->size)) {
        mem.head = blockPrev;
    }
    if (blockNext) {
        blockNext->prev = blockPrev;
        if (mem.head->size < blockNext->size) {
            mem.head = blockNext;
        }
    }
}

void* msmMemAlloc(u32 size) {
    s32 alignOfs;
    u32 freeSize;
    u32 allocSize;
    MSMBLOCK* block;
    MSMBLOCK* blockPrev;
    MSMBLOCK* blockNext;

    allocSize = size + MSM_BLOCK_HEADER_SIZE;
    alignOfs = allocSize & (MSM_BLOCK_ALIGNMENT - 1);
    if (alignOfs) {
        allocSize += MSM_BLOCK_ALIGNMENT - alignOfs;
    }
    if (mem.head->size >= allocSize) {
        blockPrev = mem.head;
    } else {
        blockPrev = &mem.first;
        
       do {
            if (blockPrev->size >= allocSize) break;
            blockPrev = blockPrev->next;
        } while (blockPrev);
        if (!blockPrev) {
            return NULL;
        }
    }
    
    freeSize = blockPrev->freeSize;
    if (freeSize != 0) {
        freeSize -= MSM_BLOCK_HEADER_SIZE;
    }
    block = (void*)((uintptr_t)blockPrev->ptr + (freeSize));
    blockNext = blockPrev->next;
    if ((uintptr_t)mem.ptr > (uintptr_t)block
        || (uintptr_t)mem.ptr + mem.size <= (uintptr_t)block) {
        return NULL;
    }
    block->freeSize = allocSize;
    block->size = blockPrev->size - allocSize;
    block->ptr = (void*)((uintptr_t)block + MSM_BLOCK_HEADER_SIZE);
    block->prev = blockPrev;
    block->next = blockNext;
    mem.head = block;
    blockPrev->size = 0;
    blockPrev->next = block;
    if (blockNext) {
        blockNext->prev = block;
        if (mem.head->size < blockNext->size) {
            mem.head = blockNext;
        }
    }
    return block->ptr;
}

void msmMemInit(void* ptr, u32 size) {
    MSMBLOCK* block;
    uintptr_t base = (uintptr_t)ptr;
    uintptr_t ofs = base & (MSM_BLOCK_ALIGNMENT - 1);
    if (ofs) {
        ofs = MSM_BLOCK_ALIGNMENT - ofs;
    }

    mem.ptr = (void*)(base + ofs);
    mem.size = (u32)(((base + size) - (uintptr_t)mem.ptr) & ~(uintptr_t)(MSM_BLOCK_ALIGNMENT - 1));
    block = &mem.first;
    block->freeSize = 0;
    block->size = mem.size;
    block->ptr = mem.ptr;
    block->prev = NULL;
    block->next = NULL;
    mem.head = &mem.first;
}
