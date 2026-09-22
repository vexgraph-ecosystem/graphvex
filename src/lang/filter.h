#ifndef LANG_FILTER_H
#define LANG_FILTER_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/image.h"

// lang/filter.h — the filter contract (the language's compositor verbs).
//
// A Filter is one node in an ORDERED compositor stack. Order is meaning:
// blur -> contrast -> depth-of-field is NOT depth-of-field -> blur -> contrast.
// The stack walks its nodes in order, each apply() reading the previous node's
// output — so a filter is a pure image->image transform, and the stack is a
// fold over it.
//
// FILTERS ARE ROWS: unlike Image (a resource), a filter is a STRATEGY with
// swappable implementations — blur(Box), blur(Gaussian), contrast, DoF — so
// each algorithm is its own FilterRow, one file per algorithm
// (filter/blur/box_blur.c, filter/blur/gaussian_blur.c, ...). The stack holds
// heterogeneous Filters and never names an algorithm type.
//
// APPLY TAKES A COMMAND BUFFER: null cmdBuffer = the CPU path (operate on the
// Image CPU shadow — the raster dialect and headless tests); non-null = the
// GPU path (record into the dialect's pass). A filter that has not implemented
// a path answers false (cold-false, the Cold-Strict, Hot-Minimal Validation Law).

// Filter kinds — one per algorithm (each maps to exactly one FilterRow).
#define FILTER_NONE           0u
#define FILTER_BLUR_BOX       1u
#define FILTER_BLUR_GAUSSIAN  2u
#define FILTER_CONTRAST       3u
#define FILTER_DEPTH_OF_FIELD 4u

// Opaque filter: dialect-private state lives behind this.
typedef struct Filter Filter;

// Construction parameters. Every field has a default; zero it and name only
// the kind. `param[]` is kind-specific (e.g. blur radius in param[0]).
typedef struct FilterDesc {
    uint32_t kind;        // FILTER_* (NONE = fail-closed)
    uint32_t width;       // working extent, native px (default 0 = follow input)
    uint32_t height;      // working extent, native px
    float param[4];       // kind-specific parameters
} FilterDesc;

// --- Constructors ---
//   Filter(kind)                     -> defaults for that kind
//   Filter_new(&(FilterDesc){ ... }) -> every other field
//
// A filter MUST name its kind, so the call-site macro is single-arity.
Filter *Filter_1(uint32_t kind);
Filter *Filter_new(const FilterDesc *desc);

#define Filter(kind) Filter_1(kind)

// Destroy the filter and its private state. Null-safe (the Teardown Order Law:
// the stack destroys nodes top-down before the device).
void Filter_destroy(Filter *filter);

// --- Core functions ---
// Apply the filter: input -> output. cmdBuffer null = CPU path. Returns false
// when not applicable (no shadow, unimplemented path, device lost) — never
// crashes, never blocks unbounded (the Bounded Wait Law).
bool Filter_apply(Filter *filter, void *cmdBuffer, Image *input, Image *output);

// --- Setters / Getters (the Symmetric Getter/Setter Completeness Law) ---
// param index 0..3. Null-safe: setters no-op, getters answer 0.0f.
void  Filter_setParam(Filter *filter, uint32_t index, float value);
float Filter_getParam(const Filter *filter, uint32_t index);

uint32_t    Filter_kind(const Filter *filter);     // FILTER_*, NONE on null
bool        Filter_isValid(const Filter *filter);
const char *Filter_name(const Filter *filter);     // algorithm name

#endif // LANG_FILTER_H
