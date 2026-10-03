#include "graphics/graphics.h"

#include "image.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// graphvex R3 — graphics/graphics.c
// The registry, the slim forwarders, the display-list streamer, and the
// headless CPU (raster) backend. No Vulkan here: the GPU backend lands behind
// the same row later. DisplayList reserves reusable clip-stack scratch while
// recording. Submission intersects every enclosing mask and restores caller
// scope; mixed rounded intersections use coalesced pixel-center scissors on
// both backends, retaining the original quad geometry and texture coordinates.
// Raster fill draws an inside border as well as the background.

// ── backend registry ────────────────────────────────────────────────────────
#define MAX_BACKENDS 8
static const Backend *s_rows[MAX_BACKENDS];
static int s_rowCount = 0;
static const Backend *s_current = NULL;
static DrawCmd s_baseClip;
static bool s_hasBaseClip = false; // explicit clip outside a display-list scope

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
        if ((*s_rows[i]).id == backendId) { s_current = s_rows[i]; s_hasBaseClip = false; return true; }
    }
    // convenience: the raster backend self-registers on first use
    if (backendId == BACKEND_RASTER) {
        Graphics_register(RasterGraphics_row());
        for (int i = 0; i < s_rowCount; i++)
            if ((*s_rows[i]).id == backendId) { s_current = s_rows[i]; s_hasBaseClip = false; return true; }
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
    bool ok = s_current && (*s_current).resize ? (*s_current).resize(w, h) : false;
    if (ok) s_hasBaseClip = false;
    return ok;
}
bool Graphics_clear(Color color) {
    return s_current && (*s_current).clear ? (*s_current).clear(color) : false;
}
bool Graphics_clipRounded(const Rect *rect, float radius) {
    if (!s_current || !(*s_current).clip || !(*s_current).clip(rect, radius)) return false;
    s_hasBaseClip = rect != NULL;
    if (rect) { s_baseClip.dst = *rect; s_baseClip.radius = fmaxf(radius, 0); }
    return true;
}
bool Graphics_clip(const Rect *rect) { return Graphics_clipRounded(rect, 0.0f); }
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
    size_t *clipStack;       // command indices; reusable submission scratch
    size_t clipCap, clipDepth; // reserved nesting depth and recording depth
};

DisplayList *DisplayList_0(void) {
    DisplayList *dl = calloc(1, sizeof *dl);
    return dl;
}

void DisplayList_free(DisplayList *dl) {
    if (!dl) return;
    free((*dl).items);
    free((*dl).clipStack);
    free(dl);
}

void DisplayList_clear(DisplayList *dl) {
    if (dl) { (*dl).count = 0; (*dl).clipDepth = 0; }
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
    if ((*dl).clipDepth == (*dl).clipCap) {
        size_t cap = (*dl).clipCap ? (*dl).clipCap * 2 : 8;
        size_t *grown = realloc((*dl).clipStack, cap * sizeof *grown);
        if (!grown) return;
        (*dl).clipStack = grown;
        (*dl).clipCap = cap;
    }
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    (*dl).clipDepth++;
    (*c).kind = CMD_CLIP_PUSH;
    (*c).dst = rect;
    (*c).radius = radius > 0.0f ? radius : 0.0f;   // 0 = rectangular scissor
}

void DisplayList_unclip(DisplayList *dl) {
    if (!dl) return;
    DrawCmd *c = dl_push(dl);
    if (!c) return;
    (*c).kind = CMD_CLIP_POP;
    if ((*dl).clipDepth > 0) (*dl).clipDepth--;
}

size_t DisplayList_count(const DisplayList *dl) { return dl ? (*dl).count : 0; }
const DrawCmd *DisplayList_cmds(const DisplayList *dl) { return dl ? (*dl).items : NULL; }

// Submit original geometry; scissors must never resize rounded boxes or UVs.
static bool submit_draw(const DrawCmd *cmd) {
    Brush brush = {(*cmd).color, (*cmd).radius, (*cmd).borderColor, (*cmd).border, (*cmd).blur};
    switch ((*cmd).kind) {
        case CMD_RECT: return Graphics_fillRect(&(*cmd).dst, &brush);
        case CMD_IMAGE: return Graphics_drawImage((*cmd).image, &(*cmd).dst);
        case CMD_TEXT: return Graphics_drawText(&(*cmd).dst, (*cmd).text, &brush);
        default: return false;
    }
}

static bool submit_clip(const Rect *rect, float radius) {
    return (*s_current).clip && (*s_current).clip(rect, radius);
}

static const DrawCmd *submit_mask(DisplayList *dl, size_t index) {
    if (s_hasBaseClip) {
        if (index == 0) return &s_baseClip;
        --index;
    }
    return &(*dl).items[(*dl).clipStack[index]];
}

static bool submit_span(const DrawCmd *cmd, Rect span) {
    return Rect_isEmpty(span) || (submit_clip(&span, 0) && submit_draw(cmd));
}

static bool submit_masks(DisplayList *dl, const DrawCmd *cmd, size_t depth) {
    depth += s_hasBaseClip ? 1 : 0;
    if (depth == 0)
        return submit_clip(NULL, 0) && submit_draw(cmd);
    if (depth == 1) {
        const DrawCmd *mask = submit_mask(dl, 0);
        return submit_clip(&(*mask).dst, (*mask).radius) && submit_draw(cmd);
    }
    Rect bounds = (*cmd).dst;
    bool rounded = false;
    for (size_t i = 0; i < depth; ++i) {
        const DrawCmd *mask = submit_mask(dl, i);
        bounds = Rect_intersect(bounds, (*mask).dst);
        rounded |= (*mask).radius > 0.0f;
    }
    if (Rect_isEmpty(bounds)) return true;
    if (!rounded) return submit_clip(&bounds, 0) && submit_draw(cmd);
    if (!isfinite(bounds.x) || !isfinite(bounds.y) ||
        !isfinite(bounds.w) || !isfinite(bounds.h)) return false;

    // Rounded rectangles are convex: intersect the horizontal interval each
    // ancestor admits at the pixel center. Coalesce identical adjacent rows.
    // Cost is restricted to this quad's clipped bounds, not the whole window.
    Rect span = {0};
    float end = ceilf(bounds.y + bounds.h - 0.5f);
    for (float y = ceilf(bounds.y - 0.5f); y < end; y += 1.0f) {
        float left = bounds.x, right = bounds.x + bounds.w;
        for (size_t i = 0; i < depth; ++i) {
            const DrawCmd *mask = submit_mask(dl, i);
            Rect r = (*mask).dst;
            float radius = fminf((*mask).radius, fminf(r.w, r.h) * 0.5f);
            if (radius <= 0.0f) continue;
            float qy = fabsf(y + 0.5f - r.y - r.h * 0.5f) - (r.h * 0.5f - radius);
            float inset = qy > 0.0f ? radius - sqrtf(fmaxf(0.0f, radius * radius - qy * qy)) : 0.0f;
            left = fmaxf(left, r.x + inset);
            right = fminf(right, r.x + r.w - inset);
        }
        float x0 = ceilf(left - 0.5f), x1 = ceilf(right - 0.5f);
        if (x1 > x0 && span.h > 0 && span.x == x0 && span.w == x1 - x0 && span.y + span.h == y) {
            span.h += 1;
        } else {
            if (!submit_span(cmd, span)) return false;
            span = (Rect){x0, y, fmaxf(x1 - x0, 0), 1};
        }
    }
    return submit_span(cmd, span);
}

bool Graphics_submit(DisplayList *dl) {
    if (!dl || !s_current) return false;
    size_t depth = 0;
    bool ok = true;
    for (size_t i = 0; i < (*dl).count && ok; ++i) {
        const DrawCmd *cmd = &(*dl).items[i];
        if ((*cmd).kind == CMD_CLIP_PUSH) {
            if (depth >= (*dl).clipCap) { ok = false; break; }
            (*dl).clipStack[depth++] = i;
        } else if ((*cmd).kind == CMD_CLIP_POP) {
            if (depth == 0) { ok = false; break; }
            --depth;
        } else {
            ok = submit_masks(dl, cmd, depth);
        }
    }
    // Restore the caller's explicit clip on success and error; list-local
    // masks never leak across submissions or overwrite an enclosing scope.
    bool reset = submit_clip(s_hasBaseClip ? &s_baseClip.dst : NULL, s_baseClip.radius);
    return ok && depth == 0 && reset;
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
            float stroke = (*brush).borderWidth > 0.0f ? (*brush).borderWidth : 0.0f;
            float borderMix = stroke > 0.0f ? fminf(fmaxf(d + stroke + 0.5f, 0.0f), 1.0f) : 0.0f;
            float borderAlpha = (float) Color_alpha((*brush).border) / 255.0f * borderMix;
            Color fill = (*brush).color, border = (*brush).border;
            uint32_t red = (uint32_t) ((float) Color_red(fill) * (1 - borderAlpha) + (float) Color_red(border) * borderAlpha + 0.5f);
            uint32_t green = (uint32_t) ((float) Color_green(fill) * (1 - borderAlpha) + (float) Color_green(border) * borderAlpha + 0.5f);
            uint32_t blue = (uint32_t) ((float) Color_blue(fill) * (1 - borderAlpha) + (float) Color_blue(border) * borderAlpha + 0.5f);
            uint32_t sa = (uint32_t) (((float) Color_alpha(fill) * (1 - borderAlpha) + (float) Color_alpha(border) * borderAlpha) * a + 0.5f);
            if (sa == 0u) continue;
            uint32_t src = COLOR_RGBA(red, green, blue, sa);
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
