#ifndef LANG_COMPOSITOR_H
#define LANG_COMPOSITOR_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/filter_stack.h"
#include "lang/image.h"

// lang/compositor.h — the board compositor (the seam's collage + filter chain).
//
// A Compositor folds the window's boards into ONE image, in z-order, then runs
// the ordered filter stack over the result:
//
//   scene (bottom)  ┐
//                   ├─ over-composite ─> canvas ─ filter stack ─> out
//   content (top)   ┘
//
// It BORROWS its boards and its filter stack (the caller owns them); it OWNS
// only the canvas — the un-filtered composite, one Image for the whole window.
// The result lands in the caller's `out`, so the device presents whatever the
// compositor produced (the seam canvas is the compositor's output).
//
// cmdBuffer null = the CPU path (blend + CPU filters over the Image shadows);
// non-null = the GPU path (a dialect records the collage + chain). The GPU path
// is cold-false until the Vulkan/Metal compositor lands (the Cold-Strict,
// Hot-Minimal Validation Law).

typedef struct Compositor Compositor;

// --- Constructors ---
// Compositor()          -> canvas sized from the first composite
// Compositor(w, h)      -> canvas pre-sized, native px
Compositor *Compositor_0(void);
Compositor *Compositor_2(uint32_t width, uint32_t height);

#define COMPOSITOR_CHOOSER(_0, _1, _2, NAME, ...) NAME
#define Compositor(...) COMPOSITOR_CHOOSER( \
    dummy __VA_OPT__(,) __VA_ARGS__, \
    Compositor_2, Compositor_1, Compositor_0 \
)(__VA_ARGS__)

// Destroy the compositor + its canvas. BORROWED boards/stack are NOT destroyed
// (the Teardown Order Law: free the boards, then the compositor).
void Compositor_destroy(Compositor *compositor);

// --- Core functions ---
// Composite scene (bottom) then content (top) into the canvas, run the filter
// stack, and write the result into out. cmdBuffer null = CPU path. Returns false
// on null args or any node failure.
bool Compositor_composite(Compositor *compositor, void *cmdBuffer, Image *out);

// --- Setters (all borrowed; null clears the slot) ---
void Compositor_setScene(Compositor *compositor, Image *scene);      // bottom
void Compositor_setContent(Compositor *compositor, Image *content);  // top
void Compositor_setFilters(Compositor *compositor, FilterStack *stack);

// --- Getters (the Symmetric Getter/Setter Completeness Law: null-safe) ---
Image *Compositor_getScene(const Compositor *compositor);      // borrowed
Image *Compositor_getContent(const Compositor *compositor);    // borrowed
FilterStack *Compositor_getFilters(const Compositor *compositor);
Image *Compositor_getCanvas(const Compositor *compositor);     // owned
uint32_t Compositor_width(const Compositor *compositor);
uint32_t Compositor_height(const Compositor *compositor);
bool Compositor_isValid(const Compositor *compositor);

#endif // LANG_COMPOSITOR_H
