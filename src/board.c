#include "board.h"

#include <stdlib.h>

// graphvex R3 — board.c
// Retained offscreen target. Owns its Image; publishes a generation.

struct Board {
    Image *image;
    _Atomic uint64_t generation;
    void *native;    // opaque dialect handle (the backend's VkImage)
};

Board *Board_new(const BoardDesc *desc) {
    Board *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    uint32_t w = desc ? desc->width : 1;
    uint32_t h = desc ? desc->height : 1;
    uint32_t usage = desc ? desc->usage : (IMAGE_USAGE_RENDER | IMAGE_USAGE_SAMPLED | IMAGE_USAGE_TRANSFER);
    ImageDesc id = {w ? w : 1u, h ? h : 1u, IMAGE_FORMAT_RGBA8, usage};
    b->image = Image_new(&id);
    if (!b->image) {
        free(b);
        return NULL;
    }
    return b;
}

Board *Board_0(void) { return Board_new(NULL); }
Board *Board_2(uint32_t width, uint32_t height) {
    BoardDesc d = {width, height, 0u};
    return Board_new(&d);
}

void Board_destroy(Board *board) {
    if (!board) return;
    Image_destroy(board->image);
    free(board);
}

bool Board_resize(Board *board, uint32_t width, uint32_t height) {
    if (!board || !board->image) return false;
    if (Image_width(board->image) == width && Image_height(board->image) == height) return true;
    return Image_resize(board->image, width, height);
}

void Board_publish(Board *board) {
    if (!board) return;
    atomic_fetch_add(&board->generation, 1u);
}

void Board_fill(Board *board, Color color) {
    if (board && board->image) Image_fill(board->image, color);
}

uint32_t Board_width(const Board *board) { return board && board->image ? Image_width(board->image) : 0u; }
uint32_t Board_height(const Board *board) { return board && board->image ? Image_height(board->image) : 0u; }
uint64_t Board_generation(const Board *board) {
    return board ? atomic_load(&board->generation) : 0u;
}
bool Board_isValid(const Board *board) { return board && Image_isValid(board->image); }
Image *Board_image(const Board *board) { return board ? board->image : NULL; }
void *Board_native(const Board *board) { return board ? board->native : NULL; }
