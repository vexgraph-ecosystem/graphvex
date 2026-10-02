#ifndef GRAPHICS_NIO_POOL_H
#define GRAPHICS_NIO_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// graphvex R3 — nio/pool.h
//
// A growable fixed-stride slab pool. Two properties make it the right home for
// the placement vocabulary:
//
//   1. STABLE ADDRESSES. Growth APPENDS a block; a live item never moves, so an
//      Element may hold a borrowed pointer for the pool's whole life and share
//      it with other Elements (aliasing = the bind).
//   2. FOREIGN MEMORY. Blocks come from vexspoke's ForeignMemory (Memory_alloc),
//      not the C heap, so the pool is one arena teardown.
//
// No ceiling: a full block just adds another (the No Hardcoding Law).
typedef struct Pool Pool;

Pool *Pool_new(size_t stride, uint32_t blockCap);
void Pool_destroy(Pool *pool);

void *Pool_alloc(Pool *pool);              // a zeroed slot, or NULL on OOM
void Pool_release(Pool *pool, void *item); // return a slot to the free list
bool Pool_contains(const Pool *pool, const void *item);   // does the slot belong here?

uint32_t Pool_live(const Pool *pool);
size_t Pool_stride(const Pool *pool);

#endif // GRAPHICS_NIO_POOL_H
