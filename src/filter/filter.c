#include "filter/filter.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Filter
 * ============================================================================
 * One node in the ordered compositor stack. A Filter is a thin { row, state }
 * wrapper: the row is one algorithm's verb table (box blur, gaussian blur,
 * contrast, depth-of-field), the state is that algorithm's private struct.
 * Callers name a kind and receive an opaque Filter — they never spell an
 * algorithm type.
 *
 * The registry resolves the algorithm by kind at construction; every verb
 * forwards through the row. The stack walks its Filters in order, so apply()
 * is a pure image->image transform and the stack is a fold: blur then contrast
 * is a different fold than contrast then blur.
 *
 * Lifetime: the wrapper is heap-allocated once (cold path); the algorithm
 * state is created by createState and destroyed by destroyState, top-down per
 * the Teardown Order Law. Zero steady-state allocation.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Filter (filter/filter.c)
 * LEVEL: L2 — Behavior (one ordered node of the compositor filter stack)
 * ============================================================================
 * SUMMARY:
 *   Backend-agnostic filter wrapper over an algorithm row. Filter_new resolves
 *   the row by FILTER_* kind, asks the algorithm to create its state, and
 *   stores both. Every Filter_* verb forwards through the row.
 *
 * STRUCT FIELDS (Mirroring lang/filter.h incomplete tag — completed here):
 * ----------------------------------------------------------------------------
 *   const FilterRow *row;   // algorithm verb table (static, process-lifetime)
 *   void *state;            // algorithm-private state (opaque here)
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   registryGrow(void)      : double the algorithm table (Anti-Hardcoding Law)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Filter_1(kind)
 *   - Filter_new(desc)
 *   - Filter_registerRow(row)
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Filter_destroy(filter)
 *   - Filter_apply(filter, cmdBuffer, input, output)
 *   - Filter_findRow(kind)
 *   - Filter_rowCount(void)
 *
 * Private Core Functions: (.c static)
 *   - registryGrow(void)
 *
 * Public Setters: (.h)
 *   - Filter_setParam(filter, index, value)
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Filter_getParam(filter, index)
 *   - Filter_kind(filter)
 *   - Filter_isValid(filter)
 *   - Filter_name(filter)
 *   - Filter_kindName(kind)
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

struct Filter {
    const FilterRow *row;   // algorithm verb table (static, process-lifetime)
    void *state;            // algorithm-private state (opaque here)
};

// --- Algorithm registry (fixed table, doubling growth) ---
static const FilterRow **s_rows = nullptr;
static uint32_t s_rowCount = 0;
static uint32_t s_rowCap = 0;

// Grow the algorithm table so a late registration never rejects (the Dynamic
// Scalability & Anti-Hardcoding Law). OOM leaves the cap untouched: the caller
// drop-degrades (the Cold-Strict, Hot-Minimal Validation Law).
static bool registryGrow(void) {
    if (s_rowCount < s_rowCap)
        return true;
    uint32_t newCap = (s_rowCap == 0) ? 8 : s_rowCap * 2;
    const FilterRow **nb = (const FilterRow**) realloc((void*) s_rows,
                                                       (size_t) newCap * sizeof(*nb));
    if (nb == nullptr)
        return false;
    s_rows = nb;
    s_rowCap = newCap;
    return true;
}

// CONSTRUCTORS (PUBLIC & PRIVATE)

bool Filter_registerRow(const FilterRow *row) {
    if (row == nullptr || (*row).kind == FILTER_NONE)
        return false;
    if ((*row).createState == nullptr || (*row).destroyState == nullptr)
        return false;
    for (uint32_t i = 0; i < s_rowCount; i++) {
        if ((*s_rows[i]).kind == (*row).kind) {
            s_rows[i] = row;
            return true;
        }
    }
    if (!registryGrow())
        return false;
    s_rows[s_rowCount++] = row;
    return true;
}

const FilterRow *Filter_findRow(uint32_t kind) {
    if (kind == FILTER_NONE)
        return nullptr;
    for (uint32_t i = 0; i < s_rowCount; i++) {
        if ((*s_rows[i]).kind == kind)
            return s_rows[i];
    }
    return nullptr;
}

uint32_t Filter_rowCount(void) {
    return s_rowCount;
}

static Filter *filterCreate(const FilterDesc *desc) {
    if (desc == nullptr || (*desc).kind == FILTER_NONE)
        return nullptr;
    const FilterRow *row = Filter_findRow((*desc).kind);
    if (row == nullptr || (*row).createState == nullptr)
        return nullptr;
    void *state = (*row).createState(desc);
    if (state == nullptr)
        return nullptr;
    Filter *filter = (Filter*) calloc(1, sizeof(Filter));
    if (filter == nullptr) {
        (*row).destroyState(state);
        return nullptr;
    }
    (*filter).row = row;
    (*filter).state = state;
    return filter;
}

Filter *Filter_new(const FilterDesc *desc) {
    return filterCreate(desc);
}

Filter *Filter_1(uint32_t kind) {
    return filterCreate(&(FilterDesc){ .kind = kind });
}

void Filter_destroy(Filter *filter) {
    if (filter == nullptr)
        return;
    if ((*filter).row != nullptr && (*filter).state != nullptr)
        (*filter).row->destroyState((*filter).state);
    free(filter);
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

bool Filter_apply(Filter *filter, void *cmdBuffer, Image *input, Image *output) {
    if (filter == nullptr || (*filter).state == nullptr)
        return false;
    if ((*filter).row->apply == nullptr)
        return false;
    return (*filter).row->apply((*filter).state, cmdBuffer, input, output);
}

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void Filter_setParam(Filter *filter, uint32_t index, float value) {
    if (filter == nullptr || (*filter).state == nullptr)
        return;
    if ((*filter).row->setParam == nullptr)
        return;
    (*filter).row->setParam((*filter).state, index, value);
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
float Filter_getParam(const Filter *filter, uint32_t index) {
    if (filter == nullptr || (*filter).state == nullptr || (*filter).row->getParam == nullptr)
        return 0.0f;
    return (*filter).row->getParam((*filter).state, index);
}

;;GETTER
uint32_t Filter_kind(const Filter *filter) {
    if (filter == nullptr || (*filter).row == nullptr)
        return FILTER_NONE;
    return (*filter).row->kind;
}

;;GETTER
bool Filter_isValid(const Filter *filter) {
    return filter != nullptr && (*filter).row != nullptr && (*filter).state != nullptr;
}

;;GETTER
const char *Filter_name(const Filter *filter) {
    if (filter == nullptr || (*filter).row == nullptr || (*filter).row->name == nullptr)
        return "none";
    return (*filter).row->name;
}

;;GETTER
const char *Filter_kindName(uint32_t kind) {
    const FilterRow *row = Filter_findRow(kind);
    if (row == nullptr || (*row).name == nullptr)
        return "none";
    return (*row).name;
}
