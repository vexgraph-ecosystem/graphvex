#ifndef GRAPHVEX_FILTER_POOL_H
#define GRAPHVEX_FILTER_POOL_H

#include <stdbool.h>
#include "compositor/compositor.h"

/* Pool-local operation: ID16 | generation16 | index32. No pointer payload.
 * Tokens are scoped to their originating live pool; this format cannot detect
 * a token from another pool with coincident index/generation. Never transfer
 * tokens between pools or use tokens after pool destruction. */
// FILTER_POOL_ID is defined once in the canonical lang/filter.h operation table.
#define FILTER_POOL_MAX_CAPACITY 4096u
typedef struct FilterPool FilterPool;
typedef struct FilterPoolConfig {
    uint32_t capacity;
    uint32_t maxRecipeFilters;
} FilterPoolConfig;

typedef enum FilterPoolStatus {
    FILTER_POOL_OK = COMPOSITOR_OK,
    FILTER_POOL_INVALID = COMPOSITOR_INVALID,
    FILTER_POOL_UNSUPPORTED = COMPOSITOR_UNSUPPORTED,
    FILTER_POOL_LIMIT = COMPOSITOR_LIMIT,
    FILTER_POOL_NO_MEMORY = COMPOSITOR_NO_MEMORY,
    FILTER_POOL_STALE,
    FILTER_POOL_BUSY
} FilterPoolStatus;

/* Cold bounded prototype: 1..4096 slots, 1..COMPOSITOR_MAX_FILTERS tokens per
 * recipe. Constructor preallocates fixed slot storage; insert copies immutable
 * ordered inline recipes, including empty recipes. Nested pool tokens are
 * UNSUPPORTED, never recursively resolved. Only current CPU operations accepted.
 * Successful insert owns one reference; copied tokens require explicit retain.
 * Final release invalidates that generation. Saturated-generation slots retire
 * permanently rather than wrapping. Refcount overflow returns LIMIT unchanged.
 * Destroy rejects outstanding references with BUSY; NULL destroy succeeds.
 * All calls require live pointers and external synchronization (no thread/GPU
 * safety claim). Failed calls preserve outputs and pool/refcount state.
 * Cold rejection emits one THROW diagnostic. Compose is a synchronous cold
 * reference operation: it copies recipes to a bounded stack snapshot, then invokes
 * Compositor_compose; no heap allocation in expansion, CPU compose may allocate.
 * No recipe borrows escape a call and no GPU retirement guarantee is implied.
 * Caps are initial prototype budgets, not hardware or UI semantic limits.
 */
FilterPoolStatus FilterPool_2(FilterPoolConfig config, FilterPool **out);
#define FilterPool(config, out) FilterPool_2(config, out)
FilterPoolStatus FilterPool_destroy(FilterPool *pool);
FilterPoolStatus FilterPool_insert(FilterPool *pool, const FilterToken *tokens,
                                  size_t count, FilterToken *out);
FilterPoolStatus FilterPool_retain(FilterPool *pool, FilterToken token);
FilterPoolStatus FilterPool_release(FilterPool *pool, FilterToken token);
/* Copy-only snapshot. count may be zero; outTokens may then be NULL. Both
 * outputs stay unchanged on failure, including insufficient capacity. */
FilterPoolStatus FilterPool_snapshot(const FilterPool *pool, FilterToken token,
                                    FilterToken *outTokens, size_t capacity,
                                    size_t *outCount);
FilterPoolStatus FilterPool_compose(const FilterPool *pool,
                                   const CompositorSurface *const *sources,
                                   size_t sourceCount, const FilterToken *tokens,
                                   size_t tokenCount, CompositorSurface **out);
uint32_t FilterPool_capacity(const FilterPool *pool);
uint32_t FilterPool_maxRecipeFilters(const FilterPool *pool);
uint32_t FilterPool_live(const FilterPool *pool);
/* Bounded cold strings; NULL pool renders "nullptr". NULL destination is
 * accepted only with cap=0; truncation flag is optional. No allocations. */
void FilterPool_toString(const FilterPool *pool, char *dest, size_t cap,
                         bool *outTruncated);
void FilterPool_toStringStruct(const FilterPool *pool, char *dest, size_t cap,
                               bool *outTruncated);

#endif
