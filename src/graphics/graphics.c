#include "graphics/graphics.h"

#include <stdlib.h>
#include <string.h>

// graphvex R3 — graphics/graphics.c
// The registry, the slim forwarders, the display-list streamer, and the
// headless CPU (raster) backend. No Vulkan here: the GPU backend lands behind
// the same row later.

// ── backend registry ────────────────────────────────────────────────────────
#define MAX_BACKENDS 8
static const Backend *s_rows[MAX_BACKENDS];
static int s_rowCount = 0;
static const Backend *s_current = NULL;

const Backend *RasterGraphics_row(void);   // defined below

bool Graphics_register(const Backend *row) {
    if (!row || row->id == BACKEND_NONE) return false;
    for (int i = 0; i < s_rowCount; i++) {
        if (s_rows[i]->id == row->id) { s_rows[i] = row; return true; }
    }
    if (s_rowCount >= MAX_BACKENDS) return false;
    s_rows[s_rowCount++] = row;
    return true;
}

bool Graphics_use(uint32_t backendId) {
    for (int i = 0; i < s_rowCount; i++) {
        if (s_rows[i]->id == backendId) { s_current = s_rows[i]; return true; }
    }
    // convenience: the raster backend self-registers on first use
    if (backendId == BACKEND_RASTER) {
        Graphics_register(RasterGraphics_row());
        for (int i = 0; i < s_rowCount; i++)
            if (s_rows[i]->id == backendId) { s_current = s_rows[i]; return true; }
    }
    return false;
}

uint32_t Graphics_backendId(void) { return s_current ? s_current->id : BACKEND_NONE; }
const Backend *Graphics_current(void) { return s_current; }

// ── forwarders (call through the active row; false when none) ───────────────
bool Graphics_begin(void)   { return s_current && s_current->begin   ? s_current->begin()   : false; }
bool Graphics_end(void)     { return s_current && s_current->end     ? s_current->end()     : false; }
bool Graphics_present(void) { return s_current && s_current->present ? s_current->present() : false; }
bool Graphics_resize(uint32_t w, uint32_t h) {
    return s_current && s_current->resize ? s_current->resize(w, h) : false;
}
bool Graphics_clear(Color color) {
    return s_current && s_current->clear ? s_current->clear(color) : false;
}
bool Graphics_clip(const Rect *rect) {
    return s_current && s_current->clip ? s_current->clip(rect) : false;
}
bool Graphics_fillRect(const Rect *rect, const Brush *brush) {
    return s_current && s_current->fillRect ? s_current->fillRect(rect, brush) : false;
}
bool Graphics_drawImage(const Image *image, const Rect *dst) {
    return s_current && s_current->drawImage ? s_current->drawImage(image, dst) : false;
}
bool Graphics_drawText(const Rect *rect, const char *text, const Brush *brush) {
    return s_current && s_current->drawText ? s_current->drawText(rect, text, brush) : false;
}

// ── the display list ────────────────────────────────────────────────────────
struct DisplayList {
    DrawCmd *items;
    size_t count;
    size_t cap;
};

DisplayList *DisplayList_0(void) {
    DisplayList *dl = calloc(1, sizeof *dl);
    return dl;
}

void DisplayList_free(DisplayList *dl) {
    if (!dl) return;
    free(dl->items);
    free(dl);
}

void DisplayList_clear(DisplayList *dl) {
    if (dl) dl->count = 0;
}

static DrawCmd *dl_push(DisplayList *dl) {
    if (dl->count == dl->cap) {
        dl->cap = dl->cap ? dl->cap * 2 : 64;
        dl->items = realloc(dl->items, dl->cap * sizeof *dl->items);
        if (!dl->items) { dl->cap = 0; dl->count = 0; return NULL; }
    }
    DrawCmd *c = &dl->items[dl->count++];
    memset(c, 0, sizeof *c);
    return c;
}

void DisplayList_rect(DisplayList *dl, Rect dst, const Brush *brush) {
    if (!dl || !brush || Rect_isEmpty(dst)) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    c->kind = CMD_RECT;
    c->dst = dst;
    c->color = brush->color;
    c->radius = brush->radius;
    c->borderColor = brush->border;
    c->border = brush->borderWidth;
}

void DisplayList_image(DisplayList *dl, const Image *image, Rect src, Rect dst) {
    if (!dl || !image || Rect_isEmpty(dst)) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    c->kind = CMD_IMAGE;
    c->dst = dst;
    c->src = src;
    c->image = image;
}

void DisplayList_text(DisplayList *dl, Rect dst, const char *text, Color color) {
    if (!dl || !text || Rect_isEmpty(dst)) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    c->kind = CMD_TEXT;
    c->dst = dst;
    c->text = text;
    c->color = color;
}

void DisplayList_clip(DisplayList *dl, Rect rect) {
    if (!dl) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    c->kind = CMD_CLIP_PUSH;
    c->dst = rect;
}

void DisplayList_unclip(DisplayList *dl) {
    if (!dl) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    c->kind = CMD_CLIP_POP;
}

size_t DisplayList_count(const DisplayList *dl) { return dl ? dl->count : 0; }
const DrawCmd *DisplayList_cmds(const DisplayList *dl) { return dl ? dl->items : NULL; }

bool Graphics_submit(DisplayList *dl) {
    if (!dl || !s_current) return false;
    const DrawCmd *cmds = dl->items;
    for (size_t i = 0; i < dl->count; i++) {
        const DrawCmd *k = &cmds[i];
        switch (k->kind) {
            case CMD_RECT: {
                Brush b = {k->color, k->radius, k->borderColor, k->border};
                if (!Graphics_fillRect(&k->dst, &b)) return false;
                break;
            }
            case CMD_IMAGE: {
                if (!Graphics_drawImage(k->image, &k->dst)) return false;
                break;
            }
            case CMD_TEXT: {
                Brush b = {k->color, 0.0f, 0u, 0.0f};
                if (!Graphics_drawText(&k->dst, k->text, &b)) return false;
                break;
            }
            case CMD_CLIP_PUSH:
                if (!Graphics_clip(&k->dst)) return false;
                break;
            case CMD_CLIP_POP:
                if (!Graphics_clip(NULL)) return false;
                break;
            default:
                break;
        }
    }
    return true;
}

// ── headless CPU (raster) backend ───────────────────────────────────────────
static uint32_t *s_px = NULL;
static int32_t s_w = 0, s_h = 0;
static Rect s_clip = {0, 0, 0, 0};

static uint32_t blend_over(uint32_t dst, uint32_t src) {
    uint32_t sa = src & 0xFFu;
    if (sa == 255u) return src;
    if (sa == 0u) return dst;
    uint32_t ia = 255u - sa;
    uint32_t r = (((src >> 24) & 0xFFu) * sa + ((dst >> 24) & 0xFFu) * ia) / 255u;
    uint32_t g = (((src >> 16) & 0xFFu) * sa + ((dst >> 16) & 0xFFu) * ia) / 255u;
    uint32_t b = (((src >> 8) & 0xFFu) * sa + ((dst >> 8) & 0xFFu) * ia) / 255u;
    uint32_t a = (sa * 255u + ((dst & 0xFFu) * ia)) / 255u;
    return (r << 24) | (g << 16) | (b << 8) | a;
}

bool Raster_configure(uint32_t width, uint32_t height) {
    free(s_px);
    s_px = NULL;
    s_w = (int32_t)width;
    s_h = (int32_t)height;
    if (width == 0 || height == 0) return true;
    s_px = calloc((size_t)width * height, sizeof *s_px);
    s_clip = (Rect){0, 0, (float)width, (float)height};
    return s_px != NULL;
}

const uint32_t *Raster_pixels(void) { return s_px; }

uint32_t Raster_pixelAt(uint32_t x, uint32_t y) {
    if (!s_px || (int32_t)x >= s_w || (int32_t)y >= s_h) return 0u;
    return s_px[(size_t)y * (size_t)s_w + (size_t)x];
}

static bool raster_begin(void) { return s_px != NULL; }
static bool raster_end(void) { return true; }
static bool raster_present(void) { return true; }

static bool raster_resize(uint32_t w, uint32_t h) { return Raster_configure(w, h); }

static bool raster_clear(Color color) {
    if (!s_px) return false;
    for (size_t i = 0; i < (size_t)s_w * (size_t)s_h; i++) s_px[i] = color;
    return true;
}

static bool raster_clip(const Rect *rect) {
    if (!rect) {
        s_clip = (Rect){0, 0, (float)s_w, (float)s_h};
        return true;
    }
    s_clip = *rect;
    return true;
}

// true if (x,y) is inside the rounded-corner shape centered on r
static bool corner_inside(const Rect *r, float radius, float x, float y) {
    float half = (r->w < r->h ? r->w : r->h) * 0.5f;
    if (radius <= 0.0f || radius > half) radius = radius > 0.0f ? half : 0.0f;
    if (radius <= 0.0f) return true;
    float lx = x + 0.5f, ly = y + 0.5f;
    float minx = r->x + radius, maxx = r->x + r->w - radius;
    float miny = r->y + radius, maxy = r->y + r->h - radius;
    float qx = lx < minx ? minx : (lx > maxx ? maxx : lx);
    float qy = ly < miny ? miny : (ly > maxy ? maxy : ly);
    if (qx == lx && qy == ly) return true;   // not in a corner region
    float dx = lx - qx, dy = ly - qy;
    return dx * dx + dy * dy <= radius * radius;
}

static bool raster_fillRect(const Rect *rect, const Brush *brush) {
    if (!s_px || !rect || !brush) return false;
    Rect r = Rect_intersect(*rect, s_clip);
    if (Rect_isEmpty(r)) return true;
    int x0 = (int)r.x < 0 ? 0 : (int)r.x;
    int y0 = (int)r.y < 0 ? 0 : (int)r.y;
    int x1 = (int)(r.x + r.w); if (x1 > s_w) x1 = s_w;
    int y1 = (int)(r.y + r.h); if (y1 > s_h) y1 = s_h;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            if (!corner_inside(rect, brush->radius, (float)x, (float)y)) continue;
            size_t i = (size_t)y * (size_t)s_w + (size_t)x;
            s_px[i] = blend_over(s_px[i], brush->color);
        }
    }
    return true;
}

static bool raster_drawImage(const Image *image, const Rect *dst) {
    // No texture store in the headless core: fill the dst with opaque magenta so
    // a missing texture is loud, and keep the seam honest.
    (void)image;
    if (!s_px || !dst) return false;
    Brush b = {0xFF00FFFFu, 0.0f, 0u, 0.0f};
    return raster_fillRect(dst, &b);
}

static bool raster_drawText(const Rect *rect, const char *text, const Brush *brush) {
    // Text metrics/glyphs are a later slice; the core records intent only.
    (void)rect;
    (void)text;
    (void)brush;
    return true;
}

const Backend *RasterGraphics_row(void) {
    static const Backend row = {
        BACKEND_RASTER, raster_begin,   raster_end,   raster_present,
        raster_resize,     raster_clear,   raster_clip,  raster_fillRect,
        raster_drawImage,  raster_drawText,
    };
    return &row;
}
