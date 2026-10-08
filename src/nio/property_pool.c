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
PropertyPool *PropertyPool_default(void) {
    if (!s_default) s_default = PropertyPool_0();
    return s_default;
}

void PropertyPool_destroy(PropertyPool *pool) {
    if (!pool) return;
    Pool_destroy((*pool).slots);
    free(pool);
}

Property *PropertyPool_alloc(PropertyPool *pool, const Property *init) {
    if (!pool) return nullptr;
    Property *p = Pool_alloc((*pool).slots);
    if (!p) return nullptr;
    *p = init ? *init : Property_default();
    return p;
}

void PropertyPool_release(PropertyPool *pool, Property *property) {
    if (pool && property) Pool_release((*pool).slots, property);
}

uint32_t PropertyPool_live(const PropertyPool *pool) {
    return pool ? Pool_live((*pool).slots) : 0u;
}
