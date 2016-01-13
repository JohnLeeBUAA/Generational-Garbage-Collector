/*
 * Allocation functions
 *
 * Copyright (c) 2014, 2015 Gregor Richards
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define _BSD_SOURCE /* for MAP_ANON */
#define _DARWIN_C_SOURCE /* for MAP_ANON on OS X */

/* for standards info */
#if defined(unix) || defined(__unix) || defined(__unix__) || \
    (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#if _POSIX_VERSION
#include <sys/mman.h>
#endif

#include "ggggc/gc.h"
#include "ggggc-internals.h"

#ifdef __cplusplus
extern "C" {
#endif

/* figure out which allocator to use */
#if defined(GGGGC_USE_MALLOC)
#define GGGGC_ALLOCATOR_MALLOC 1
#include "allocate-malloc.c"

#elif _POSIX_ADVISORY_INFO >= 200112L
#define GGGGC_ALLOCATOR_POSIX_MEMALIGN 1
#include "allocate-malign.c"

#elif defined(MAP_ANON)
#define GGGGC_ALLOCATOR_MMAP 1
#include "allocate-mmap.c"

#elif defined(_WIN32)
#define GGGGC_ALLOCATOR_VIRTUALALLOC 1
#include "allocate-win-valloc.c"

#else
#warning GGGGC: No allocator available other than malloc!
#define GGGGC_ALLOCATOR_MALLOC 1
#include "allocate-malloc.c"

#endif

/* pools which are freely available */
static struct GGGGC_Pool *freePoolsHead, *freePoolsTail;

/* allocate and initialize a pool */
static struct GGGGC_Pool *newPool(int mustSucceed)
{
    struct GGGGC_Pool *ret;

    ret = NULL;

    /* try to reuse a pool */
    if (freePoolsHead) {
        if (freePoolsHead) {
            ret = freePoolsHead;
            freePoolsHead = freePoolsHead->next;
            if (!freePoolsHead) freePoolsTail = NULL;
        }
    }

    /* otherwise, allocate one */
    if (!ret) ret = (struct GGGGC_Pool *) allocPool(mustSucceed);

    if (!ret) return NULL;

    /* set it up */
    ret->next = NULL;
    ret->free = ret->start;
    ret->end = (ggc_size_t *) ((unsigned char *) ret + GGGGC_POOL_BYTES);

    return ret;
}

static struct GGGGC_PoolOld *newPoolOld(int mustSucceed)
{
    struct GGGGC_PoolOld *ret;

    ret = NULL;

    /* try to reuse a pool */
    if (freePoolsHead) {
        if (freePoolsHead) {
            ret = (struct GGGGC_PoolOld *)freePoolsHead;
            freePoolsHead = freePoolsHead->next;
            if (!freePoolsHead) freePoolsTail = NULL;
        }
    }

    /* otherwise, allocate one */
    if (!ret) ret = (struct GGGGC_PoolOld *) allocPool(mustSucceed);

    if (!ret) return NULL;

    /* set it up */
    ret->next = NULL;
    memset(ret->rememberSet, 0, REMEMBERSET_SIZE*sizeof(ggc_size_t));
    ret->maxRememberSetIndex = 0;
    ret->minRememberSetIndex = REMEMBERSET_SIZE - 1;
    ret->free = ret->start;
    ret->end = (ggc_size_t *) ((unsigned char *) ret + GGGGC_POOL_BYTES);

    return ret;
}

void ggggc_expandB0()
{
    while (pCtB0 < pCtB1 * B0_B1_RATIO) {
        b0End->next = newPool(1);
        b0End = b0End->next;
        b0End->gen = GEN_OF_B0;

        pCtB0++;
    }
}

void ggggc_expandB1(int poolsNeed)
{
    int i;
    for (i = 0; i < poolsNeed; i++) {
        b1ToEnd->next = newPool(1);
        b1ToEnd = b1ToEnd->next;
        b1ToEnd->gen = GEN_OF_B1TO;

        b1FromEnd->next = newPool(1);
        b1FromEnd = b1FromEnd->next;
        b1FromEnd->gen = GEN_OF_B1FROM;

        pCtB1 += 2;
    }
}

void ggggc_expandOld(int poolsNeed)
{
    int i;
    for (i = 0; i < poolsNeed; i++) {
        oldEnd->next = newPoolOld(1);
        oldEnd = oldEnd->next;
        oldEnd->gen = GEN_OF_OLD;

        pCtOld++;
    }
}

static void initialize()
{
    GEN_OF_B1TO = 0;
    GEN_OF_B1FROM = 1;
    GEN_OF_B0 = 2;
    GEN_OF_OLD = 3;

    b1ToHead = newPool(1);
    b1ToHead->gen = GEN_OF_B1TO;
    b1ToEnd = b1ToHead;
    b1FromHead = newPool(1);
    b1FromHead->gen = GEN_OF_B1FROM;
    b1FromEnd = b1FromHead;
    pCtB1 = 2;
    lCtB1 = 0;
    b1ToCur = b1ToHead;
    b1FromCur = b1FromHead;

    b0Head = newPool(1);
    b0Head->gen = GEN_OF_B0;
    b0End = b0Head;
    pCtB0 = 1;
    ggggc_expandB0();
    b0Cur = b0Head;

    oldHead = newPoolOld(1);
    oldHead->gen = GEN_OF_OLD;
    oldEnd = oldHead;
    pCtOld = 1;
    lCtOld = 0;
    oldCur = oldHead;

    inCollect = 0;
    inCollectFull = 0;
    mustAllocPool = 0;
    skipFreelist = 0;
    freelisthops = 0;
}

/* heuristically expand a generation if it has too many survivors */
void ggggc_expandGeneration(struct GGGGC_Pool *pool)
{
    ggc_size_t space, survivors, poolCt;

    if (!pool) return;

    /* first figure out how much space was used */
    space = 0;
    survivors = 0;
    poolCt = 0;
    while (1) {
        space += pool->end - pool->start;
        survivors += pool->survivors;
        pool->survivors = 0;
        poolCt++;
        if (!pool->next) break;
        pool = pool->next;
    }

    /* now decide if it's too much */
    if (survivors > space/2) {
        /* allocate more */
        ggc_size_t i;
        for (i = 0; i < poolCt; i++) {
            pool->next = newPool(0);
            pool = pool->next;
            if (!pool) break;
        }
    }
}

/* free a generation (used when a thread exits) */
void ggggc_freeGeneration(struct GGGGC_Pool *pool)
{
    if (!pool) return;
    if (freePoolsHead) {
        freePoolsTail->next = pool;
    } else {
        freePoolsHead = pool;
    }
    while (pool->next) pool = pool->next;
    freePoolsTail = pool;
}

void *ggggc_malloc(struct GGGGC_Descriptor *descriptor)
{
    ggc_size_t *ret = NULL;
    
    if (!b0Cur) {
        initialize();
    }

    /* round up size to the nearest even number for freeobj */
    descriptor->size = (descriptor->size + 1) & ~1;

    retry:
    
    /* bump pointer in B0 */
    while (1) {
        if ((b0Cur->end - b0Cur->free) >= descriptor->size) {
            ret = b0Cur->free;
            b0Cur->free += descriptor->size;
            memset(ret, 0, (descriptor->size)*sizeof(ggc_size_t));
            ((struct GGGGC_Header *)ret)->descriptor__ptr = descriptor;
            return ret;
        }
        if (b0Cur->next) {
            b0Cur = b0Cur->next;
        }
        else {
            break;
        }
    }

    /* if B0 is full, run a collection, get the correct descriptor and retry malloc */
    ggggc_collect();

    if (forwarded((ggc_size_t *)descriptor)) {
        descriptor = (struct GGGGC_Descriptor *)forwardingAddress((ggc_size_t *)descriptor);
    }

    goto retry;
}

void *ggggc_mallocB1(struct GGGGC_Descriptor *descriptor)
{
    ggc_size_t *ret = NULL;

    retry:

    if (mustAllocPool) {
        ggggc_expandB1(1);
        mustAllocPool = 0;
    }

    /* obj will be copied to here so no need to zero space or set header */
    /* bump pointer in B1 tospace */
    while (1) {
        if ((b1ToCur->end - b1ToCur->free) >= descriptor->size) {
            ret = b1ToCur->free;
            b1ToCur->free += descriptor->size;
            return ret;
        }
        if (b1ToCur->next) {
            b1ToCur = b1ToCur->next;
        }
        else {
            break;
        }
    }

    mustAllocPool = 1;
    
    goto retry;
}

void *ggggc_mallocOld(struct GGGGC_Descriptor *descriptor)
{
    ggc_size_t *ret = NULL, foSize;
    struct GGGGC_Freeobj *curFo, *newFo;

    retry:

    if (mustAllocPool) {
        ggggc_expandOld(1);
        mustAllocPool = 0;
    }

    /* no need to zero space or set header either */
    /* try freelist */
    if (!skipFreelist && freelist) {
        freelisthops = 0;
        curFo = freelist;
        while (curFo->next) {
            foSize = getFoSize(curFo->next);
            if (foSize > descriptor->size) {
                ret = (ggc_size_t *)(curFo->next);
                newFo = (struct GGGGC_Freeobj *)(ret + descriptor->size);
                newFo->next = curFo->next->next;
                newFo->selfend = curFo->next->selfend;
                curFo->next = newFo;
                return ret;
            }
            else if (foSize == descriptor->size) {
                ret = (ggc_size_t *)(curFo->next);
                curFo->next = curFo->next->next;
                return ret;
            }
            else {
                freelisthops++;
                curFo = curFo->next;
            }
        }
    }
    skipFreelist = 0;

    /* try freespace */
    while (1) {
        if ((oldCur->end - oldCur->free) >= descriptor->size) {
            ret = oldCur->free;
            oldCur->free += descriptor->size;
            return ret;
        }
        if (oldCur->next) {
            oldCur = oldCur->next;
        }
        else {
            break;
        }
    }

    if (inCollectFull) {
        /* if is in a re-try young collect, get new pools directly and retry malloc */
        mustAllocPool = 1;
        skipFreelist = 1;
        goto retry;
    }
    else {
        return NULL;
    }
}

struct GGGGC_Array {
    struct GGGGC_Header header;
    ggc_size_t length;
};

/* allocate a pointer array (size is in words) */
void *ggggc_mallocPointerArray(ggc_size_t sz)
{
    struct GGGGC_Descriptor *descriptor = ggggc_allocateDescriptorPA(sz + 1 + sizeof(struct GGGGC_Header)/sizeof(ggc_size_t));
    struct GGGGC_Array *ret = (struct GGGGC_Array *) ggggc_malloc(descriptor);
    ret->length = sz;
    return ret;
}

/* allocate a data array */
void *ggggc_mallocDataArray(ggc_size_t nmemb, ggc_size_t size)
{
    struct GGGGC_Descriptor *descriptor = NULL;
    struct GGGGC_Array *ret = NULL;
    ggc_size_t sz = ((nmemb*size)+sizeof(ggc_size_t)-1)/sizeof(ggc_size_t);

    GGC_PUSH_2(descriptor, ret);

    descriptor = ggggc_allocateDescriptorDA(sz + 1 + sizeof(struct GGGGC_Header)/sizeof(ggc_size_t));
    ret = (struct GGGGC_Array *) ggggc_malloc(descriptor);
    ret->length = nmemb;
    return ret;
}

/* allocate a descriptor-descriptor for a descriptor of the given size */
struct GGGGC_Descriptor *ggggc_allocateDescriptorDescriptor(ggc_size_t size)
{
    struct GGGGC_Descriptor tmpDescriptor, *ret;
    ggc_size_t ddSize;

    /* need one description bit for every word in the object */
    ddSize = GGGGC_WORD_SIZEOF(struct GGGGC_Descriptor) + GGGGC_DESCRIPTOR_WORDS_REQ(size);

    /* check if we already have a descriptor */
    if (ggggc_descriptorDescriptors[size])
        return ggggc_descriptorDescriptors[size];

    /* otherwise, need to allocate one. First lock the space */
    if (ggggc_descriptorDescriptors[size]) {
        return ggggc_descriptorDescriptors[size];
    }

    /* now make a temporary descriptor to describe the descriptor descriptor */
    tmpDescriptor.header.descriptor__ptr = NULL;
    tmpDescriptor.size = ddSize;
    tmpDescriptor.pointers[0] = GGGGC_DESCRIPTOR_DESCRIPTION;

    /* allocate the descriptor descriptor */
    ret = (struct GGGGC_Descriptor *) ggggc_malloc(&tmpDescriptor);

    /* make it correct */
    ret->size = size;
    ret->pointers[0] = GGGGC_DESCRIPTOR_DESCRIPTION;

    /* put it in the list */
    ggggc_descriptorDescriptors[size] = ret;
    GGC_PUSH_1(ggggc_descriptorDescriptors[size]);
    GGC_GLOBALIZE();

    /* and give it a proper descriptor */
    ggggc_descriptorDescriptors[size]->header.descriptor__ptr = ggggc_allocateDescriptorDescriptor(ddSize);

    return ggggc_descriptorDescriptors[size];
}

/* allocate a descriptor for an object of the given size in words with the
 * given pointer layout */
struct GGGGC_Descriptor *ggggc_allocateDescriptor(ggc_size_t size, ggc_size_t pointers)
{
    ggc_size_t pointersA[1];
    pointersA[0] = pointers;
    return ggggc_allocateDescriptorL(size, pointersA);
}

/* descriptor allocator when more than one word is required to describe the
 * pointers */
struct GGGGC_Descriptor *ggggc_allocateDescriptorL(ggc_size_t size, const ggc_size_t *pointers)
{
    struct GGGGC_Descriptor *dd, *ret;
    ggc_size_t dPWords, dSize;

    /* the size of the descriptor */
    if (pointers)
        dPWords = GGGGC_DESCRIPTOR_WORDS_REQ(size);
    else
        dPWords = 1;
    dSize = GGGGC_WORD_SIZEOF(struct GGGGC_Descriptor) + dPWords;

    /* get a descriptor-descriptor for the descriptor we're about to allocate */
    dd = ggggc_allocateDescriptorDescriptor(dSize);

    /* use that to allocate the descriptor */
    ret = (struct GGGGC_Descriptor *) ggggc_malloc(dd);
    ret->size = size;

    /* and set it up */
    if (pointers) {
        memcpy(ret->pointers, pointers, sizeof(ggc_size_t) * dPWords);
        ret->pointers[0] |= 1; /* first word is always the descriptor pointer */
    } else {
        ret->pointers[0] = 0;
    }

    return ret;
}

/* descriptor allocator for pointer arrays */
struct GGGGC_Descriptor *ggggc_allocateDescriptorPA(ggc_size_t size)
{
    ggc_size_t *pointers;
    ggc_size_t dPWords, i;

    /* fill our pointer-words with 1s */
    dPWords = GGGGC_DESCRIPTOR_WORDS_REQ(size);
    pointers = (ggc_size_t *) alloca(sizeof(ggc_size_t) * dPWords);
    for (i = 0; i < dPWords; i++) pointers[i] = (ggc_size_t) -1;

    /* get rid of non-pointers */
    pointers[0] &= ~0x2;

    /* and allocate */
    return ggggc_allocateDescriptorL(size, pointers);
}

/* descriptor allocator for data arrays */
struct GGGGC_Descriptor *ggggc_allocateDescriptorDA(ggc_size_t size)
{
    /* and allocate */
    return ggggc_allocateDescriptorL(size, NULL);
}

/* allocate a descriptor from a descriptor slot */
struct GGGGC_Descriptor *ggggc_allocateDescriptorSlot(struct GGGGC_DescriptorSlot *slot)
{
    if (slot->descriptor) return slot->descriptor;
    if (slot->descriptor) {
        return slot->descriptor;
    }

    slot->descriptor = ggggc_allocateDescriptor(slot->size, slot->pointers);

    /* make the slot descriptor a root */
    GGC_PUSH_1(slot->descriptor);
    GGC_GLOBALIZE();

    return slot->descriptor;
}

/* and a combined malloc/allocslot */
void *ggggc_mallocSlot(struct GGGGC_DescriptorSlot *slot)
{
    return ggggc_malloc(ggggc_allocateDescriptorSlot(slot));
}

#ifdef __cplusplus
}
#endif
