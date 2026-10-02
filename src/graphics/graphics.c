#include "graphics/graphics.h"

#include "image.h"

#include <math.h>
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
    if (!row || (*row).id == BACKEND_NONE) return false;
    for (int i = 0; i < s_rowCount; i++) {
        if ((*s_rows[i]).id == (*row).id) { s_rows[i] = row; return true; }
    }
    if (s_rowCount >= MAX_BACKENDS) return false;
    s_rows[s_rowCount++] = row;
    return true;
}

bool Graphics_use(uint32_t backendId) {
    for (int i = 0; i < s_rowCount; i++) {
        if ((*s_rows[i]).id == backendId) { s_current = s_rows[i]; return true; }
    }
    // convenience: the raster backend self-registers on first use
    if (backendId == BACKEND_RASTER) {
        Graphics_register(RasterGraphics_row());
        for (int i = 0; i < s_rowCount; i++)
            if ((*s_rows[i]).id == backendId) { s_current = s_rows[i]; return true; }
    }
    return false;
}

uint32_t Graphics_backendId(void) { return s_current ? (*s_current).id : BACKEND_NONE; }
const Backend *Graphics_current(void) { return s_current; }

// ── forwarders (call through the active row; false when none) ───────────────
bool Graphics_begin(void)   { return s_current && (*s_current).begin   ? (*s_current).begin()   : false; }
bool Graphics_end(void)     { return s_current && (*s_current).end     ? (*s_current).end()     : false; }
bool Graphics_present(void) { return s_current && (*s_current).present ? (*s_current).present() : false; }
bool Graphics_resize(uint32_t w, uint32_t h) {
    return s_current && (*s_current).resize ? (*s_current).resize(w, h) : false;
}
bool Graphics_clear(Color color) {
    return s_current && (*s_current).clear ? (*s_current).clear(color) : false;
}
bool Graphics_clip(const Rect *rect) {
    return s_current && (*s_current).clip ? (*s_current).clip(rect, 0.0f) : false;
}
bool Graphics_clipRounded(const Rect *rect, float radius) {
    return s_current && (*s_current).clip ? (*s_current).clip(rect, radius) : false;
}
bool Graphics_fillRect(const Rect *rect, const Brush *brush) {
    return s_current && (*s_current).fillRect ? (*s_current).fillRect(rect, brush) : false;
}
bool Graphics_drawImage(const Image *image, const Rect *dst) {
    return s_current && (*s_current).drawImage ? (*s_current).drawImage(image, dst) : false;
}
bool Graphics_drawText(const Rect *rect, const char *text, const Brush *brush) {
    return s_current && (*s_current).drawText ? (*s_current).drawText(rect, text, brush) : false;
}
bool Graphics_capture(Image *dest) {
    return s_current && (*s_current).capture ? (*s_current).capture(dest) : false;
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
    free((*dl).items);
    free(dl);
}

void DisplayList_clear(DisplayList *dl) {
    if (dl) (*dl).count = 0;
}

static DrawCmd *dl_push(DisplayList *dl) {
    if ((*dl).count == (*dl).cap) {
        (*dl).cap = (*dl).cap ? (*dl).cap * 2 : 64;
        (*dl).items = realloc((*dl).items, (*dl).cap * sizeof *(*dl).items);
        if (!(*dl).items) { (*dl).cap = 0; (*dl).count = 0; return NULL; }
    }
    DrawCmd *c = &(*dl).items[(*dl).count++];
    memset(c, 0, sizeof *c);
    return c;
}

void DisplayList_rect(DisplayList *dl, Rect dst, const Brush *brush) {
    if (!dl || !brush || Rect_isEmpty(dst)) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    (*c).kind = CMD_RECT;
    (*c).dst = dst;
    (*c).color = (*brush).color;
    (*c).radius = (*brush).radius;
    (*c).borderColor = (*brush).border;
    (*c).border = (*brush).borderWidth;
    (*c).blur = (*brush).blur;
}

void DisplayList_image(DisplayList *dl, const Image *image, Rect src, Rect dst) {
    if (!dl || !image || Rect_isEmpty(dst)) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    (*c).kind = CMD_IMAGE;
    (*c).dst = dst;
    (*c).src = src;
    (*c).image = image;
}

void DisplayList_text(DisplayList *dl, Rect dst, const char *text, Color color) {
    if (!dl || !text || Rect_isEmpty(dst)) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    (*c).kind = CMD_TEXT;
    (*c).dst = dst;
    (*c).text = text;
    (*c).color = color;
}

void DisplayList_clip(DisplayList *dl, Rect rect) {
    DisplayList_clipRounded(dl, rect, 0.0f);
}

void DisplayList_clipRounded(DisplayList *dl, Rect rect, float radius) {
    if (!dl) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    (*c).kind = CMD_CLIP_PUSH;
    (*c).dst = rect;
    (*c).radius = radius > 0.0f ? radius : 0.0f;   // 0 = rectangular scissor
}

void DisplayList_unclip(DisplayList *dl) {
    if (!dl) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    (*c).kind = CMD_CLIP_POP;
}

size_t DisplayList_count(const DisplayList *dl) { return dl ? (*dl).count : 0; }
const DrawCmd *DisplayList_cmds(const DisplayList *dl) { return dl ? (*dl).items : NULL; }

bool Graphics_submit(DisplayList *dl) {
    if (!dl || !s_current) return false;
    const DrawCmd *cmds = (*dl).items;
    for (size_t i = 0; i < (*dl).count; i++) {
        const DrawCmd *k = &cmds[i];
        switch ((*k).kind) {
            case CMD_RECT: {
                Brush b = {(*k).color, (*k).radius, (*k).borderColor, (*k).border, (*k).blur};
                if (!Graphics_fillRect(&(*k).dst, &b)) return false;
                break;
            }
            case CMD_IMAGE: {
                if (!Graphics_drawImage((*k).image, &(*k).dst)) return false;
                break;
            }
            case CMD_TEXT: {
                Brush b = {(*k).color, 0.0f, 0u, 0.0f, 0.0f};
                if (!Graphics_drawText(&(*k).dst, (*k).text, &b)) return false;
                break;
            }
            case CMD_CLIP_PUSH:
                if (!Graphics_clipRounded(&(*k).dst, (*k).radius)) return false;
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
static int32_t s_capW = 0, s_capH = 0;   // allocation; row stride == s_capW
static int32_t s_w = 0, s_h = 0;         // logical viewport (the scissor region)
static Rect s_clip = {0, 0, 0, 0};
static float s_clipRadius = 0.0f;   // > 0 = rounded mask on the clip rect

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
    // GROW-ONLY. The framebuffer is allocated once and only ever grows, so a
    // resize is pure scissoring: no realloc, no refill, ever. The row stride is
    // the allocation width (s_capW), independent of the logical width (s_w).
    if (width == 0 || height == 0) {
        s_w = 0;
        s_h = 0;
        s_clip = (Rect){0, 0, 0, 0};
        return true;
    }
    if (s_px && width <= (uint32_t)s_capW && height <= (uint32_t)s_capH) {
        s_w = (int32_t)width;
        s_h = (int32_t)height;
        s_clip = (Rect){0, 0, (float)width, (float)height};
        return true;
    }
    uint32_t nw = s_capW > 0 ? (uint32_t)s_capW : width;
    uint32_t nh = s_capH > 0 ? (uint32_t)s_capH : height;
    while (nw < width) nw += nw / 2 + 64;   // amortized growth
    while (nh < height) nh += nh / 2 + 64;
    uint32_t *grown = realloc(s_px, (size_t)nw * (size_t)nh * sizeof *grown);
    if (!grown) return false;
    s_px = grown;
    s_capW = (int32_t)nw;
    s_capH = (int32_t)nh;
    s_w = (int32_t)width;
    s_h = (int32_t)height;
    s_clip = (Rect){0, 0, (float)width, (float)height};
    return true;
}

const uint32_t *Raster_pixels(void) { return s_px; }

uint32_t Raster_pixelAt(uint32_t x, uint32_t y) {
    if (!s_px || (int32_t)x >= s_w || (int32_t)y >= s_h) return 0u;
    return s_px[(size_t)y * (size_t)s_capW + (size_t)x];
}

static bool raster_begin(void) { return s_px != NULL; }
static bool raster_end(void) { return true; }
static bool raster_present(void) { return true; }

static bool raster_resize(uint32_t w, uint32_t h) { return Raster_configure(w, h); }

static bool raster_clear(Color color) {
    if (!s_px) return false;
    for (int y = 0; y < s_h; y++) {
        uint32_t *row = s_px + (size_t)y * (size_t)s_capW;
        for (int x = 0; x < s_w; x++) row[x] = color;
    }
    return true;
}

static bool raster_clip(const Rect *rect, float radius) {
    if (!rect) {
        s_clip = (Rect){0, 0, (float)s_w, (float)s_h};
        s_clipRadius = 0.0f;
        return true;
    }
    s_clip = *rect;
    s_clipRadius = radius > 0.0f ? radius : 0.0f;
    return true;
}

// signed distance to a rounded box centred at the origin (negative = inside)
static float sd_round_box(float px, float py, float hw, float hh, float r) {
    float qx = fabsf(px) - hw + r;
    float qy = fabsf(py) - hh + r;
    float mx = qx > 0.0f ? qx : 0.0f;
    float my = qy > 0.0f ? qy : 0.0f;
    float inside = qx > qy ? qx : qy;
    if (inside > 0.0f) inside = 0.0f;
    return inside + sqrtf(mx * mx + my * my) - r;
}

// The quad ALREADY includes the blur margin, so the SHAPE is the quad inset by
// `blur`; coverage falls off smoothly over `blur` px. blur == 0 keeps the sharp
// 1px-antialiased edge. The element's own rect is untouched — only the paint
// area grew.
static bool raster_fillRect(const Rect *rect, const Brush *brush) {
    if (!s_px || !rect || !brush) return false;
    float blur = (*brush).blur > 0.0f ? (*brush).blur : 0.0f;
    float cx = (*rect).x + (*rect).w * 0.5f;
    float cy = (*rect).y + (*rect).h * 0.5f;
    float hw = (*rect).w * 0.5f - blur;
    float hh = (*rect).h * 0.5f - blur;
    if (hw < 0.0f) hw = 0.0f;
    if (hh < 0.0f) hh = 0.0f;
    float r = (*brush).radius;
    float rmax = hw < hh ? hw : hh;
    if (r > rmax) r = rmax;
    if (r < 0.0f) r = 0.0f;

    Rect clipr = Rect_intersect(*rect, s_clip);
    if (Rect_isEmpty(clipr)) return true;
    int x0 = (int)clipr.x < 0 ? 0 : (int)clipr.x;
    int y0 = (int)clipr.y < 0 ? 0 : (int)clipr.y;
    int x1 = (int)(clipr.x + clipr.w); if (x1 > s_w) x1 = s_w;
    int y1 = (int)(clipr.y + clipr.h); if (y1 > s_h) y1 = s_h;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            if (s_clipRadius > 0.0f) {
                // the active clip may be a rounded mask (a parent's corner radius)
                float ccx = s_clip.x + s_clip.w * 0.5f;
                float ccy = s_clip.y + s_clip.h * 0.5f;
                float chw = s_clip.w * 0.5f, chh = s_clip.h * 0.5f;
                float cr = s_clipRadius;
                float cmax = chw < chh ? chw : chh;
                if (cr > cmax) cr = cmax;
                if (sd_round_box((float)x + 0.5f - ccx, (float)y + 0.5f - ccy, chw, chh, cr) > 0.0f)
                    continue;
            }
            float d = sd_round_box((float)x + 0.5f - cx, (float)y + 0.5f - cy, hw, hh, r);
            float a;
            if (blur > 0.0f) {
                // falloff centred on the shape edge: 1 inside, 0.5 at the edge,
                // 0 at +blur — a bleed, not a full-opacity ring. A wider blur
                // spreads the same panel over more pixels, so its colour weakens.
                if (d <= -blur) a = 1.0f;
                else if (d >= blur) continue;
                else { float t = (d + blur) / (2.0f * blur); a = 1.0f - t * t * (3.0f - 2.0f * t); }
                a *= 24.0f / (24.0f + blur);
            } else {
                a = 0.5f - d;
                if (a <= 0.0f) continue;
                if (a > 1.0f) a = 1.0f;
            }
            uint32_t sa = (uint32_t)((float)((*brush).color & 0xFFu) * a + 0.5f);
            if (sa == 0u) continue;
            uint32_t src = ((*brush).color & 0xFFFFFF00u) | sa;
            size_t i = (size_t)y * (size_t)s_capW + (size_t)x;
            s_px[i] = blend_over(s_px[i], src);
        }
    }
    return true;
}

static bool raster_drawImage(const Image *image, const Rect *dst) {
    // No texture store in the headless core: fill the dst with opaque magenta so
    // a missing texture is loud, and keep the seam honest.
    (void)image;
    if (!s_px || !dst) return false;
    Brush b = {0xFF00FFFFu, 0.0f, 0u, 0.0f, 0.0f};
    return raster_fillRect(dst, &b);
}

static bool raster_drawText(const Rect *rect, const char *text, const Brush *brush) {
    // Text metrics/glyphs are a later slice; the core records intent only.
    (void)rect;
    (void)text;
    (void)brush;
    return true;
}

// screenshot: copy the framebuffer into an Image (RGBA8, 0xRRGGBBAA bytes)
static bool raster_capture(Image *dest) {
    if (!dest || !s_px) return false;
    if (!Image_ensureShadow(dest, (uint32_t)s_w, (uint32_t)s_h)) return false;
    uint8_t *out = Image_pixels(dest);
    if (!out) return false;
    size_t outStride = Image_stride(dest);
    for (int y = 0; y < s_h; y++) {
        const uint32_t *row = s_px + (size_t)y * (size_t)s_capW;
        uint8_t *orow = out + (size_t)y * outStride;
        for (int x = 0; x < s_w; x++) {
            uint32_t c = row[x];
            orow[x * 4 + 0] = (uint8_t)((c >> 24) & 0xFFu);
            orow[x * 4 + 1] = (uint8_t)((c >> 16) & 0xFFu);
            orow[x * 4 + 2] = (uint8_t)((c >> 8) & 0xFFu);
            orow[x * 4 + 3] = (uint8_t)(c & 0xFFu);
        }
    }
    return true;
}

const Backend *RasterGraphics_row(void) {
    static const Backend row = {
        BACKEND_RASTER, raster_begin,   raster_end,     raster_present,
        raster_resize,  raster_clear,   raster_clip,    raster_fillRect,
        raster_drawImage, raster_drawText, raster_capture,
    };
    return &row;
}
