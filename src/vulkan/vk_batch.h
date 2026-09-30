#ifndef GRAPHICS_VK_BATCH_H
#define GRAPHICS_VK_BATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "graphics/graphics.h"
#include "image.h"

// graphvex R3 — vulkan/vk_batch.h
//
// THE QUAD BATCHER. The whole renderer is "turn the display list into one
// vertex buffer of textured quads and issue a handful of draw calls." This is
// pure CPU data — no Vulkan handle — so it is unit-testable headless and the
// GPU side only ever uploads + draws it.
//
// One vertex per corner. A quad is two triangles (6 vertices). Every rectangle,
// image and glyph is a quad; the fragment shader does the rounded-corner SDF,
// the border, and the (future) glyph/alpha sampling.

typedef struct VkQuad {
    // destination, native px
    float x, y, w, h;
    // source UVs in [0,1] (image); rects use 0..1 solid
    float u0, v0, u1, v1;
    Color fill;
    Color border;
    float radius;    // corner radius, px
    float stroke;    // border width, px (0 = none)
    float mode;      // 0 = solid, 1 = image, 2 = glyph mask
    uint32_t texture; // atlas/layer id (0 = white)
} VkQuad;

typedef struct VkBatch {
    VkQuad *quads;
    size_t count;
    size_t cap;
} VkBatch;

// One interleaved vertex: pos(2) uv(2) fill(4) border(4) params(4) quadSize(2)
#define VK_VERTEX_FLOATS 18u

typedef struct VkVertex {
    float x, y;
    float u, v;
    float r, g, b, a;         // fill
    float br, bg, bb, ba;     // border
    float radius, stroke, mode, layer;
    float qw, qh;             // quad size in px (for the rounded-rect SDF)
} VkVertex;

VkBatch *VkBatch_0(void);
void VkBatch_free(VkBatch *b);
void VkBatch_clear(VkBatch *b);

void VkBatch_rect(VkBatch *b, Rect dst, const Brush *brush);
void VkBatch_image(VkBatch *b, const Image *image, Rect src, Rect dst);
void VkBatch_glyph(VkBatch *b, Rect dst, uint32_t atlasLayer, Color color);

// Expand to 6 vertices per quad. Returns the number of vertices written; if
// `out` is null or `capacity` is too small, returns the required count and
// writes nothing (caller sizes the upload).
size_t VkBatch_vertices(const VkBatch *b, VkVertex *out, size_t capacity);

#endif // GRAPHICS_VK_BATCH_H
