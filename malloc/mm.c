/*
 * mm.c
 * 
 * This malloc package uses an 'explicit free list' implementation with a LIFO 
 * free list and first-fit placement policy.
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
#define MIN(x, y) ((x) < (y) ? (x) : (y))

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

static void *free_list_start;
static void *heap_listp;

static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void *coalesce(void *bp);
static void expand_alloc_block_right(void *bp, size_t asize, size_t new_free_block_size);
static void add_to_free_list(void *bp);
static void remove_from_free_list(void *bp);
static void print_free_list(void);
static int mm_check(void);

// setup initial free block
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

// place new allocation in an available free block or new heap memory if needed
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

// mm_free is constant time; place new free block at the beginning of the free list
void mm_free(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

// convoluted mess at the moment. uses space from adjacent free blocks if available
void *mm_realloc(void *ptr, size_t size) {
    if (!ptr) {
        return mm_malloc(size);
    }
    if (size == 0) {
        mm_free(ptr);
        return ptr;
    }

    void *new_ptr;
    size_t asize = ALIGN(size + MIN_B_SIZE);
    size_t bsize = GET_SIZE(HDRP(ptr));
    void *prev_block = PREV_BLKP(ptr);
    void *next_block = NEXT_BLKP(ptr);
    size_t prev_bsize = GET_SIZE(HDRP(prev_block));
    size_t next_bsize = GET_SIZE(HDRP(next_block));
    size_t prev_alloc = GET_ALLOC(HDRP(prev_block));
    size_t next_alloc = GET_ALLOC(HDRP(next_block));
    size_t copy_size = MIN(size, bsize);
    size_t free_ptr = 0;

    if (asize <= bsize) { // use existing block
        new_ptr = ptr;
        if (bsize - asize >= MIN_B_SIZE) {
            // block has enough room to split
            PUT(HDRP(new_ptr), PACK(asize, 1));
            PUT(FTRP(new_ptr), PACK(asize, 1));
            void *new_free_block = NEXT_BLKP(new_ptr);
            PUT(HDRP(new_free_block), PACK(bsize - asize, 0));
            PUT(FTRP(new_free_block), PACK(bsize - asize, 0));
            coalesce(new_free_block);
        } else {
            // don't split; new size is close enough to old size; just return
            return new_ptr;
        }
    } else if (!prev_alloc && asize <= prev_bsize + bsize) { // expand existing block into previous block
        if (prev_bsize + bsize - asize >= MIN_B_SIZE) {
            // shift the start of the allocated block to the left
            size_t new_size = prev_bsize + bsize - asize;
            PUT(HDRP(prev_block), PACK(new_size, 0));
            PUT(FTRP(prev_block), PACK(new_size, 0));
            new_ptr = NEXT_BLKP(prev_block);
            PUT(HDRP(new_ptr), PACK(asize, 1));
            PUT(FTRP(new_ptr), PACK(asize, 1));
        } else {
            // shift "all the way left" i.e. replacing the entire previous free block
            remove_from_free_list(prev_block);
            new_ptr = prev_block;
            PUT(HDRP(new_ptr), PACK(prev_bsize + bsize, 1));
            PUT(FTRP(new_ptr), PACK(prev_bsize + bsize, 1));
        }
    } else if (!next_alloc && asize <= next_bsize + bsize) { // expand existing block into next block
        new_ptr = ptr;
        if (next_bsize + bsize - asize >= MIN_B_SIZE) {
            // shift the end of the allocated block to the right
            expand_alloc_block_right(new_ptr, asize, next_bsize + bsize - asize);
        } else {
            // shift "all the way right" i.e. replacing the entire next free block
            remove_from_free_list(next_block);
            PUT(HDRP(new_ptr), PACK(next_bsize + bsize, 1));
            PUT(FTRP(new_ptr), PACK(next_bsize + bsize, 1));
        }
    } else if (!prev_alloc && !next_alloc && asize <= prev_bsize + next_bsize + bsize) { // expand existing block into previous and next blocks
        new_ptr = prev_block;
        if (prev_bsize + next_bsize + bsize - asize >= MIN_B_SIZE) {
            // move the beginning of the allocated block to the previous free block then shift the end of the allocated block right into the next free block
            remove_from_free_list(prev_block);
            expand_alloc_block_right(new_ptr, asize, next_bsize + bsize - asize);
        } else {
            // use the entire 3 block space (prev free block, allocated block, next free block)
            remove_from_free_list(prev_block);
            remove_from_free_list(next_block);
            PUT(HDRP(new_ptr), PACK(prev_bsize + next_bsize + bsize, 1));
            PUT(FTRP(new_ptr), PACK(prev_bsize + next_bsize + bsize, 1));
        }
    } else { // don't have enough space; use malloc to find a new block
        new_ptr = mm_malloc(size);
        free_ptr = 1;
    }

    if (new_ptr != ptr) {
        memmove(new_ptr, ptr, copy_size);
    }

    if (free_ptr) {
        mm_free(ptr);
    }

    return new_ptr;
}

static void *extend_heap(size_t words) {
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

// first-fit policy
static void *find_fit(size_t asize) {
    void *iter = free_list_start;
    while (iter) {
        if (GET_SIZE(HDRP(iter)) >= asize)
            return iter;
        iter = SUCC_BLKP(iter);
    }
    return NULL;
}

static void place(void *bp, size_t asize) {
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

static void *coalesce(void *bp) {
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

static void expand_alloc_block_right(void *bp, size_t asize, size_t new_free_block_size) {
    void *pred_block = PRED_BLKP(NEXT_BLKP(bp));
    void *succ_block = SUCC_BLKP(NEXT_BLKP(bp));
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    void *new_free_block = NEXT_BLKP(bp);
    PUT(HDRP(new_free_block), PACK(new_free_block_size, 0));
    PUT(FTRP(new_free_block), PACK(new_free_block_size, 0));
    PUTP(PRED_P(new_free_block), pred_block);
    PUTP(SUCC_P(new_free_block), succ_block);
    if (succ_block) {
        PUTP(PRED_P(succ_block), new_free_block);
    }
    if (pred_block) {
        PUTP(SUCC_P(pred_block), new_free_block);
    } else {
        free_list_start = new_free_block;
    }
}

static void add_to_free_list(void *bp) {
    PUTP(SUCC_P(bp), free_list_start);
    PUTP(PRED_P(bp), NULL);
    if (free_list_start) {
        PUTP(PRED_P(free_list_start), bp);
    }
    free_list_start = bp;
}

static void remove_from_free_list(void *bp) {
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

// utility function to call in gdb
static void print_free_list() {
    void *start = free_list_start;
    while (start) {
        printf("free block %p %d %d\n", start, GET_SIZE(HDRP(start)), GET_ALLOC(HDRP(start)));
        printf("pred:      %p\n", PRED_BLKP(start));
        printf("succ       %p\n\n", SUCC_BLKP(start));
        start = SUCC_BLKP(start);
    }
}

// check that every block in the free block list is marked as free and that each block only points to other free blocks
static int free_list_blocks_marked_free() {
    void *iter = free_list_start;
    while (iter) {
        if (GET_ALLOC(HDRP(iter)) != 0 || GET_ALLOC(FTRP(iter)) != 0) {
            fprintf(stderr, "block ptr %p in free list is not marked free\n", iter);
            return -1;
        }

        // make sure this free block points to other free blocks
        void *pred = PRED_BLKP(iter);
        void *succ = SUCC_BLKP(iter);
        if (pred && (GET_ALLOC(HDRP(pred)) != 0 || GET_ALLOC(FTRP(pred)) != 0)) {
            fprintf(stderr, "block ptr %p's PRED ptr points to block not marked as free: %p\n", iter, pred);
            return -1;
        }
        if (succ && (GET_ALLOC(HDRP(succ)) != 0 || GET_ALLOC(FTRP(succ)) != 0)) {
            fprintf(stderr, "block ptr %p's SUCC ptr points to block not marked as free: %p\n", iter, succ);
            return -1;
        }

        iter = SUCC_BLKP(iter);
    }
    return 0;
}

// make sure there are no contiguous free blocks
static int contiguous_free_blocks_coalesced() {
    void *iter = NEXT_BLKP(heap_listp);
    while (GET_SIZE(HDRP(iter)) != 0) {
        if (GET_ALLOC(HDRP(iter)) == 0 && GET_ALLOC(HDRP(NEXT_BLKP(iter))) == 0) {
            fprintf(stderr, "block ptrs %p and %p should be coalesced\n", iter, NEXT_BLKP(iter));
            return -1;
        }
        iter = NEXT_BLKP(iter);
    }
    return 0;
}

// verify that bp is in the free block list
static int find_block_in_free_list(void *bp) {
    void *iter = free_list_start;
    while (iter) {
        if (iter == bp) {
            return 0;
        }
        iter = SUCC_BLKP(iter);
    }
    return -1;
}

// verify that all free blocks on the heap are in the free block list
static int all_free_blocks_in_free_list() {
    void *iter = NEXT_BLKP(heap_listp);
    while (GET_SIZE(HDRP(iter)) != 0) {
        if (GET_ALLOC(HDRP(iter)) == 0 && find_block_in_free_list(iter) == -1) {
            fprintf(stderr, "block ptr %p is marked free but is not in free list\n", iter);
            return -1;
        }
        iter = NEXT_BLKP(iter);
    }
    return 0;
}

// verify that every block is completely within heap address range
static int check_ptrs_valid_heap_address() {
    void *heap_lo = mem_heap_lo();
    void *heap_hi = mem_heap_hi();

    void *iter = heap_listp;
    while (GET_SIZE(HDRP(iter)) != 0) {
        if ((void *)HDRP(iter) < heap_lo || (void *)HDRP(iter) > heap_hi || (void *)FTRP(iter) < heap_lo || (void *)FTRP(iter) > heap_hi) {
            fprintf(stderr, "block at ptr %p is not fully within heap bounds\n", iter);
            return -1;
        }
        iter = NEXT_BLKP(iter);
    }
    return 0;
}

static int mm_check() {
    if (free_list_blocks_marked_free() != 0) return -1;
    if (contiguous_free_blocks_coalesced() != 0) return -1;
    if (all_free_blocks_in_free_list() != 0) return -1;
    if (check_ptrs_valid_heap_address() != 0) return -1;
    return 0;
}
