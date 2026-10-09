#include "nio/pool.h"

#include <stdlib.h>
#include <string.h>

#include "nio/mem.h"   // vexspoke R2 — ForeignMemory (Memory_alloc / Memory_free)

// graphvex R3 — nio/pool.c
// Blocks are cut from ForeignMemory and APPENDED, never moved, so a borrowed
// slot pointer stays valid for the pool's life. A free slot is threaded with
// the address of the next free slot (every slot is >= sizeof(void*)).

struct Pool {
    size_t stride;
    uint32_t blockCap;          // slots per block
    void **blocks;              // every ForeignMemory block, for teardown
    int blockCount, blocksCap;
    void *freeHead;             // intrusive free list over the slots
    uint32_t live;
};

// Appends a backing block pointer, growing the block directory when required.
static bool blocks_push(Pool *p, void *block) {
    if ((*p).blockCount == (*p).blocksCap) {
        int cap = (*p).blocksCap ? (*p).blocksCap * 2 : 4;
        void **grown = realloc((*p).blocks, (size_t)cap * sizeof *grown);
        if (!grown) return false;
        (*p).blocks = grown;
        (*p).blocksCap = cap;
    }
    (*p).blocks[(*p).blockCount++] = block;
    return true;
}

// Allocates a block of fixed-stride slots and links them onto the intrusive free list.
static bool pool_grow(Pool *p) {
    size_t Bytes = (*p).stride * (size_t)(*p).blockCap;
    uint8_t *mem = Memory_alloc(0, Bytes);
    if (!mem) return false;
    if (!blocks_push(p, mem)) { Memory_free(mem); return false; }
    for (uint32_t i = 0; i < (*p).blockCap; i++) {
        void *slot = mem + (size_t)i * (*p).stride;
        *(void **)slot = (*p).freeHead;
        (*p).freeHead = slot;
    }
    return true;
}

// Creates a pool whose slots have the given stride and per-block capacity.
Pool *Pool_new(size_t stride, uint32_t blockCap) {
    if (stride < sizeof(void *) || blockCap == 0) return nullptr;
    Pool *p = calloc(1, sizeof *p);
    if (!p) return nullptr;
    (*p).stride = stride;
    (*p).blockCap = blockCap;
    return p;
}

// Returns every ForeignMemory block and releases the pool metadata.
void Pool_destroy(Pool *pool) {
    if (!pool) return;
    for (int i = 0; i < (*pool).blockCount; i++) Memory_free((*pool).blocks[i]);
    free((*pool).blocks);
    free(pool);
}

// Acquires and zeroes one slot, growing the pool when its free list is empty.
void *Pool_alloc(Pool *pool) {
    if (!pool) return nullptr;
    if (!(*pool).freeHead && !pool_grow(pool)) return nullptr;
    void *item = (*pool).freeHead;
    (*pool).freeHead = *(void **)item;
    memset(item, 0, (*pool).stride);
    (*pool).live++;
    return item;
}

// Returns a slot to the free list; caller must supply a live slot from this pool.
void Pool_release(Pool *pool, void *item) {
    if (!pool || !item) return;
    *(void **)item = (*pool).freeHead;
    (*pool).freeHead = item;
    (*pool).live--;
}

// Checks whether an address lies within any allocated slot block (not whether it is slot-aligned).
bool Pool_contains(const Pool *pool, const void *item) {
    if (!pool || !item) return false;
    const uint8_t *p = item;
    for (int i = 0; i < (*pool).blockCount; i++) {
        const uint8_t *base = (*pool).blocks[i];
        if (p >= base && p < base + (*pool).stride * (size_t)(*pool).blockCap) return true;
    }
    return false;
}

// Returns the number of currently allocated slots, or zero for null.
uint32_t Pool_live(const Pool *pool) { return pool ? (*pool).live : 0u; }
// Returns the configured slot stride in bytes, or zero for null.
size_t Pool_stride(const Pool *pool) { return pool ? (*pool).stride : 0u; }
