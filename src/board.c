#include "board.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Board (board.c)
 * ============================================================================
 * A RETAINED offscreen render target — NOT a swapchain and never a window
 * surface. You render into its Image, then composite it into the current
 * target. The board owns its pixels across frames; a resize recreates them only
 * when the extent actually changes.
 *
 * Board_publish() bumps an atomic generation, and that newer generation is what
 * wakes the present-on-demand loop, so a board that did not change costs zero
 * frames. To keep R3 free of R4 types, the board owns no scene: the layer that
 * DOES own the scene registers a revalidate step (BoardRevalidateFn), and
 * Board_revalidate runs them in order and then publishes. This is the middle of
 * the Frame -> Surface -> Board -> panel render cascade.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Board (board.c)
 * ============================================================================
 * Retained offscreen Image + atomic generation + borrowed revalidate steps.
 *
 * STRUCT FIELDS:
 * ----------------------------------------------------------------------------
 *   Image    *image;         // owned target (RGBA8)
 *   _Atomic uint64_t generation; // publishes so far (0 = never)
 *   void     *native;        // opaque dialect handle (the backend's VkImage)
 *   BoardRevalidateFn *revalidators; // borrowed steps (R4 scene/content)
 *   void    **revalidateUd;  // userdata per step
 *   int       revalidateCount, revalidateCap;
 *
 * FUNCTION REGISTRY (exported by board.h):
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Board_0, Board_2, Board_new, Board_destroy
 * Core:
 *   - Board_resize, Board_publish, Board_fill
 * Queries / Getters:
 *   - Board_width, Board_height, Board_generation, Board_isValid,
 *     Board_image, Board_native
 * Revalidation:
 *   - Board_addRevalidator, Board_clearRevalidators, Board_revalidate
 * ============================================================================
 */

struct Board {
    Image *image;
    _Atomic uint64_t generation;
    void *native;    // opaque dialect handle (the backend's VkImage)
    BoardRevalidateFn *revalidators;   // borrowed steps (R4 scene/content panels)
    void **revalidateUd;               // userdata per step
    int revalidateCount, revalidateCap;
};

// Creates a retained RGBA image target from the descriptor, using 1x1 defaults when absent.
Board *Board_new(const BoardDesc *desc) {
    Board *b = calloc(1, sizeof *b);
    if (!b) return nullptr;
    uint32_t w = desc ? (*desc).width : 1;
    uint32_t h = desc ? (*desc).height : 1;
    uint32_t usage = desc ? (*desc).usage : (IMAGE_USAGE_RENDER | IMAGE_USAGE_SAMPLED | IMAGE_USAGE_TRANSFER);
    ImageDesc id = {w ? w : 1u, h ? h : 1u, IMAGE_FORMAT_RGBA8, usage};
    (*b).image = Image_new(&id);
    if (!(*b).image) {
        free(b);
        return nullptr;
    }
    return b;
}

// Creates a board with the default 1x1 image target.
Board *Board_0(void) { return Board_new(nullptr); }
// Creates a board with the requested dimensions and default image usage.
Board *Board_2(uint32_t width, uint32_t height) {
    BoardDesc d = {width, height, 0u};
    return Board_new(&d);
}

// Destroys the owned image and callback arrays; null is ignored.
void Board_destroy(Board *board) {
    if (!board) return;
    Image_destroy((*board).image);
    free((*board).revalidators);
    free((*board).revalidateUd);
    free(board);
}

// Resizes the image target, preserving its allocation when the current capacity fits.
bool Board_resize(Board *board, uint32_t width, uint32_t height) {
    if (!board || !(*board).image) return false;
    if (Image_width((*board).image) == width && Image_height((*board).image) == height) return true;
    return Image_resize((*board).image, width, height);
}

// Advances the atomic generation to announce newly published board content.
void Board_publish(Board *board) {
    if (!board) return;
    atomic_fetch_add(&(*board).generation, 1u);
}

// Fills the board's image with the supplied packed color when the board is valid.
void Board_fill(Board *board, Color color) {
    if (board && (*board).image) Image_fill((*board).image, color);
}

// Returns the image width, or zero when the board or image is absent.
uint32_t Board_width(const Board *board) { return board && (*board).image ? Image_width((*board).image) : 0u; }
// Returns the image height, or zero when the board or image is absent.
uint32_t Board_height(const Board *board) { return board && (*board).image ? Image_height((*board).image) : 0u; }
// Reads the atomic publish generation, or zero for a null board.
uint64_t Board_generation(const Board *board) {
    return board ? atomic_load(&(*board).generation) : 0u;
}
// Reports whether the board has a nonempty image target.
bool Board_isValid(const Board *board) { return board && Image_isValid((*board).image); }
// Returns the board-owned image as a borrowed pointer, or nullptr for a null board.
Image *Board_image(const Board *board) { return board ? (*board).image : nullptr; }
// Returns the opaque native handle without transferring ownership.
void *Board_native(const Board *board) { return board ? (*board).native : nullptr; }

// ── revalidation ────────────────────────────────────────────────────────────
// Appends a borrowed callback and userdata pair; invalid input or growth failure leaves it unadded.
void Board_addRevalidator(Board *board, BoardRevalidateFn fn, void *userdata) {
    if (!board || !fn) return;
    if ((*board).revalidateCount == (*board).revalidateCap) {
        int cap = (*board).revalidateCap ? (*board).revalidateCap * 2 : 4;
        BoardRevalidateFn *steps = realloc((*board).revalidators, (size_t)cap * sizeof *steps);
        void **uds = realloc((*board).revalidateUd, (size_t)cap * sizeof *uds);
        if (!steps || !uds) return;   // best-effort; keep whatever we had
        (*board).revalidators = steps;
        (*board).revalidateUd = uds;
        (*board).revalidateCap = cap;
    }
    (*board).revalidators[(*board).revalidateCount] = fn;
    (*board).revalidateUd[(*board).revalidateCount] = userdata;
    (*board).revalidateCount++;
}

// Removes all registered callbacks without freeing their borrowed userdata.
void Board_clearRevalidators(Board *board) {
    if (!board) return;
    (*board).revalidateCount = 0;
}

// Runs registered callbacks in order, then publishes the board once.
void Board_revalidate(Board *board) {
    if (!board) return;
    for (int i = 0; i < (*board).revalidateCount; i++)
        (*board).revalidators[i](board, (*board).revalidateUd[i]);
    Board_publish(board);   // the board changed; wake the demand loop
}
