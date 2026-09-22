#ifndef LANG_FILTER_STACK_H
#define LANG_FILTER_STACK_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/filter.h"
#include "lang/image.h"

// lang/filter_stack.h — the ordered compositor filter stack.
//
// A FilterStack is an ORDERED list of Filters folded over an image:
//
//   stack = [ blur.box(2), contrast(1.5), depth_of_field(...) ]
//   stack_apply(input) -> ... -> output
//
// Order is meaning: blur then contrast is a different fold than contrast then
// blur. The stack owns no algorithm — it borrows its nodes (the caller creates
// and destroys them) and walks them in insertion order, each node's output
// feeding the next node's input (a ping-pong through one owned scratch Image,
// so an N-node stack costs exactly one extra image, never N).
//
// Empty stack = identity: apply copies input to output (the empty fold). A node
// that cannot apply (unimplemented GPU path, missing shadow, OOM) fails the
// whole fold — deterministic, never a half-processed output.

typedef struct FilterStack FilterStack;

// --- Constructors ---
// FilterStack()          -> empty stack, default capacity
// FilterStack(capacity)  -> pre-sized (grows by doubling regardless)
FilterStack *FilterStack_0(void);
FilterStack *FilterStack_1(uint32_t capacity);

#define FILTER_STACK_CHOOSER(_0, _1, NAME, ...) NAME
#define FilterStack(...) FILTER_STACK_CHOOSER( \
    dummy __VA_OPT__(,) __VA_ARGS__, \
    FilterStack_1, FilterStack_0 \
)(__VA_ARGS__)

// Destroy the stack + its scratch. BORROWED filters are NOT destroyed (the
// caller owns the nodes; the Teardown Order Law: free the nodes, then the stack).
void FilterStack_destroy(FilterStack *stack);

// --- Core functions ---
// Append a filter (borrowed). Grows by doubling (the Dynamic Scalability &
// Anti-Hardcoding Law). Returns false on null filter or OOM.
bool FilterStack_add(FilterStack *stack, Filter *filter);

// Swap-remove the node at index. Returns false on a stale index.
bool FilterStack_remove(FilterStack *stack, uint32_t index);

// Fold the stack over input -> output. cmdBuffer null = CPU path. Empty stack
// copies input to output. Returns false on null args or any node failure.
bool FilterStack_apply(FilterStack *stack, void *cmdBuffer, Image *input, Image *output);

// --- Getters (the Symmetric Getter/Setter Completeness Law: null-safe) ---
uint32_t FilterStack_count(const FilterStack *stack);
Filter  *FilterStack_get(const FilterStack *stack, uint32_t index); // borrowed
bool     FilterStack_isValid(const FilterStack *stack);

#endif // LANG_FILTER_STACK_H
