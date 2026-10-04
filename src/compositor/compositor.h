#ifndef GRAPHVEX_COMPOSITOR_H
#define GRAPHVEX_COMPOSITOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "lang/filter.h"

typedef enum CompositorStatus {
    COMPOSITOR_OK = 0,
    COMPOSITOR_INVALID,
    COMPOSITOR_UNSUPPORTED,
    COMPOSITOR_LIMIT,
    COMPOSITOR_NO_MEMORY
} CompositorStatus;

typedef struct CompositorBounds {
    int32_t x, y;
    uint32_t width, height;
} CompositorBounds;

typedef struct CompositorSurface CompositorSurface;
#define COMPOSITOR_MAX_PIXELS UINT32_C(16777216)
#define COMPOSITOR_MAX_SOURCES 256u
#define COMPOSITOR_MAX_FILTERS 256u
#define COMPOSITOR_MAX_SCATTER_TAPS UINT64_C(67108864)

/* CPU reference backend only; no GPU submission is implied.
 * Surfaces own tightly packed linear-light premultiplied float RGBA. Their
 * half-open integer bounds have an origin independent of allocation. Empty
 * surfaces are represented by width=height=0 (not just one zero dimension).
 * Pixels: finite nonnegative RGB (HDR allowed), alpha in [0,1], and zero RGB
 * when alpha is zero. No normalization to RGB<=alpha: gain can produce HDR.
 * All calls require external synchronization and live pointers. No retained
 * borrows. Mutable pixel access is for constructing sources; submission
 * validates its contents. Null destroy is safe. Null bounds/pixels queries
 * return zero/NULL. Out parameters remain unchanged on failure; on success
 * create/compose transfer a NEW surface, not replacement of an existing one.
 * Caller frees prior owned values itself. Allocations are bounded by the pixel
 * limit; individual failures have explicit status. These are prototype cold
 * allocation/work limits, not steady-state zero-allocation or GPU claims:
 * at most 256 sources/filters, 16M pixels/surface, and 64M scatter taps/pass.
 * The radius cap of 16 pixels is a prototype backend limit, not a UI semantic
 * maximum. Recursive groups are formed
 * by composing group outputs as sources of a later compose call. */
CompositorStatus CompositorSurface_create(CompositorBounds bounds,
                                          CompositorSurface **out);
/* Arity constructors return a new owned surface or NULL on failure; use create
 * when the status is required. Zero constructs the owned empty identity. */
CompositorSurface *CompositorSurface_0(void);
CompositorSurface *CompositorSurface_1(CompositorBounds bounds);
CompositorSurface *CompositorSurface_zero(void);
#define GRAPHVEX_COMPOSITOR_CTOR(_0, _1, NAME, ...) NAME
#define CompositorSurface(...) \
    GRAPHVEX_COMPOSITOR_CTOR(0 __VA_OPT__(,) __VA_ARGS__, \
                            CompositorSurface_1, CompositorSurface_0)(__VA_ARGS__)
void CompositorSurface_destroy(CompositorSurface *surface);
CompositorBounds CompositorSurface_bounds(const CompositorSurface *surface);
float *CompositorSurface_pixels(CompositorSurface *surface);
const float *CompositorSurface_constPixels(const CompositorSurface *surface);
CompositorStatus CompositorSurface_validate(const CompositorSurface *surface);
/* Cold bounded strings. Null self writes "nullptr"; absent/zero-capacity dest
 * reports truncation. Strings never allocate, inspect pixels or recurse. */
void CompositorSurface_toString(const CompositorSurface *surface, char *dest,
                                size_t cap, bool *outTruncated);
void CompositorSurface_toStringStruct(const CompositorSurface *surface, char *dest,
                                      size_t cap, bool *outTruncated);

/* Validates tokens even for empty bounds. Ordered blur support expands each
 * side by its radius; pointwise filters keep bounds. No clipping is implicit.
 * Rejects unsupported IDs, reserved payload bits, invalid gains and overflow. */
CompositorStatus Compositor_filterBounds(CompositorBounds source,
                                        const FilterToken *filters, size_t count,
                                        CompositorBounds *out);
/* Isolate: source-over all sources in array order over transparent union bounds,
 * THEN apply the whole ordered stack exactly once to the assembled group.
 * Empty source/filter arrays may be NULL. No Foreground/Backdrop scene policy
 * is invented here: caller chooses and snapshots the correct sources. */
CompositorStatus Compositor_compose(const CompositorSurface *const *sources,
                                    size_t sourceCount,
                                    const FilterToken *filters, size_t filterCount,
                                    CompositorSurface **out);
/* Source-over into existing destination, integer origin-aware, clipped to its
 * bounds. Self-compositing is rejected. Failure never partially changes dest. */
CompositorStatus Compositor_sourceOver(const CompositorSurface *source,
                                       CompositorSurface *destination);

#endif
