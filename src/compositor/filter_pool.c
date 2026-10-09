#include "compositor/filter_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "exception/throw.h"

;;DEFINITION
/**
 * FilterPool stores complex ordered recipes as immutable inline-token records.
 * Tokens carry pool-local index/generation, not native pointers. Refcounts keep
 * records alive for CPU submission; externally synchronized synchronous compose
 * snapshots them before invoking the CPU compositor. Retire at generation
 * saturation: no old token ever becomes valid again within this pool lifetime.
 * Fixed cold allocations are bounded prototype storage, not arena/GPU lifetime
 * or zero-allocation compositor claims. No nested pooled recipes are admitted.
 */
;;OVERVIEW
/**
 * CLASS: FilterPool. Fields: capacity, maxRecipeFilters, live, slots.
 * Private Slot record: references, generation, count, retired, tokens[256].
 * Public: _2 constructor/macro, destroy, insert, retain, release, snapshot,
 * compose, capacity/maxRecipeFilters/live queries, toString/toStringStruct.
 * Private: lookup checks index/generation; reject reports cold errors once;
 * format projects bounded strings. No mutable recipe view is exported.
 */
typedef struct Slot {
    uint32_t references;
    uint16_t generation;
    size_t count;
    bool retired;
    FilterToken tokens[COMPOSITOR_MAX_FILTERS];
} Slot;

_Static_assert(FILTER_POOL_MAX_CAPACITY <= SIZE_MAX / sizeof(Slot),
               "configured slot budget must fit allocation size");

struct FilterPool {
    uint32_t capacity;
    uint32_t maxRecipeFilters;
    uint32_t live;
    Slot *slots;
};

// Reports a rejected cold operation and returns its status unchanged.
static FilterPoolStatus reject(FilterPoolStatus status) {
    if (status != FILTER_POOL_OK)
        THROW("FilterPool rejected operation: status=%d", (int) status);
    return status;
}

// Resolves a live pool token after validating pool identity, slot index, and generation.
static FilterPoolStatus lookup(const FilterPool *pool, FilterToken token,
                               Slot **out) {
    if (!pool || Filter_id(token) != FILTER_POOL_ID)
        return FILTER_POOL_INVALID;
    uint64_t payload = Filter_payload(token);
    uint32_t index = (uint32_t) payload;
    uint16_t generation = (uint16_t) (payload >> 32);
    if (!generation)
        return FILTER_POOL_INVALID;
    if (index >= (*pool).capacity)
        return FILTER_POOL_STALE;
    Slot *slot = &(*pool).slots[index];
    if (!(*slot).references || (*slot).generation != generation)
        return FILTER_POOL_STALE;
    *out = slot;
    return FILTER_POOL_OK;
}

// Creates bounded recipe storage and initializes each slot's first generation.
FilterPoolStatus FilterPool_2(FilterPoolConfig config, FilterPool **out) {
    if (!out || !config.capacity || !config.maxRecipeFilters)
        return reject(FILTER_POOL_INVALID);
    if (config.capacity > FILTER_POOL_MAX_CAPACITY ||
        config.maxRecipeFilters > COMPOSITOR_MAX_FILTERS)
        return reject(FILTER_POOL_LIMIT);
    FilterPool *pool = calloc(1, sizeof *pool);
    if (!pool)
        return reject(FILTER_POOL_NO_MEMORY);
    Slot *slots = calloc(config.capacity, sizeof *slots);
    if (!slots) {
        free(pool);
        return reject(FILTER_POOL_NO_MEMORY);
    }
    (*pool).capacity = config.capacity;
    (*pool).maxRecipeFilters = config.maxRecipeFilters;
    (*pool).slots = slots;
    for (uint32_t i = 0; i < config.capacity; ++i)
        slots[i].generation = 1;
    *out = pool;
    return FILTER_POOL_OK;
}

// Destroys an empty pool; returns busy and preserves it while recipes remain live.
FilterPoolStatus FilterPool_destroy(FilterPool *pool) {
    if (!pool)
        return FILTER_POOL_OK;
    if ((*pool).live)
        return reject(FILTER_POOL_BUSY);
    free((*pool).slots);
    free(pool);
    return FILTER_POOL_OK;
}

// Validates and copies an ordered non-nested recipe into an available slot.
FilterPoolStatus FilterPool_insert(FilterPool *pool, const FilterToken *tokens,
                                  size_t count, FilterToken *out) {
    if (!pool || !out || (count && !tokens))
        return reject(FILTER_POOL_INVALID);
    if (count > (*pool).maxRecipeFilters)
        return reject(FILTER_POOL_LIMIT);
    for (size_t i = 0; i < count; ++i)
        if (Filter_id(tokens[i]) == FILTER_POOL_ID)
            return reject(FILTER_POOL_UNSUPPORTED);
    CompositorBounds unused;
    CompositorStatus status = Compositor_filterBounds((CompositorBounds){0},
                                                     tokens, count, &unused);
    if (status != COMPOSITOR_OK)
        return reject((FilterPoolStatus) status);
    for (uint32_t i = 0; i < (*pool).capacity; ++i) {
        Slot *slot = &(*pool).slots[i];
        if ((*slot).references || (*slot).retired)
            continue;
        if (count)
            memcpy((*slot).tokens, tokens, count * sizeof *tokens);
        (*slot).count = count;
        (*slot).references = 1;
        ++(*pool).live;
        *out = ((uint64_t) FILTER_POOL_ID << 48) |
               ((uint64_t) (*slot).generation << 32) | i;
        return FILTER_POOL_OK;
    }
    return reject(FILTER_POOL_LIMIT);
}

// Adds one reference to a live recipe unless its reference count is saturated.
FilterPoolStatus FilterPool_retain(FilterPool *pool, FilterToken token) {
    Slot *slot;
    FilterPoolStatus status = lookup(pool, token, &slot);
    if (status != FILTER_POOL_OK)
        return reject(status);
    if ((*slot).references == UINT32_MAX)
        return reject(FILTER_POOL_LIMIT);
    ++(*slot).references;
    return FILTER_POOL_OK;
}

// Drops one recipe reference and advances or retires its slot generation at zero.
FilterPoolStatus FilterPool_release(FilterPool *pool, FilterToken token) {
    Slot *slot;
    FilterPoolStatus status = lookup(pool, token, &slot);
    if (status != FILTER_POOL_OK)
        return reject(status);
    if (--(*slot).references)
        return FILTER_POOL_OK;
    --(*pool).live;
    (*slot).count = 0;
    if ((*slot).generation == UINT16_MAX)
        (*slot).retired = true;
    else
        ++(*slot).generation;
    return FILTER_POOL_OK;
}

// Copies a live recipe into caller storage, leaving output count untouched on rejection.
FilterPoolStatus FilterPool_snapshot(const FilterPool *pool, FilterToken token,
                                    FilterToken *outTokens, size_t capacity,
                                    size_t *outCount) {
    Slot *slot;
    FilterPoolStatus status = lookup(pool, token, &slot);
    if (status != FILTER_POOL_OK)
        return reject(status);
    if (!outCount || ((*slot).count && !outTokens))
        return reject(FILTER_POOL_INVALID);
    if (capacity < (*slot).count)
        return reject(FILTER_POOL_LIMIT);
    if ((*slot).count)
        memcpy(outTokens, (*slot).tokens, (*slot).count * sizeof *outTokens);
    *outCount = (*slot).count;
    return FILTER_POOL_OK;
}

// Expands pool tokens in order and composes the supplied source group with that recipe.
FilterPoolStatus FilterPool_compose(const FilterPool *pool,
                                   const CompositorSurface *const *sources,
                                   size_t sourceCount, const FilterToken *tokens,
                                   size_t tokenCount, CompositorSurface **out) {
    if (!pool || !out || (tokenCount && !tokens) || (sourceCount && !sources))
        return reject(FILTER_POOL_INVALID);
    if (tokenCount > COMPOSITOR_MAX_FILTERS || sourceCount > COMPOSITOR_MAX_SOURCES)
        return reject(FILTER_POOL_LIMIT);
    FilterToken expanded[COMPOSITOR_MAX_FILTERS];
    size_t count = 0;
    for (size_t i = 0; i < tokenCount; ++i) {
        if (Filter_id(tokens[i]) != FILTER_POOL_ID) {
            if (count == COMPOSITOR_MAX_FILTERS)
                return reject(FILTER_POOL_LIMIT);
            expanded[count++] = tokens[i];
            continue;
        }
        Slot *slot;
        FilterPoolStatus status = lookup(pool, tokens[i], &slot);
        if (status != FILTER_POOL_OK)
            return reject(status);
        if ((*slot).count > COMPOSITOR_MAX_FILTERS - count)
            return reject(FILTER_POOL_LIMIT);
        if ((*slot).count)
            memcpy(expanded + count, (*slot).tokens, (*slot).count * sizeof *expanded);
        count += (*slot).count;
    }
    return reject((FilterPoolStatus) Compositor_compose(sources, sourceCount, expanded, count, out));
}

// Returns configured slot capacity, or zero for null.
uint32_t FilterPool_capacity(const FilterPool *pool) {
    return pool ? (*pool).capacity : 0;
}
// Returns the per-recipe filter limit, or zero for null.
uint32_t FilterPool_maxRecipeFilters(const FilterPool *pool) {
    return pool ? (*pool).maxRecipeFilters : 0;
}
// Returns the number of occupied recipe slots, or zero for null.
uint32_t FilterPool_live(const FilterPool *pool) {
    return pool ? (*pool).live : 0;
}

// Writes the selected bounded pool projection and reports truncation.
static void format(const FilterPool *pool, bool structure, char *dest, size_t cap,
                   bool *outTruncated) {
    if (!dest && cap) {
        if (outTruncated)
            *outTruncated = true;
        reject(FILTER_POOL_INVALID);
        return;
    }
    int length;
    if (!pool)
        length = snprintf(dest, cap, "nullptr");
    else if (structure)
        length = snprintf(dest, cap,
            "FilterPool{capacity=%u,maxRecipeFilters=%u,live=%u,slots=%p}",
            (*pool).capacity, (*pool).maxRecipeFilters, (*pool).live,
            (void*) (*pool).slots);
    else
        length = snprintf(dest, cap, "FilterPool(live=%u,capacity=%u)",
                          (*pool).live, (*pool).capacity);
    if (outTruncated)
        *outTruncated = length < 0 || (size_t) length >= cap;
}
// Formats the pool's concise value summary into caller storage.
void FilterPool_toString(const FilterPool *pool, char *dest, size_t cap,
                         bool *outTruncated) {
    format(pool, false, dest, cap, outTruncated);
}
// Formats the pool's own fields into caller storage.
void FilterPool_toStringStruct(const FilterPool *pool, char *dest, size_t cap,
                               bool *outTruncated) {
    format(pool, true, dest, cap, outTruncated);
}
