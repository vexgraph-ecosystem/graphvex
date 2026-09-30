#include "vulkan/vk_batch.h"

#include <stdlib.h>
#include <string.h>

// graphvex R3 — vulkan/vk_batch.c
// Pure CPU quad batching. No Vulkan handle: testable headless.

VkBatch *VkBatch_0(void) {
    VkBatch *b = calloc(1, sizeof *b);
    return b;
}

void VkBatch_free(VkBatch *b) {
    if (!b) return;
    free(b->quads);
    free(b);
}

void VkBatch_clear(VkBatch *b) {
    if (b) b->count = 0;
}

static VkQuad *batch_push(VkBatch *b) {
    if (b->count == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        VkQuad *grown = realloc(b->quads, b->cap * sizeof *grown);
        if (!grown) { b->cap = 0; b->count = 0; return NULL; }
        b->quads = grown;
    }
    VkQuad *q = &b->quads[b->count++];
    memset(q, 0, sizeof *q);
    q->u0 = 0.0f; q->v0 = 0.0f; q->u1 = 1.0f; q->v1 = 1.0f;
    return q;
}

void VkBatch_rect(VkBatch *b, Rect dst, const Brush *brush) {
    if (!b || !brush || Rect_isEmpty(dst)) return;
    VkQuad *q = batch_push(b);
    if (!q) return;
    q->x = dst.x; q->y = dst.y; q->w = dst.w; q->h = dst.h;
    q->fill = brush->color;
    q->border = brush->border;
    q->radius = brush->radius;
    q->stroke = brush->borderWidth;
    q->mode = 0.0f;
    q->texture = 0u;
}

void VkBatch_image(VkBatch *b, const Image *image, Rect src, Rect dst) {
    if (!b || !image || Rect_isEmpty(dst)) return;
    VkQuad *q = batch_push(b);
    if (!q) return;
    q->x = dst.x; q->y = dst.y; q->w = dst.w; q->h = dst.h;
    float iw = Image_width(image) > 0 ? (float)Image_width(image) : 1.0f;
    float ih = Image_height(image) > 0 ? (float)Image_height(image) : 1.0f;
    q->u0 = src.x / iw; q->v0 = src.y / ih;
    q->u1 = (src.x + src.w) / iw; q->v1 = (src.y + src.h) / ih;
    q->fill = COLOR_WHITE;
    q->mode = 1.0f;
    q->texture = Image_layer(image);
}

void VkBatch_glyph(VkBatch *b, Rect dst, uint32_t atlasLayer, Color color) {
    if (!b || Rect_isEmpty(dst)) return;
    VkQuad *q = batch_push(b);
    if (!q) return;
    q->x = dst.x; q->y = dst.y; q->w = dst.w; q->h = dst.h;
    q->fill = color;
    q->mode = 2.0f;
    q->texture = atlasLayer;
}

static void put(VkVertex *v, const VkQuad *q, float x, float y, float u, float vv) {
    v->x = x; v->y = y;
    v->u = u; v->v = vv;
    v->r = (float)Color_red(q->fill) / 255.0f;
    v->g = (float)Color_green(q->fill) / 255.0f;
    v->b = (float)Color_blue(q->fill) / 255.0f;
    v->a = (float)Color_alpha(q->fill) / 255.0f;
    v->br = (float)Color_red(q->border) / 255.0f;
    v->bg = (float)Color_green(q->border) / 255.0f;
    v->bb = (float)Color_blue(q->border) / 255.0f;
    v->ba = (float)Color_alpha(q->border) / 255.0f;
    v->radius = q->radius;
    v->stroke = q->stroke;
    v->mode = q->mode;
    v->layer = (float)q->texture;
    v->qw = q->w;
    v->qh = q->h;
}

size_t VkBatch_vertices(const VkBatch *b, VkVertex *out, size_t capacity) {
    if (!b) return 0;
    size_t needed = b->count * 6u;
    if (!out || capacity < needed) return needed;
    size_t w = 0;
    for (size_t i = 0; i < b->count; i++) {
        const VkQuad *q = &b->quads[i];
        float x0 = q->x, y0 = q->y, x1 = q->x + q->w, y1 = q->y + q->h;
        // two triangles: (0,0)(1,0)(0,1)  (0,1)(1,0)(1,1)
        put(&out[w++], q, x0, y0, q->u0, q->v0);
        put(&out[w++], q, x1, y0, q->u1, q->v0);
        put(&out[w++], q, x0, y1, q->u0, q->v1);
        put(&out[w++], q, x0, y1, q->u0, q->v1);
        put(&out[w++], q, x1, y0, q->u1, q->v0);
        put(&out[w++], q, x1, y1, q->u1, q->v1);
    }
    return w;
}
