#ifndef GRAPHICS_BOARD_H
#define GRAPHICS_BOARD_H

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>

#include "graphics/graphics.h"
#include "image.h"

// graphvex R3 — board.h
//
// A RETAINED offscreen render target — NOT a swapchain and never a window
// surface. You render into its Image, then composite it (blit) into the current
// target. A Board owns its pixels across frames; resizing recreates them only
// when the size changes.
//
// Board_publish() bumps an atomic generation. That newer generation is what
// wakes the present-on-demand loop (RenderLoop), so a board that did not change
// costs zero frames. This is the whole reason there is no swapchain: WE own the
// images, the presentation seam just copies one.

typedef struct Board Board;

typedef struct BoardDesc {
    uint32_t width;    // native px (default 1)
    uint32_t height;   // native px (default 1)
    uint32_t usage;    // IMAGE_USAGE_* (default RENDER|SAMPLED|TRANSFER)
} BoardDesc;

Board *Board_0(void);
Board *Board_2(uint32_t width, uint32_t height);
Board *Board_new(const BoardDesc *desc);
void Board_destroy(Board *board);

bool Board_resize(Board *board, uint32_t width, uint32_t height);
void Board_publish(Board *board);              // generation++ (drives the demand loop)
void Board_fill(Board *board, Color color);    // CPU-side clear of the target image

uint32_t Board_width(const Board *board);      // native px
uint32_t Board_height(const Board *board);
uint64_t Board_generation(const Board *board); // publishes so far (0 = never)
bool     Board_isValid(const Board *board);
Image   *Board_image(const Board *board);      // the retained target (borrowed)
void    *Board_native(const Board *board);     // opaque dialect handle (VkImage)

// ── revalidation (the render cascade, R4 registers, R3 invokes) ─────────────
// A board owns a generation but no scene: the layer that DOES own the scene
// (an R4 scene/content panel) registers a revalidate step here, so the chain
// Frame -> Surface -> Board -> panel stays top-down without R3 learning R4
// types. A step renders what the board holds; Board_revalidate runs every step
// in registration order, then publishes so the demand loop wakes.
typedef void (*BoardRevalidateFn)(Board *board, void *userdata);

void Board_addRevalidator(Board *board, BoardRevalidateFn fn, void *userdata); // borrowed; replace not deduped
void Board_clearRevalidators(Board *board);
void Board_revalidate(Board *board);   // run every step, then Board_publish

#endif // GRAPHICS_BOARD_H
