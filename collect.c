/*
 * The collector
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "ggggc/gc.h"
#include "ggggc-internals.h"

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************************/
/* functions */

static void mark(ggc_size_t *obj)
{
    *obj |= 1;
}

static void unmark(ggc_size_t *obj)
{
    *obj &= ~1;
}

static int isMarked(ggc_size_t *obj)
{
    return *obj & 1;
}

static void markFo(struct GGGGC_Freeobj *obj)
{
    obj->selfend = (ggc_size_t*)((ggc_size_t)obj->selfend | 2);
}

static void unmarkFo(struct GGGGC_Freeobj *obj)
{
    obj->selfend = (ggc_size_t*)((ggc_size_t)obj->selfend & ~2);
}

static int isMarkedFo(struct GGGGC_Freeobj *obj)
{
    return (ggc_size_t)obj->selfend & 2;
}

ggc_size_t getFoSize(struct GGGGC_Freeobj *obj)
{
	return (ggc_size_t*)((ggc_size_t)obj->selfend & ~2) - (ggc_size_t *)obj + 1;
}

int forwarded(ggc_size_t *fromRef)
{
	return *fromRef & 4;
}

ggc_size_t *forwardingAddress(ggc_size_t *fromRef)
{
	return (ggc_size_t *)(*fromRef & ~7);
}

static void setForwardingAddress(ggc_size_t *fromRef, ggc_size_t *toRef)
{
	*(ggc_size_t **)fromRef = (ggc_size_t *)((ggc_size_t)toRef | 4);
}

/* if child is forwarded, get the forwarding address */
static ggc_size_t *getCorrectChild(ggc_size_t *child)
{
    if (child == NULL) {
        return NULL;
    }
    else {
        child = (ggc_size_t *)((ggc_size_t)child & ~7);
        if (forwarded(child)) {
            child = forwardingAddress(child);
        }
        return child;
    }
}

void setRememberSet(ggc_size_t *loc)
{
	ggc_size_t offset, rememberSetIndex;
	struct GGGGC_PoolOld *tempPool;

	tempPool = GGGGC_POOLOLD_OF(loc);
	offset = loc - tempPool->start;
	rememberSetIndex = offset/GGGGC_BITS_PER_WORD;
	tempPool->rememberSet[rememberSetIndex] |= ((ggc_size_t)1 << offset%GGGGC_BITS_PER_WORD);

	if (rememberSetIndex > tempPool->maxRememberSetIndex) {
		tempPool->maxRememberSetIndex = rememberSetIndex;
	}
	if (rememberSetIndex < tempPool->minRememberSetIndex) {
		tempPool->minRememberSetIndex = rememberSetIndex;
	}
}

static void clearRememberSet()
{
	struct GGGGC_PoolOld *tempPool;
	for (tempPool = oldHead; tempPool != oldCur->next; tempPool = tempPool->next) {
		memset(tempPool->rememberSet, 0, REMEMBERSET_SIZE*sizeof(ggc_size_t));
	}
}

static void swapB1Pools()
{
	struct GGGGC_Pool *tempPool;

	tempPool = b1FromHead;
    b1FromHead = b1ToHead;
    b1ToHead = tempPool;

    tempPool = b1FromEnd;
    b1FromEnd = b1ToEnd;
    b1ToEnd = tempPool;

    tempPool = b1FromCur;
    b1FromCur = b1ToCur;
    b1ToCur = tempPool;

    /* also swap the generation tag */
    GEN_OF_B1TO = 1 - GEN_OF_B1TO;
    GEN_OF_B1FROM = 1 - GEN_OF_B1FROM;
}

/* reset B0 and B1 fromspace */
static void resetPools()
{
	struct GGGGC_Pool *tempPool;

	for (tempPool = b0Head; tempPool != b0Cur->next; tempPool = tempPool->next) {
		tempPool->free = tempPool->start;
	}
	b0Cur = b0Head;

	for (tempPool = b1FromHead; tempPool != b1FromCur->next; tempPool = tempPool->next) {
		tempPool->free = tempPool->start;
	}
	b1FromCur = b1FromHead;
}

/* functions */
/*******************************************************************************************/
/* ggggc_collect() */

static void pushIfNeedWorklist(ggc_size_t **loc, char needToRemember)
{
    /* save some unnecessary pushes here */
	if (*loc != NULL && (GEN_OF(*loc) != GEN_OF_OLD)) {
        if (GEN_OF(*loc) == GEN_OF_B1TO && !isMarked(*loc)) {
            return;
        }
		if (forwarded(*loc)) {
			ggc_size_t *toRef = forwardingAddress(*loc);
			*loc = toRef;
			if (needToRemember && (GEN_OF(toRef) != GEN_OF_OLD)) {
				setRememberSet((ggc_size_t *)loc);
			}
		}
		else {
			struct GGGGC_Worklist *node = (struct GGGGC_Worklist *)malloc(sizeof(struct GGGGC_Worklist));
		    node->loc = loc;
		    node->needToRemember = needToRemember;
		    node->next = worklist->next;
		    worklist->next = node;
		}
	}
}

static struct GGGGC_Worklist *popWorklist()
{
    if (worklist->next != NULL) {
        struct GGGGC_Worklist *node = worklist->next;
        worklist->next = worklist->next->next;
        return node;
    }
    return NULL;
}

static void freeWorklist()
{
	struct GGGGC_Worklist *node;
	while (worklist) {
		node = worklist;
		worklist = worklist->next;
		free(node);
	}
}

static void initializeWorklist()
{
	ggc_size_t i, j, mask, **loc;
	struct GGGGC_PoolOld *tempPool;
	struct GGGGC_PointerStack *psCur;

	worklist = (struct GGGGC_Worklist *)malloc(sizeof(struct GGGGC_Worklist));
    worklist->next = NULL;

    /* add refs of young gen in remember set */
    for (tempPool = oldHead; tempPool != oldCur->next; tempPool = tempPool->next) {
    	for (i = tempPool->minRememberSetIndex; i <= tempPool->maxRememberSetIndex; i++) {
    		if (tempPool->rememberSet[i] != 0) {
    			mask = 1;
    			for (j = 0; j < GGGGC_BITS_PER_WORD; j++) {
    				if (tempPool->rememberSet[i] & mask) {
    					loc = (ggc_size_t **)(tempPool->start + i*GGGGC_BITS_PER_WORD + j);
    					if (*loc != NULL && (GEN_OF(*loc) != GEN_OF_OLD)) {
    						pushIfNeedWorklist(loc, 0);
    					}
    					else {
                            /* reset wrongly remembered bit */
    						tempPool->rememberSet[i] &= ~mask;
    					}
    				}
    				mask <<= 1;
    			}
    		}
    	}
    }

    /* add refs of young gen in roots */
    for (psCur = ggggc_pointerStack; psCur; psCur = psCur->next) {
        for (i = 0; i < psCur->size; i++) {
        	pushIfNeedWorklist((ggc_size_t **)(psCur->pointers[i]), 0);
        }
    }
}

static void scan(ggc_size_t *obj)
{
	ggc_size_t pWord, pBit, maxWord, maxBit, pCur;
	char needToRemember;
    struct GGGGC_Descriptor *dCur;

    if (GEN_OF(obj) == GEN_OF_OLD) {
    	needToRemember = 1;
    }
    else {
    	needToRemember = 0;
    }
    
    dCur = ((struct GGGGC_Header *)obj)->descriptor__ptr;
    if (dCur->pointers[0] & 1) {
        maxWord = (dCur->size - 1)/GGGGC_BITS_PER_WORD;
        for (pWord = 0; pWord <= maxWord; pWord++) {
            pCur = dCur->pointers[pWord];
            maxBit = (pWord == maxWord)?((dCur->size - 1)%GGGGC_BITS_PER_WORD):(GGGGC_BITS_PER_WORD);
            for (pBit = 0; pBit <= maxBit; pBit++) {
                if (pCur & 1) {
                    pushIfNeedWorklist((ggc_size_t **)(obj + pWord*GGGGC_BITS_PER_WORD + pBit), needToRemember);
                }
                pCur >>= 1;
            }
        }
    }
    else {
        /* even if the object has no refs, still need to process it's first word */
        pushIfNeedWorklist((ggc_size_t **)obj, needToRemember);
    }
}

void ggggc_collect()
{
	ggc_size_t **loc, *fromRef, *toRef;
	struct GGGGC_Worklist *node;
	struct GGGGC_Descriptor *dCur;
    int poolsNeed;

    inCollect = 1;
    lCtB1 = 0;
    swapB1Pools();

    /* re-try young collect start here */
    retry:

	initializeWorklist();
	while (node = popWorklist()) {
		loc = node->loc;
		fromRef = *loc;
        /* need to do unmark job for re-try young collect */
        if (GEN_OF(fromRef) == GEN_OF_B1TO) {
            if (isMarked(fromRef)) {
                /* obj is not scanned */
                unmark(fromRef);
                scan(fromRef);
            }
        }
        else {
            if (forwarded(fromRef)) {
                toRef = forwardingAddress(fromRef);
            }
            else {
                unmark(fromRef);
                dCur = ((struct GGGGC_Header *)fromRef)->descriptor__ptr;
                if (GEN_OF(fromRef) == GEN_OF_B0) {
                    toRef = ggggc_mallocB1(dCur);
                }
                else if (GEN_OF(fromRef) == GEN_OF_B1FROM) {
                    toRef = ggggc_mallocOld(dCur);
                    if (toRef == NULL) {
                        ggggc_collectFull();
                        goto retry;
                    }
                    lCtB1 += dCur->size;
                }
                memcpy(toRef, fromRef, (dCur->size)*sizeof(ggc_size_t));
                setForwardingAddress(fromRef, toRef);
                scan(toRef);
            }
            /* update loc */
            *loc = toRef;
            if (node->needToRemember && (GEN_OF(toRef) != GEN_OF_OLD)) {
                setRememberSet((ggc_size_t *)loc);
            }
        }

		free(node);
	}

    freeWorklist();
    resetPools();
    poolsNeed = (lCtB1 * 3)/GGGGC_WORDS_PER_POOL + 1 - pCtB1;
    ggggc_expandB1(poolsNeed);
    ggggc_expandB0();
    inCollectFull = 0;
    inCollect = 0;
}

/* ggggc_collect() */
/*******************************************************************************************/
/* ggggc_collectFull() */

static void pushWorklistFull(ggc_size_t *obj)
{
	struct GGGGC_WorklistFull *node = (struct GGGGC_WorklistFull *)malloc(sizeof(struct GGGGC_WorklistFull));
    node->obj = obj;
    node->next = worklistFull->next;
    worklistFull->next = node;
}

static ggc_size_t *popWorklistFull()
{
    if (worklistFull->next != NULL) {
        ggc_size_t *ret = worklistFull->next->obj;
        struct GGGGC_WorklistFull *node = worklistFull->next;
        worklistFull->next = worklistFull->next->next;
        free(node);
        return ret;
    }
    return NULL;
}

static void freeWorklistFull()
{
	struct GGGGC_WorklistFull *node;
	while (worklistFull) {
		node = worklistFull;
		worklistFull = worklistFull->next;
		free(node);
	}
}

static void initializeWorklistFull()
{
	ggc_size_t *obj, i;
	struct GGGGC_PointerStack *psCur;

	worklistFull = (struct GGGGC_WorklistFull *)malloc(sizeof(struct GGGGC_WorklistFull));
    worklistFull->next = NULL;

    /* add refs in roots */
    for (psCur = ggggc_pointerStack; psCur; psCur = psCur->next) {
        for (i = 0; i < psCur->size; i++) {
        	obj = getCorrectChild(*(ggc_size_t **)(psCur->pointers[i]));
        	if (obj != NULL && !isMarked(obj)) {
        		mark(obj);
        		pushWorklistFull(obj);
        	}
        }
    }
}

static void scanFull(ggc_size_t *obj)
{
	ggc_size_t pWord, pBit, maxWord, maxBit, pCur, *child;
    struct GGGGC_Descriptor *dCur;

    /* deal with the first word first */
    child = getCorrectChild(*(ggc_size_t **)obj);
    if (child != NULL && !isMarked(child)) {
        mark(child);
        pushWorklistFull(child);
    }
    if (child != NULL && GEN_OF(obj) == GEN_OF_OLD && GEN_OF(child) != GEN_OF_OLD) {
        setRememberSet((ggc_size_t *)(obj));
    }

    dCur = (struct GGGGC_Descriptor *)child;
    /* deal with other refs */
    if (dCur->pointers[0] & 1) {
        maxWord = (dCur->size - 1)/GGGGC_BITS_PER_WORD;
        for (pWord = 0; pWord <= maxWord; pWord++) {
            pCur = dCur->pointers[pWord];
            maxBit = (pWord == maxWord)?((dCur->size - 1)%GGGGC_BITS_PER_WORD):(GGGGC_BITS_PER_WORD);
            for (pBit = 0; pBit <= maxBit; pBit++) {
                if ((pCur & 1) && (pBit != 0 || pWord != 0)) {
                	child = getCorrectChild(*(ggc_size_t **)(obj + pWord*GGGGC_BITS_PER_WORD + pBit));
                	if (child != NULL && !isMarked(child)) {
		        		mark(child);
		        		pushWorklistFull(child);
		        	}
		        	if (child != NULL && GEN_OF(obj) == GEN_OF_OLD && GEN_OF(child) != GEN_OF_OLD) {
		        		setRememberSet((ggc_size_t *)(obj + pWord*GGGGC_BITS_PER_WORD + pBit));
		        	}
                }
                pCur >>= 1;
            }
        }
    }
}

void ggggc_collectFull()
{
	ggc_size_t *obj, *ptr;
	struct GGGGC_PoolOld *tempPool;
	struct GGGGC_Freeobj *endFo, *newFo;
	int poolsNeed;

	inCollectFull = 1;
	lCtOld = 0;
	clearRememberSet();

	/* mark */
	initializeWorklistFull();
	while (obj = popWorklistFull()) {
		scanFull(obj);
	}
	freeWorklistFull();

	/* sweep old gen and build freelist */
	if (!freelist) {
    	freelist = (struct GGGGC_Freeobj *)malloc(sizeof(struct GGGGC_Freeobj));
    	freelist->next = NULL;
    }
    endFo = freelist;
    for (tempPool = oldHead; tempPool != oldCur->next; tempPool = tempPool->next) {
        ptr = tempPool->start;
        while (ptr < tempPool->free) {
            if (isMarked(ptr)) {
                unmark(ptr);
                lCtOld += ((struct GGGGC_Header *)ptr)->descriptor__ptr->size;
                ptr += ((struct GGGGC_Header *)ptr)->descriptor__ptr->size;
            }
            else {
                newFo = (struct GGGGC_Freeobj *)ptr;
                newFo->next = NULL;

                while (!isMarked(ptr) && ptr < tempPool->free) {
                    if (isMarkedFo((struct GGGGC_Freeobj *)ptr)) {
                        unmarkFo((struct GGGGC_Freeobj *)ptr);
                        newFo->selfend = ((struct GGGGC_Freeobj *)ptr)->selfend;
                        ptr = newFo->selfend + 1;
                    }
                    else {
                        ptr += ((struct GGGGC_Header *)ptr)->descriptor__ptr->size;
                        newFo->selfend = ptr - 1;
                    }
                }
                markFo(newFo);
                endFo->next = newFo;
                endFo = newFo;
            }
        }
    }
	
	poolsNeed = (lCtOld * 2)/GGGGC_WORDS_PER_POOL + 1 - pCtOld;
	ggggc_expandOld(poolsNeed);

    if (inCollect) {
        /* if ggggc_collectFull() is called by ggggc_collect(), discard the old young worklist */
        freeWorklist();
    }
    else {
        /* if ggggc_collectFull() is called independently, still need a re-try young collect */
        ggggc_collect();
    }
}

/* ggggc_collectFull() */
/*******************************************************************************************/
/* ggggc_yield() */

int ggggc_yield()
{
    if (freelisthops > 20) {
		ggggc_collectFull();
	}
    return 0;
}

#ifdef __cplusplus
}
#endif
