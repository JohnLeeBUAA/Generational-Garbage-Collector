/*
 * Header for internal GGGGC functions
 *
 * Copyright (c) 2014 Gregor Richards
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

#ifndef GGGGC_INTERNALS_H
#define GGGGC_INTERNALS_H 1

#include "ggggc/gc.h"

#ifdef __cplusplus
extern "C" {
#endif

void ggggc_expandB0(void);
void ggggc_expandB1(int poolsNeed);
void ggggc_expandOld(int poolsNeed);
void *ggggc_mallocB1(struct GGGGC_Descriptor *descriptor);
void *ggggc_mallocOld(struct GGGGC_Descriptor *descriptor);

ggc_size_t getFoSize(struct GGGGC_Freeobj *obj);
int forwarded(ggc_size_t *fromRef);
ggc_size_t *forwardingAddress(ggc_size_t *fromRef);

extern char inCollect;
extern char inCollectFull;
extern char mustAllocPool;
extern char skipFreelist;
extern ggc_size_t freelisthops;
extern ggc_size_t GEN_OF_B0;
extern ggc_size_t GEN_OF_B1TO;
extern ggc_size_t GEN_OF_B1FROM;
extern ggc_size_t GEN_OF_OLD;
extern ggc_size_t pCtB0;
extern ggc_size_t pCtB1;
extern ggc_size_t pCtOld;
extern ggc_size_t lCtB1;
extern ggc_size_t lCtOld;
extern struct GGGGC_Worklist *worklist;
extern struct GGGGC_WorklistFull *worklistFull;
extern struct GGGGC_Freeobj *freelist;
extern struct GGGGC_Pool *b0Head;
extern struct GGGGC_Pool *b0End;
extern struct GGGGC_Pool *b0Cur;
extern struct GGGGC_Pool *b1ToHead;
extern struct GGGGC_Pool *b1ToEnd;
extern struct GGGGC_Pool *b1ToCur;
extern struct GGGGC_Pool *b1FromHead;
extern struct GGGGC_Pool *b1FromEnd;
extern struct GGGGC_Pool *b1FromCur;
extern struct GGGGC_PoolOld *oldHead;
extern struct GGGGC_PoolOld *oldEnd;
extern struct GGGGC_PoolOld *oldCur;
extern struct GGGGC_Descriptor *ggggc_descriptorDescriptors[GGGGC_WORDS_PER_POOL/GGGGC_BITS_PER_WORD+sizeof(struct GGGGC_Descriptor)];

#ifdef __cplusplus
}
#endif

#endif
