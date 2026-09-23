#include "raster/raster_graphics.h"
#include "font/font.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: RasterGraphics
 * ============================================================================
 * The software backend row of the unified Graphics seam. Every verb is CPU code
 * over one owned RGBA8 Image (the framebuffer): clear, scissor, fillRect,
 * drawRect, fillCircle, drawCircle, drawImage. It needs no GPU and no window, so
 * it is the headless/CI row and the pixel-exact reference the GPU rows are
 * checked against.
 *
 * The row is a process-global singleton (the Graphics table carries no self), so
 * its impls read file-local state. Colors arrive packed 0xRRGGBBAA (the Strict
 * 0xRRGGBBAA Color Law: alpha low byte) and blend straight-alpha over the
 * framebuffer. Before resize binds a framebuffer, every verb cold-returns false
 * (the Cold-Strict, Hot-Minimal Validation Law).
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: RasterGraphics (raster/raster_graphics.c)
 * LEVEL: L2 — Behavior (software draw-verb row)
 * ============================================================================
 * SUMMARY:
 *   The software Graphics row: one owned RGBA8 Image + clip state; every verb
 *   paints CPU-side. RasterGraphics_getRow exports the static table.
 *
 * STRUCT FIELDS: none — file-local framebuffer + clip state.
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   unpackRGBA(color, r,g,b,a)          : 0xRRGGBBAA -> normalized floats
 *   putPixel(fb, x, y, r,g,b,a)         : bounds+clip checked straight-alpha over
 *   fillSpan(fb, x0,y0,x1,y1, r,g,b,a)  : axis-aligned filled span
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Private Core Functions: (.c static)
 *   - rasterBegin/End/Present/Clear/Clip/Resize
 *   - rasterFillRect / rasterDrawRect
 *   - rasterFillCircle / rasterDrawCircle
 *   - rasterFillPath / rasterDrawPath  (cold-false until Shape lands)
 *   - rasterDrawText : glyph coverage through clip-aware, opacity-aware putPixel
 *   - rasterDrawImage
 * ============================================================================
 */

// --- Framebuffer + clip state (process-global singleton) ---
static Image *s_fb = nullptr;      // owned RGBA8 framebuffer
static uint32_t s_w = 0;           // drawable extent, native px
static uint32_t s_h = 0;
static bool s_clipEnabled = false;
static float s_clipX = 0.0f, s_clipY = 0.0f, s_clipW = 0.0f, s_clipH = 0.0f;

// 0xRRGGBBAA -> normalized floats (alpha low byte).
static void unpackRGBA(uint32_t color, float *r, float *g, float *b, float *a) {
    *r = (float) ((color >> 24) & 0xFFu) / 255.0f;
    *g = (float) ((color >> 16) & 0xFFu) / 255.0f;
    *b = (float) ((color >> 8) & 0xFFu) / 255.0f;
    *a = (float) (color & 0xFFu) / 255.0f;
}

// Straight-alpha over, bounds + clip checked. No-op off-target or clipped out.
static void putPixel(Image *fb, int x, int y, float r, float g, float b, float a) {
    if (fb == nullptr || x < 0 || y < 0 || (uint32_t) x >= s_w || (uint32_t) y >= s_h)
        return;
    if (a <= 0.0f)
        return;
    if (s_clipEnabled) {
        if ((float) x < s_clipX || (float) y < s_clipY
            || (float) x >= s_clipX + s_clipW || (float) y >= s_clipY + s_clipH)
            return;
    }
    uint8_t *px = Image_pixels(fb);
    if (px == nullptr)
        return;
    uint8_t *p = px + ((size_t) y * s_w + (size_t) x) * 4u;
    float da = (float) p[3] / 255.0f;
    p[0] = (uint8_t) ((r * a + ((float) p[0] / 255.0f) * (1.0f - a)) * 255.0f + 0.5f);
    p[1] = (uint8_t) ((g * a + ((float) p[1] / 255.0f) * (1.0f - a)) * 255.0f + 0.5f);
    p[2] = (uint8_t) ((b * a + ((float) p[2] / 255.0f) * (1.0f - a)) * 255.0f + 0.5f);
    p[3] = (uint8_t) ((a + da * (1.0f - a)) * 255.0f + 0.5f);
}

// CONSTRUCTORS / LIFECYCLE

bool RasterGraphics_resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return false;
    if (s_fb != nullptr && s_w == width && s_h == height)
        return true;
    Image *fresh = Image_2(width, height);
    if (fresh == nullptr)
        return false;
    Image_destroy(s_fb);
    s_fb = fresh;
    s_w = width;
    s_h = height;
    s_clipEnabled = false;
    return true;
}

bool RasterGraphics_setFramebuffer(Image *fb, uint32_t width, uint32_t height) {
    if (fb == nullptr || width == 0 || height == 0)
        return false;
    if (Image_width(fb) != width || Image_height(fb) != height)
        return false;
    s_fb = fb;
    s_w = width;
    s_h = height;
    return true;
}

void RasterGraphics_shutdown(void) {
    Image_destroy(s_fb);
    s_fb = nullptr;
    s_w = 0;
    s_h = 0;
    s_clipEnabled = false;
}

Image *RasterGraphics_getFramebuffer(void) { return s_fb; }
uint32_t RasterGraphics_getWidth(void) { return s_w; }
uint32_t RasterGraphics_getHeight(void) { return s_h; }
bool RasterGraphics_isReady(void) { return s_fb != nullptr; }

// CORE FUNCTIONS (PRIVATE ROW VERBS)

static bool rasterBegin(void) { return s_fb != nullptr; }
static bool rasterEnd(void) { return s_fb != nullptr; }
static bool rasterPresent(void) { return s_fb != nullptr; } // CPU pixels are already "on screen".

static bool rasterResize(uint32_t width, uint32_t height) {
    return RasterGraphics_resize(width, height);
}

static bool rasterClear(uint32_t color) {
    if (s_fb == nullptr)
        return false;
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
    unpackRGBA(color, &r, &g, &b, &a);
    uint8_t *px = Image_pixels(s_fb);
    if (px == nullptr)
        return false;
    uint8_t cr = (uint8_t) (r * 255.0f + 0.5f);
    uint8_t cg = (uint8_t) (g * 255.0f + 0.5f);
    uint8_t cb = (uint8_t) (b * 255.0f + 0.5f);
    uint8_t ca = (uint8_t) (a * 255.0f + 0.5f);
    for (size_t i = 0; i < (size_t) s_w * s_h; i++) {
        px[i * 4 + 0] = cr;
        px[i * 4 + 1] = cg;
        px[i * 4 + 2] = cb;
        px[i * 4 + 3] = ca;
    }
    return true;
}

static bool rasterClip(const Rectangle *rect) {
    if (rect == nullptr) {
        s_clipEnabled = false;
        return true;
    }
    s_clipX = (*rect).x;
    s_clipY = (*rect).y;
    s_clipW = (*rect).width;
    s_clipH = (*rect).height;
    s_clipEnabled = true;
    return true;
}

static bool rasterFillRect(const Rectangle *rect, const Brush *brush) {
    if (s_fb == nullptr || rect == nullptr || brush == nullptr)
        return false;
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
    unpackRGBA(Brush_getColor(brush), &r, &g, &b, &a);
    a *= Brush_getOpacity(brush);
    // Bound work to the visible target before converting logical extents to ints.
    // Large virtual documents must not cause full-document raster loops.
    float left = s_clipEnabled ? fmaxf(0.0f, s_clipX) : 0.0f;
    float top = s_clipEnabled ? fmaxf(0.0f, s_clipY) : 0.0f;
    float right = s_clipEnabled ? fminf((float) s_w, s_clipX + s_clipW) : (float) s_w;
    float bottom = s_clipEnabled ? fminf((float) s_h, s_clipY + s_clipH) : (float) s_h;
    int x0 = (int) fminf((float) s_w, fmaxf(left, floorf((*rect).x + 0.5f)));
    int y0 = (int) fminf((float) s_h, fmaxf(top, floorf((*rect).y + 0.5f)));
    int x1 = (int) fmaxf(0.0f, fminf(right, floorf((*rect).x + (*rect).width + 0.5f)));
    int y1 = (int) fmaxf(0.0f, fminf(bottom, floorf((*rect).y + (*rect).height + 0.5f)));
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            putPixel(s_fb, x, y, r, g, b, a);
    return true;
}

static bool rasterDrawRect(const Rectangle *rect, const Stroke *stroke) {
    if (s_fb == nullptr || rect == nullptr || stroke == nullptr)
        return false;
    int w = (int) ceilf(Stroke_getWidth(stroke));
    if (w < 1)
        w = 1;
    float x = (*rect).x, y = (*rect).y, rw = (*rect).width, rh = (*rect).height;
    Rectangle top = { x, y, rw, (float) w };
    Rectangle bottom = { x, y + rh - (float) w, rw, (float) w };
    Rectangle left = { x, y, (float) w, rh };
    Rectangle right = { x + rw - (float) w, y, (float) w, rh };
    Brush b = { Stroke_getColor(stroke), 1.0f };
    return rasterFillRect(&top, &b) && rasterFillRect(&bottom, &b)
        && rasterFillRect(&left, &b) && rasterFillRect(&right, &b);
}

static bool rasterFillCircle(float cx, float cy, float radius, const Brush *brush) {
    if (s_fb == nullptr || brush == nullptr || radius <= 0.0f)
        return false;
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
    unpackRGBA(Brush_getColor(brush), &r, &g, &b, &a);
    a *= Brush_getOpacity(brush);
    float r2 = radius * radius;
    int x0 = (int) floorf(cx - radius);
    int y0 = (int) floorf(cy - radius);
    int x1 = (int) ceilf(cx + radius);
    int y1 = (int) ceilf(cy + radius);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float) x + 0.5f - cx;
            float dy = (float) y + 0.5f - cy;
            if (dx * dx + dy * dy <= r2)
                putPixel(s_fb, x, y, r, g, b, a);
        }
    }
    return true;
}

static bool rasterDrawCircle(float cx, float cy, float radius, const Stroke *stroke) {
    if (s_fb == nullptr || stroke == nullptr || radius <= 0.0f)
        return false;
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
    unpackRGBA(Stroke_getColor(stroke), &r, &g, &b, &a);
    float half = Stroke_getWidth(stroke) * 0.5f;
    if (half < 0.5f)
        half = 0.5f;
    float inner = radius - half;
    float outer = radius + half;
    float in2 = inner * inner;
    float out2 = outer * outer;
    int x0 = (int) floorf(cx - outer);
    int y0 = (int) floorf(cy - outer);
    int x1 = (int) ceilf(cx + outer);
    int y1 = (int) ceilf(cy + outer);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float) x + 0.5f - cx;
            float dy = (float) y + 0.5f - cy;
            float d2 = dx * dx + dy * dy;
            if (d2 >= in2 && d2 <= out2)
                putPixel(s_fb, x, y, r, g, b, a);
        }
    }
    return true;
}

static bool rasterFillPath(const Shape *shape, const Brush *brush) {
    (void) shape;
    (void) brush;
    return false; // vector paths land with Shape.
}

static bool rasterDrawPath(const Shape *shape, const Stroke *stroke) {
    (void) shape;
    (void) stroke;
    return false;
}

static bool rasterDrawImage(const Image *image, const Rectangle *dst) {
    if (s_fb == nullptr || image == nullptr || dst == nullptr)
        return false;
    const uint8_t *src = Image_pixels(image);
    if (src == nullptr)
        return false;
    uint32_t sw = Image_width(image);
    uint32_t sh = Image_height(image);
    if (sw == 0 || sh == 0)
        return false;
    int dw = (int) floorf((*dst).width + 0.5f);
    int dh = (int) floorf((*dst).height + 0.5f);
    if (dw <= 0 || dh <= 0)
        return false;
    int dx0 = (int) floorf((*dst).x + 0.5f);
    int dy0 = (int) floorf((*dst).y + 0.5f);
    for (int y = 0; y < dh; y++) {
        uint32_t sy = (uint32_t) ((int64_t) y * sh / dh);
        if (sy >= sh)
            sy = sh - 1;
        for (int x = 0; x < dw; x++) {
            uint32_t sx = (uint32_t) ((int64_t) x * sw / dw);
            if (sx >= sw)
                sx = sw - 1;
            const uint8_t *sp = src + ((size_t) sy * sw + sx) * 4u;
            putPixel(s_fb, dx0 + x, dy0 + y,
                     (float) sp[0] / 255.0f, (float) sp[1] / 255.0f,
                     (float) sp[2] / 255.0f, (float) sp[3] / 255.0f);
        }
    }
    return true;
}

// The row: one static table, registered once at boot.
static bool rasterDrawText(const Rectangle *rect, const char *text, const Brush *brush) {
    if (s_fb == nullptr || rect == nullptr || text == nullptr || brush == nullptr)
        return false;
    float r, g, b, a;
    unpackRGBA(Brush_getColor(brush), &r, &g, &b, &a);
    a *= Brush_getOpacity(brush);
    int startX = (int) (*rect).x;
    int x = startX;
    int y = (int) (*rect).y;
    int wrap = (int) (*rect).width;
    for (const char *p = text; *p; p++) {
        int advance = Font_advance(*p);
        if (*p == '\n') {
            x = startX;
            y += Font_lineHeight();
            continue;
        }
        if (wrap > 0 && x > startX && x - startX + advance > wrap) {
            x = startX;
            y += Font_lineHeight();
        }
        const uint8_t *glyph = Font_glyph(*p);
        if (glyph) {
            // The built-in font's fixed 8-by-8 glyph format, not a row limit.
            for (int row = 0; row < 8; row++)
                for (int col = 0; col < 8; col++)
                    if (glyph[row] & (0x80u >> col))
                        putPixel(s_fb, x + col, y + row, r, g, b, a);
        }
        x += advance;
    }
    return true;
}

static const Graphics kRasterRow = {
    .backendId = LANG_BACKEND_RASTER,
    .begin = rasterBegin,
    .end = rasterEnd,
    .present = rasterPresent,
    .resize = rasterResize,
    .clear = rasterClear,
    .clip = rasterClip,
    .fillRect = rasterFillRect,
    .drawRect = rasterDrawRect,
    .fillCircle = rasterFillCircle,
    .drawCircle = rasterDrawCircle,
    .fillPath = rasterFillPath,
    .drawPath = rasterDrawPath,
    .drawImage = rasterDrawImage,
    .drawText = rasterDrawText,
};

const Graphics *RasterGraphics_getRow(void) {
    return &kRasterRow;
}
