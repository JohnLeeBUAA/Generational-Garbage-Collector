/* this test is basically a badlll with garbage */

#include <stdio.h>
#include <stdlib.h>
#include "ggggc/gc.h"

GGC_TYPE(TWO)
    GGC_MPTR(TWO, next);
    GGC_MDATA(int, val);
GGC_END_TYPE(TWO,
    GGC_PTR(TWO, next)
    );

int main(void)
{
    int i, j, last, cur;
    TWO head = NULL, end = NULL, node = NULL;
    
    GGC_PUSH_3(node, end, head);

    head = GGC_NEW(TWO);
    GGC_WD(head, val, 0);
    end = head;

    /* create list */
    for (i = 1; i < 4096; i++) {
        
        /* create garbage */
        for (j = 0; j < 4096; j++) {
            node = GGC_NEW(TWO);
        }
        
        node = GGC_NEW(TWO);
        GGC_WD(node, val, i);
        GGC_WP(end, next, node);
        end = node;
    }

    /* check list */
    last = GGC_RD(head, val);
    node = GGC_RP(head, next);
    while (node) {
        cur = GGC_RD(node, val);
        if (cur == (last + 1)) {
            last = cur;
            node = GGC_RP(node, next);
        }
        else {
            printf("WRONG\n");
            exit(1);
        }
    }
    
    return 0;
}
