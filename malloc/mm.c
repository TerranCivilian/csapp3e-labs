/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

#define WSIZE      4
#define DSIZE      8
#define CHUNKSIZE (1<<12)

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))

#define GET(p)       (*(unsigned int *)(p))
#define PUT(p, val)  (*(unsigned int *)(p) = val)

#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp)  ((char *)(bp) - WSIZE)
#define FTRP(bp)  ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Next + previous block pointers of contiguous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

#define SUCC_P(bp)  (((char *)(bp) + WSIZE))
#define PRED_P(bp)  (bp)

#define GETP(p)      (*(char **)(p))
#define PUTP(p, val) (*(char **)(p) = val)

/* Successor + predecessor block pointers of free block list */
#define SUCC_BLKP(bp)  (GETP(SUCC_P(bp)))
#define PRED_BLKP(bp)  (GETP(PRED_P(bp)))

/* four word minimum block size */
#define MIN_B_SIZE 16
/* double word (8) alignment */
#define ALIGNMENT 8
/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))



team_t team = {
    "ateam",
    "Harry Bovik",
    "bovik@cs.cmu.edu",
    "",
    ""
};




static void *free_list_start;
static void *heap_listp;

void *find_fit(size_t asize);
void add_to_free_list(void *bp);
void remove_from_free_list(void *bp);
void *extend_heap(size_t words);
void place(void *bp, size_t asize);
int mm_check(void);
void print_free_list(void);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
    free_list_start = NULL;

    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0);
    PUT((char *)heap_listp + (1*WSIZE), PACK(DSIZE, 1));
    PUT((char *)heap_listp + (2*WSIZE), PACK(DSIZE, 1));
    PUT((char *)heap_listp + (3*WSIZE), PACK(0, 1));
    heap_listp = (char *)heap_listp + (2*WSIZE);

    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size) {
    if (size == 0)
        return NULL;

    size_t asize = ALIGN(size + MIN_B_SIZE);

    char *bp;
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    size_t extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(bp, asize);

    return bp;
}

void *find_fit(size_t asize) {
    void *iter = free_list_start;
    while (iter) {
        if (GET_SIZE(HDRP(iter)) >= asize)
            return iter;
        iter = SUCC_BLKP(iter);
    }
    return NULL;
}

void place(void *bp, size_t asize) {
    size_t bsize = GET_SIZE(HDRP(bp));
    if (bsize - asize >= MIN_B_SIZE) { // split
        void *prev_free_block = PRED_BLKP(bp);
        void *next_free_block = SUCC_BLKP(bp);

        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));

        size_t new_bsize = bsize - asize;
        void *new_bp = NEXT_BLKP(bp);
        PUT(HDRP(new_bp), PACK(new_bsize, 0));
        PUT(FTRP(new_bp), PACK(new_bsize, 0));
        PUTP(PRED_P(new_bp), prev_free_block);
        PUTP(SUCC_P(new_bp), next_free_block);
        if (next_free_block) {
            PUTP(PRED_P(next_free_block), new_bp);
        }

        if (prev_free_block) {
            PUTP(SUCC_P(prev_free_block), new_bp);
        } else {
            free_list_start = new_bp;
        }
    } else { // use whole block
        remove_from_free_list(bp);
        PUT(HDRP(bp), PACK(bsize, 1));
        PUT(FTRP(bp), PACK(bsize, 1));
    }
}

void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        add_to_free_list(bp);
        return bp;
    } else if (prev_alloc && !next_alloc) {
        // remove next contiguous block from free list
        remove_from_free_list(NEXT_BLKP(bp));

        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {
        // remove previous contiguous block from free list
        remove_from_free_list(PREV_BLKP(bp));

        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    } else {
        // remove next contiguous block from free list
        remove_from_free_list(NEXT_BLKP(bp));

        // remove previous contiguous block from free list
        remove_from_free_list(PREV_BLKP(bp));

        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    add_to_free_list(bp);

    return bp;
}

void remove_from_free_list(void *bp) {
    void *next_free_bp = SUCC_BLKP(bp);
    void *prev_free_bp = PRED_BLKP(bp);
    if (prev_free_bp) {
        PUTP(SUCC_P(prev_free_bp), next_free_bp);
    }
    if (next_free_bp) {
        PUTP(PRED_P(next_free_bp), prev_free_bp);
        if (prev_free_bp == NULL) {
            free_list_start = next_free_bp;
        }
    }
    if (!prev_free_bp && !next_free_bp) {
        free_list_start = NULL;
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

void add_to_free_list(void *bp) {
    PUTP(SUCC_P(bp), free_list_start);
    PUTP(PRED_P(bp), NULL);
    if (free_list_start) {
        PUTP(PRED_P(free_list_start), bp);
    }
    free_list_start = bp;
}

void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    bp = coalesce(bp);
    return bp;
}

// utility function to call in gdb
void print_free_list() {
    void *start = free_list_start;
    while (start) {
        printf("free block %p %d %d\n", start, GET_SIZE(HDRP(start)), GET_ALLOC(HDRP(start)));
        printf("pred:      %p\n", PRED_BLKP(start));
        printf("succ       %p\n\n", SUCC_BLKP(start));
        start = SUCC_BLKP(start);
    }
}
