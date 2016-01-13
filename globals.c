#include "ggggc-internals.h"

/* publics */
struct GGGGC_PointerStack *ggggc_pointerStack, *ggggc_pointerStackGlobals;

/* internals */
char inCollect;
char inCollectFull;
char mustAllocPool;
char skipFreelist;
ggc_size_t freelisthops;
ggc_size_t GEN_OF_B0;
ggc_size_t GEN_OF_B1TO;
ggc_size_t GEN_OF_B1FROM;
ggc_size_t GEN_OF_OLD;
ggc_size_t pCtB0;
ggc_size_t pCtB1;
ggc_size_t pCtOld;
ggc_size_t lCtB1;
ggc_size_t lCtOld;
struct GGGGC_Worklist *worklist;
struct GGGGC_WorklistFull *worklistFull;
struct GGGGC_Freeobj *freelist;
struct GGGGC_Pool *b0Head;
struct GGGGC_Pool *b0End;
struct GGGGC_Pool *b0Cur;
struct GGGGC_Pool *b1ToHead;
struct GGGGC_Pool *b1ToEnd;
struct GGGGC_Pool *b1ToCur;
struct GGGGC_Pool *b1FromHead;
struct GGGGC_Pool *b1FromEnd;
struct GGGGC_Pool *b1FromCur;
struct GGGGC_PoolOld *oldHead;
struct GGGGC_PoolOld *oldEnd;
struct GGGGC_PoolOld *oldCur;
struct GGGGC_Descriptor *ggggc_descriptorDescriptors[GGGGC_WORDS_PER_POOL/GGGGC_BITS_PER_WORD+sizeof(struct GGGGC_Descriptor)];
