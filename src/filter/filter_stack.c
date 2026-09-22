#include "lang/filter_stack.h"

#include <stdlib.h>
#include <string.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: FilterStack
 * ============================================================================
 * The ordered compositor filter chain. A stack is a list of BORROWED Filters
 * walked in insertion order; each node's output is the next node's input, so
 * the stack is a fold over an image and the ORDER is the meaning — blur then
 * contrast is not contrast then blur.
 *
 * The stack owns exactly ONE scratch Image (the ping-pong surface), so an
 * N-node stack costs one extra image, never N. The ping-pong parity is chosen
 * so the LAST node always writes into the caller's output: an odd stack starts
 * at the output, an even stack at the scratch.
 *
 * An empty stack is the identity fold (copy input -> output). A node that
 * cannot apply fails the whole fold — deterministic, never a half-processed
 * output (the Cold-Strict, Hot-Minimal Validation Law).
 *
 * Lifetime: the stack heap-owns its slot table + scratch; it never owns a
 * filter. Teardown is top-down (the Teardown Order Law): destroy the nodes,
 * then the stack.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: FilterStack (filter/filter_stack.c)
 * LEVEL: L2 — Behavior (ordered filter chain over an image)
 * ============================================================================
 * SUMMARY:
 *   Ordered list of borrowed Filters + one owned scratch Image. FilterStack_add
 *   appends; FilterStack_apply folds input -> output through the nodes in order.
 *
 * STRUCT FIELDS (Mirroring lang/filter_stack.h incomplete tag — completed here):
 * ----------------------------------------------------------------------------
 *   Filter **filters;   // borrowed nodes, insertion order (slot table)
 *   uint32_t count;     // active slots
 *   uint32_t cap;       // table capacity (doubling)
 *   Image *scratch;     // OWNED ping-pong surface (one for the whole stack)
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   tableGrow(stack)                  : double the slot table (Anti-Hardcoding Law)
 *   copyImage(input, output)          : RGBA8 shadow copy (the empty fold)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - FilterStack_0(void) / FilterStack_1(capacity)
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - FilterStack_destroy(stack)
 *   - FilterStack_add(stack, filter)
 *   - FilterStack_remove(stack, index)
 *   - FilterStack_apply(stack, cmdBuffer, input, output)
 *
 * Private Core Functions: (.c static)
 *   - tableGrow(stack)
 *   - copyImage(input, output)
 *
 * Public Getters: (.h)
 *   - FilterStack_count(stack)
 *   - FilterStack_get(stack, index)
 *   - FilterStack_isValid(stack)
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

struct FilterStack {
    Filter **filters;   // borrowed nodes, insertion order (slot table)
    uint32_t count;     // active slots
    uint32_t cap;       // table capacity (doubling)
    Image *scratch;     // OWNED ping-pong surface (one for the whole stack)
};

// Grow the slot table so a long chain never rejects (the Dynamic Scalability &
// Anti-Hardcoding Law). OOM leaves the cap untouched: add drop-degrades.
static bool tableGrow(FilterStack *stack) {
    if ((*stack).count < (*stack).cap)
        return true;
    uint32_t newCap = ((*stack).cap == 0) ? 8 : (*stack).cap * 2;
    Filter **nb = (Filter**) realloc((void*) (*stack).filters,
                                     (size_t) newCap * sizeof(*nb));
    if (nb == nullptr)
        return false;
    (*stack).filters = nb;
    (*stack).cap = newCap;
    return true;
}

// The empty fold: copy the RGBA8 shadow input -> output. Ensures the output
// shadow matches the input extent. Returns false on null/mismatch/OOM.
static bool copyImage(Image *input, Image *output) {
    if (input == nullptr || output == nullptr)
        return false;
    uint32_t w = Image_width(input);
    uint32_t h = Image_height(input);
    if (w == 0 || h == 0)
        return false;
    if (!Image_ensureShadow(w, h, output))
        return false;
    const uint8_t *src = Image_pixels(input);
    uint8_t *dst = Image_pixels(output);
    if (src == nullptr || dst == nullptr)
        return false;
    memcpy(dst, src, (size_t) w * (size_t) h * 4u);
    return true;
}

// CONSTRUCTORS (PUBLIC & PRIVATE)

static FilterStack *stackCreate(uint32_t capacity) {
    FilterStack *stack = (FilterStack*) calloc(1, sizeof(FilterStack));
    if (stack == nullptr)
        return nullptr;
    if (capacity > 0) {
        (*stack).filters = (Filter**) calloc(capacity, sizeof(Filter*));
        if ((*stack).filters == nullptr) {
            free(stack);
            return nullptr;
        }
        (*stack).cap = capacity;
    }
    return stack;
}

FilterStack *FilterStack_0(void) {
    return stackCreate(0);
}

FilterStack *FilterStack_1(uint32_t capacity) {
    return stackCreate(capacity);
}

void FilterStack_destroy(FilterStack *stack) {
    if (stack == nullptr)
        return;
    free((void*) (*stack).filters);
    (*stack).filters = nullptr;
    Image_destroy((*stack).scratch);
    (*stack).scratch = nullptr;
    free(stack);
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

bool FilterStack_add(FilterStack *stack, Filter *filter) {
    if (stack == nullptr || filter == nullptr)
        return false;
    if (!tableGrow(stack))
        return false;
    (*stack).filters[(*stack).count++] = filter;
    return true;
}

bool FilterStack_remove(FilterStack *stack, uint32_t index) {
    if (stack == nullptr || index >= (*stack).count)
        return false;
    (*stack).filters[index] = (*stack).filters[--(*stack).count];
    return true;
}

bool FilterStack_apply(FilterStack *stack, void *cmdBuffer, Image *input, Image *output) {
    if (stack == nullptr || input == nullptr || output == nullptr)
        return false;
    uint32_t n = (*stack).count;
    if (n == 0)
        return copyImage(input, output);

    // One owned scratch, sized to the input. The parity makes the LAST node
    // write into the caller's output: odd stack starts at output, even at scratch.
    uint32_t w = Image_width(input);
    uint32_t h = Image_height(input);
    if (w == 0 || h == 0)
        return false;
    if ((*stack).scratch == nullptr) {
        (*stack).scratch = Image_2(w, h);
        if ((*stack).scratch == nullptr)
            return false;
    } else if (!Image_ensureShadow(w, h, (*stack).scratch)) {
        return false;
    }

    bool startAtOutput = (n % 2u) == 1u;
    Image *src = input;
    for (uint32_t i = 0; i < n; i++) {
        bool toOutput = startAtOutput ? ((i % 2u) == 0u) : ((i % 2u) == 1u);
        Image *dst = toOutput ? output : (*stack).scratch;
        if (!Filter_apply((*stack).filters[i], cmdBuffer, src, dst))
            return false; // deterministic: a failed node fails the whole fold
        src = dst;
    }
    return true;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
uint32_t FilterStack_count(const FilterStack *stack) {
    return stack ? (*stack).count : 0u;
}

;;GETTER
Filter *FilterStack_get(const FilterStack *stack, uint32_t index) {
    if (stack == nullptr || index >= (*stack).count)
        return nullptr;
    return (*stack).filters[index];
}

;;GETTER
bool FilterStack_isValid(const FilterStack *stack) {
    return stack != nullptr;
}
