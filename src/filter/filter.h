#ifndef FILTER_FILTER_H
#define FILTER_FILTER_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/filter.h"

// filter/filter.h — the filter registry (the filter half of the language).
//
// A Filter is a thin `{ row, state }` wrapper: the row is one algorithm's verb
// table, the state is that algorithm's private struct. Every algorithm
// (box_blur, gaussian_blur, contrast, depth_of_field) exports ONE `const
// FilterRow` and registers it once at boot; Filter_create resolves the row by
// kind. Same shape as the Device registry — one contract, one row per
// implementation, zero algorithm types in caller code.
//
// SLOT RECORD: FilterRow (owned by the registry — a behaviorless function
// table, the Single Class Per File Law). The `Filter` class lives in
// filter/filter.c.
//
// LIFETIME: fixed table, doubling growth, zero steady-state allocation (the
// Dynamic Scalability & Anti-Hardcoding Law).

// SLOT RECORD: one algorithm's filter verbs. `state` is the algorithm's private
// struct; the registry never inspects it.
typedef struct FilterRow {
    uint32_t kind;                                             // FILTER_*
    const char *name;                                          // algorithm name
    void  *(*createState)(const FilterDesc *desc);             // algorithm up
    void   (*destroyState)(void *state);                       // algorithm down
    bool   (*apply)(void *state, void *cmdBuffer, Image *input, Image *output);
    bool   (*setParam)(void *state, uint32_t index, float value);
    float  (*getParam)(const void *state, uint32_t index);
} FilterRow;

// --- Constructors ---
// Register an algorithm row. Idempotent per kind (re-register replaces). The
// row must outlive the process (algorithms export static rows). Returns false
// on null row, null verbs, or kind NONE.
bool Filter_registerRow(const FilterRow *row);

// --- Core functions ---
const FilterRow *Filter_findRow(uint32_t kind);
uint32_t Filter_rowCount(void);
const char *Filter_kindName(uint32_t kind);   // algorithm name, or "none"

#endif // FILTER_FILTER_H
