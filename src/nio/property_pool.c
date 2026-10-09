#include "nio/property_pool.h"

#include <stdlib.h>

#include "nio/pool.h"

// graphvex R3 — nio/property_pool.c
// A thin typed face over nio/pool: stride = sizeof(Property). Blocks are 64
// slots and double as they fill (no ceiling).

#define PROPERTY_POOL_BLOCK_CAP 64u

struct PropertyPool {
    Pool *slots;
};

// Creates a typed pool for stable Property records.
PropertyPool *PropertyPool_0(void) {
    PropertyPool *pp = calloc(1, sizeof *pp);
    if (!pp) return nullptr;
    (*pp).slots = Pool_new(sizeof(Property), PROPERTY_POOL_BLOCK_CAP);
    if (!(*pp).slots) { free(pp); return nullptr; }
    return pp;
}

// The process-global pool Element-owned bounds come from (like memory's default
// arena). Borrowed, never destroyed by callers.
static PropertyPool *s_default = nullptr;
// Returns the process-global lazily created pool; callers borrow it and must not destroy it.
PropertyPool *PropertyPool_default(void) {
    if (!s_default) s_default = PropertyPool_0();
    return s_default;
}

// Destroys the pool and all of its backing storage; do not use on the shared default pool.
void PropertyPool_destroy(PropertyPool *pool) {
    if (!pool) return;
    Pool_destroy((*pool).slots);
    free(pool);
}

// Allocates a stable property slot and copies init, or the default property when init is null.
Property *PropertyPool_alloc(PropertyPool *pool, const Property *init) {
    if (!pool) return nullptr;
    Property *p = Pool_alloc((*pool).slots);
    if (!p) return nullptr;
    *p = init ? *init : Property_default();
    return p;
}

// Returns a property slot to its pool; the pointer must belong to that pool.
void PropertyPool_release(PropertyPool *pool, Property *property) {
    if (pool && property) Pool_release((*pool).slots, property);
}

// Returns the number of outstanding property slots, or zero for null.
uint32_t PropertyPool_live(const PropertyPool *pool) {
    return pool ? Pool_live((*pool).slots) : 0u;
}
