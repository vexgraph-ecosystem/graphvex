#include "raster/raster_graphics.h"

#include <float.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"
#include "buffer/buffer.h"
#include "graphics/graphics.h"
#include "lang/rect/rectangle.h"
#include "oop/type.h"
#include "paint/brush.h"
#include "paint/stroke.h"
#include "vector/shape.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: RasterGraphics
 * ============================================================================
 * Software rasterization backend row fulfilling the unified Graphics seam.
 * Executes drawing primitives, clipping, and blits directly onto CPU host memory
 * via an arena-backed RGBA8 pixel buffer in strict compliance with the
 * Unified Graphics Abstraction Law and the Strict 0xRRGGBBAA Color Law.
 *
 * All color manipulation adheres monotonically to 32-bit packed 0xRRGGBBAA:
 * channel 0 (red) extracts from bits 24..31, channel 1 (green) from bits 16..23,
 * channel 2 (blue) from bits 8..15, and channel 3 (alpha) from bits 0..7.
 * RasterGraphics serves as the software reference standard that all hardware-
 * accelerated Vulkan and Metal backends must mirror.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: RasterGraphics (raster/raster_graphics.c — defined in raster/raster_graphics.h)
 * LEVEL: L2 — Behavior (CPU software rasterizer)
 * ============================================================================
 * RasterGraphics is the software row of the unified Graphics seam: every
 * verb rasterizes into an owned RGBA8 buffer (the DGraphics backend).
 * It is the reference implementation — the pixel output the GPU rows must
 * match — and the headless test surface (no GPU, CI-friendly). The
 * one process-global singleton stands behind the row; the Graphics table
 * carries no self pointer (the Pixel Coordinate Contract: native pixels
 * everywhere; NDC exists only inside vertex shaders).
 *
 * Rejection policy (the Cold-Strict, Hot-Minimal Validation Law): every
 * verb null-checks its arguments and cold-returns false. Brush opacity is
 * CLAMPED to [0..1] (reject-or-clamp: clamp, stated here). Paths larger
 * than the 4096-float flatten scratch are rejected with false (the
 * Dynamic Scalability & Anti-Hardcoding Law; ;;DRAFT — an arena slab with
 * exponential growth replaces the fixed scratch in a later commit).
 *
 * Color encoding follows the Strict 0xRRGGBBAA Color Law across all operations.
 *
 * STRUCT FIELDS (Mirroring raster/raster_graphics.h):
 * ----------------------------------------------------------------------------
 *   Buffer *framebuffer;  // RGBA8 native-px framebuffer (arena-backed); null until resize
 *   uint32_t width;       // drawable extent, native px; 0 until resize
 *   uint32_t height;      // drawable extent, native px; 0 until resize
 *   bool clipEnabled;     // scissor active
 *   float clipX, clipY;   // scissor top-left, native px
 *   float clipW, clipH;   // scissor extent, native px
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   FlatPath scratch — 4096-float interleaved x,y polyline (cubics
 *     subdivided 8 steps), count = points, closed flag. Bounded reject
 *     policy documented above.
 *   brushRgba(brush, outRgba) — unpacks brush color and modulates alpha with opacity.
 *   putPixel(x, y, rgba) — writes 4 channels into RGBA8 buffer.
 *   fillBox(x0, y0, x1, y1, rgba) — fills rectangular pixel region.
 *   flattenShape(shape, out) — flattens shape verbs into FlatPath polyline.
 *   scanlineFill(fp, rgba) — even-odd scanline fill for polygon outline.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - (none — singleton state is file-static and zero-initialized)
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - RasterGraphics_getRow(void)             : Query const Graphics row table
 *   - RasterGraphics_resize(width, height)    : Bind/rebind drawable extent
 *
 * Private Core Functions: (.c static)
 *   - implBegin(void)                         : Validate ready state for frame begin
 *   - implEnd(void)                           : Complete frame rendering
 *   - implPresent(void)                       : Present frame to target surface
 *   - implResize(width, height)               : Allocate arena-backed framebuffer
 *   - implClear(color)                        : Clear entire drawable with 0xRRGGBBAA
 *   - implClip(rect)                          : Update or disable scissor clipping
 *   - implFillRect(rect, brush)               : Fill rectangle with solid brush
 *   - implDrawRect(rect, stroke)              : Stroke rectangle perimeter
 *   - implFillCircle(cx, cy, radius, brush)   : Fill raster circle
 *   - implDrawCircle(cx, cy, radius, stroke)  : Stroke circle boundary
 *   - implFillPath(shape, brush)              : Fill arbitrary vector shape
 *   - implDrawPath(shape, stroke)             : Stroke vector shape segments
 *   - implDrawImage(image, dst)               : Bilinear blit image into dst rectangle
 *
 * Public Setters: (.h)
 *   - RasterGraphics_setFramebuffer(fb, w, h) : SWAP the raster target to a caller-owned 4-channel buffer (retained-bake sub-pass; no alloc/free; clip untouched)
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - RasterGraphics_getFramebuffer(void)     : Buffer * (null until resize)
 *   - RasterGraphics_getWidth(void)           : uint32_t (0 until resize)
 *   - RasterGraphics_getHeight(void)          : uint32_t (0 until resize)
 *   - RasterGraphics_isReady(void)            : bool (framebuffer bound)
 *
 * Private Getters: (.c static)
 *   - ready(void)                             : Test if framebuffer is bound
 * ============================================================================
 */

#define RASTER_FLAT_FLOATS 4096u
#define RASTER_CUBIC_STEPS 8u

// The file-local singleton: zero-initialized — framebuffer NULL, so
// every verb cold-returns false until RasterGraphics_resize succeeds.
static RasterGraphics rasterGraphics;

typedef struct FlatPath {
    float pts[RASTER_FLAT_FLOATS];  // interleaved x,y
    uint32_t count;                 // number of points (pairs)
    bool closed;
} FlatPath;

// --- private helpers -------------------------------------------------------

static bool ready(void) {
    return rasterGraphics.framebuffer != nullptr;
}

static bool brushRgba(const Brush *brush, uint32_t *outRgba) {
    if (!brush || !outRgba)
        return false;
    float op = (*brush).opacity;
    if (op < 0.0f)
        op = 0.0f;
    if (op > 1.0f)
        op = 1.0f;
    // Strict 0xRRGGBBAA: alpha lives in bits 0..7; opacity modulates it. The
    // stored word keeps red/green/blue and replaces only the alpha byte.
    uint32_t a = (uint32_t) ((float) ((*brush).color & 0xFFu) * op + 0.5f);
    if (a > 0xFFu)
        a = 0xFFu;
    *outRgba = ((*brush).color & 0xFFFFFF00u) | (a & 0xFFu);
    return true;
}

static void putPixel(int32_t x, int32_t y, uint32_t rgba) {
    if (!ready())
        return;
    if (x < 0 || y < 0)
        return;
    if (x >= (int32_t) rasterGraphics.width || y >= (int32_t) rasterGraphics.height)
        return;
    if (rasterGraphics.clipEnabled) {
        float fx = (float) x;
        float fy = (float) y;
        if (fx < rasterGraphics.clipX || fy < rasterGraphics.clipY)
            return;
        if (fx >= rasterGraphics.clipX + rasterGraphics.clipW)
            return;
        if (fy >= rasterGraphics.clipY + rasterGraphics.clipH)
            return;
    }
    Buffer *fb = rasterGraphics.framebuffer;
    Buffer_setPixel(fb, (size_t) x, (size_t) y, 0u, (uint64_t) ((rgba >> 24) & 0xFFu));
    Buffer_setPixel(fb, (size_t) x, (size_t) y, 1u, (uint64_t) ((rgba >> 16) & 0xFFu));
    Buffer_setPixel(fb, (size_t) x, (size_t) y, 2u, (uint64_t) ((rgba >> 8) & 0xFFu));
    Buffer_setPixel(fb, (size_t) x, (size_t) y, 3u, (uint64_t) (rgba & 0xFFu));
}

static void fillBox(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgba) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    int32_t dw = (int32_t) rasterGraphics.width;
    int32_t dh = (int32_t) rasterGraphics.height;
    if (x1 > dw) x1 = dw;
    if (y1 > dh) y1 = dh;
    if (x1 <= x0 || y1 <= y0)
        return;
    for (int32_t y = y0; y < y1; y++)
        for (int32_t x = x0; x < x1; x++)
            putPixel(x, y, rgba);
}

// Flatten the shape into a polyline (cubics subdivided RASTER_CUBIC_STEPS).
// Returns false on empty or oversized paths (reject policy in overview).
static bool flattenShape(const Shape *shape, FlatPath *out) {
    if (!shape || !out)
        return false;
    if ((*shape).pointCount == 0)
        return false;
    (*out).count = 0;
    (*out).closed = (*shape).closed;
    double curX = 0.0;
    double curY = 0.0;
    size_t pointIdx = 0;
    for (size_t v = 0; v < (*shape).verbCount; v++) {
        uint8_t verb = (*shape).verbs[v];
        if (verb == SHAPE_VERB_MOVE) {
            curX = (double) (*shape).points[pointIdx * 2u];
            curY = (double) (*shape).points[pointIdx * 2u + 1u];
            pointIdx++;
            if ((*out).count + 1u > RASTER_FLAT_FLOATS / 2u)
                return false;
            (*out).pts[(*out).count * 2u] = (float) curX;
            (*out).pts[(*out).count * 2u + 1u] = (float) curY;
            (*out).count++;
        } else if (verb == SHAPE_VERB_LINE) {
            double x = (double) (*shape).points[pointIdx * 2u];
            double y = (double) (*shape).points[pointIdx * 2u + 1u];
            pointIdx++;
            if ((*out).count + 1u > RASTER_FLAT_FLOATS / 2u)
                return false;
            (*out).pts[(*out).count * 2u] = (float) x;
            (*out).pts[(*out).count * 2u + 1u] = (float) y;
            (*out).count++;
            curX = x;
            curY = y;
        } else if (verb == SHAPE_VERB_CUBIC) {
            double c1x = (double) (*shape).points[pointIdx * 2u];
            double c1y = (double) (*shape).points[pointIdx * 2u + 1u];
            double c2x = (double) (*shape).points[pointIdx * 2u + 2u];
            double c2y = (double) (*shape).points[pointIdx * 2u + 3u];
            double x3 = (double) (*shape).points[pointIdx * 2u + 4u];
            double y3 = (double) (*shape).points[pointIdx * 2u + 5u];
            pointIdx += 3u;
            for (uint32_t s = 1u; s <= RASTER_CUBIC_STEPS; s++) {
                double t = (double) s / (double) RASTER_CUBIC_STEPS;
                double mt = 1.0 - t;
                double mt2 = mt * mt;
                double t2 = t * t;
                double x = mt2 * mt * curX + 3.0 * mt2 * t * c1x + 3.0 * mt * t2 * c2x + t2 * t * x3;
                double y = mt2 * mt * curY + 3.0 * mt2 * t * c1y + 3.0 * mt * t2 * c2y + t2 * t * y3;
                if ((*out).count + 1u > RASTER_FLAT_FLOATS / 2u)
                    return false;
                (*out).pts[(*out).count * 2u] = (float) x;
                (*out).pts[(*out).count * 2u + 1u] = (float) y;
                (*out).count++;
            }
            curX = x3;
            curY = y3;
        } else if (verb == SHAPE_VERB_CLOSE) {
            (*out).closed = true;
        } else {
            return false;
        }
    }
    return (*out).count >= 1u;
}

// Even-odd scanline fill over the flattened polyline.
static void scanlineFill(const FlatPath *fp, uint32_t rgba) {
    double minY = DBL_MAX;
    double maxY = -DBL_MAX;
    uint32_t n = (*fp).count;
    for (uint32_t i = 0; i < n; i++) {
        double y = (double) (*fp).pts[i * 2u + 1u];
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }
    int32_t yMin = (int32_t) minY;
    int32_t yMax = (int32_t) (maxY + 1.0);
    if (yMax < 0 || yMin >= (int32_t) rasterGraphics.height)
        return;
    if (yMin < 0) yMin = 0;
    int32_t dh = (int32_t) rasterGraphics.height;
    if (yMax > dh)
        yMax = dh;
    int32_t xs[RASTER_FLAT_FLOATS];
    for (int32_t py = yMin; py < yMax; py++) {
        double fy = (double) py + 0.5;
        uint32_t cross = 0u;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t j = (i + 1u) % n;
            if ((*fp).closed == false && j == 0u)
                break;
            double x0 = (double) (*fp).pts[i * 2u];
            double y0 = (double) (*fp).pts[i * 2u + 1u];
            double x1 = (double) (*fp).pts[j * 2u];
            double y1 = (double) (*fp).pts[j * 2u + 1u];
            bool crosses = (y0 <= fy && fy < y1) || (y1 <= fy && fy < y0);
            if (!crosses)
                continue;
            double t = (fy - y0) / (y1 - y0);
            double x = x0 + t * (x1 - x0);
            int32_t xi = (int32_t) x;
            if (cross + 1u <= RASTER_FLAT_FLOATS) {
                xs[cross] = xi;
                cross++;
            }
        }
        // insertion sort (crossings are few per scanline in practice)
        for (uint32_t i = 1u; i < cross; i++) {
            int32_t key = xs[i];
            uint32_t k = i;
            while (k > 0u && xs[k - 1u] > key) {
                xs[k] = xs[k - 1u];
                k--;
            }
            xs[k] = key;
        }
        for (uint32_t i = 0u; i + 1u < cross; i += 2u) {
            int32_t xa = xs[i];
            int32_t xb = xs[i + 1u];
            if (xb <= xa)
                continue;
            for (int32_t px = xa; px < xb; px++)
                putPixel(px, py, rgba);
        }
    }
}

// --- row entry implementations ---------------------------------------------

static bool implBegin(void) {
    return ready();
}

static bool implEnd(void) {
    return ready();
}

static bool implPresent(void) {
    return ready();
}

static bool implResize(uint32_t width, uint32_t height) {
    if (width == 0u || height == 0u)
        return false;
    Buffer *fb = Buffer_4(ID_RASTER_GRAPHICS, width, height, 4u);
    if (!fb)
        return false;
    Buffer *old = rasterGraphics.framebuffer;
    if (old)
        Buffer_free(old);
    rasterGraphics.framebuffer = fb;
    rasterGraphics.width = width;
    rasterGraphics.height = height;
    rasterGraphics.clipEnabled = false;
    return true;
}

bool RasterGraphics_setFramebuffer(Buffer *fb, uint32_t width, uint32_t height) {
    if (fb == nullptr)
        return false;
    if (width == 0u || height == 0u)
        return false;
    if (Buffer_width(fb) != (size_t) width || Buffer_height(fb) != (size_t) height)
        return false; // the row bounds-checks against width/height — the buffer must match
    if (Buffer_channels(fb) != 4u)
        return false; // the row's channel contract: ch0=A, ch1=R, ch2=G, ch3=B
    rasterGraphics.framebuffer = fb;
    rasterGraphics.width = width;
    rasterGraphics.height = height;
    return true;
}

static bool implClear(uint32_t color) {
    if (!ready())
        return false;
    fillBox(0, 0, (int32_t) rasterGraphics.width, (int32_t) rasterGraphics.height, color);
    return true;
}

static bool implClip(const Rectangle *rect) {
    if (!rasterGraphics.framebuffer)
        return false;
    if (rect == nullptr) {
        rasterGraphics.clipEnabled = false;
        return true;
    }
    rasterGraphics.clipEnabled = true;
    rasterGraphics.clipX = (*rect).x;
    rasterGraphics.clipY = (*rect).y;
    rasterGraphics.clipW = (*rect).width;
    rasterGraphics.clipH = (*rect).height;
    return true;
}

static bool implFillRect(const Rectangle *rect, const Brush *brush) {
    if (!rect || !ready())
        return false;
    uint32_t rgba;
    if (!brushRgba(brush, &rgba))
        return false;
    fillBox((int32_t) (*rect).x, (int32_t) (*rect).y,
            (int32_t) ((*rect).x + (*rect).width), (int32_t) ((*rect).y + (*rect).height), rgba);
    return true;
}

static bool implDrawRect(const Rectangle *rect, const Stroke *stroke) {
    if (!rect || !stroke || !ready())
        return false;
    uint32_t rgba = (*stroke).color;
    float wf = (*stroke).width;
    if (wf < 1.0f)
        wf = 1.0f;
    int32_t sw = (int32_t) (wf + 0.5f);
    int32_t x0 = (int32_t) (*rect).x;
    int32_t y0 = (int32_t) (*rect).y;
    int32_t x1 = (int32_t) ((*rect).x + (*rect).width);
    int32_t y1 = (int32_t) ((*rect).y + (*rect).height);
    fillBox(x0, y0, x1, y0 + sw, rgba);
    fillBox(x0, y1 - sw, x1, y1, rgba);
    fillBox(x0, y0 + sw, x0 + sw, y1 - sw, rgba);
    fillBox(x1 - sw, y0 + sw, x1, y1 - sw, rgba);
    return true;
}

static bool implFillCircle(float cx, float cy, float radius, const Brush *brush) {
    if (!ready())
        return false;
    if (radius <= 0.0f)
        return false;
    uint32_t rgba;
    if (!brushRgba(brush, &rgba))
        return false;
    double r = (double) radius;
    double r2 = r * r;
    int32_t x0 = (int32_t) (cx - r);
    int32_t y0 = (int32_t) (cy - r);
    int32_t x1 = (int32_t) (cx + r + 1.0);
    int32_t y1 = (int32_t) (cy + r + 1.0);
    for (int32_t py = y0; py < y1; py++) {
        for (int32_t px = x0; px < x1; px++) {
            double dx = (double) px + 0.5 - (double) cx;
            double dy = (double) py + 0.5 - (double) cy;
            if (dx * dx + dy * dy <= r2)
                putPixel(px, py, rgba);
        }
    }
    return true;
}

static bool implDrawCircle(float cx, float cy, float radius, const Stroke *stroke) {
    if (!stroke || !ready())
        return false;
    if (radius <= 0.0f)
        return false;
    uint32_t rgba = (*stroke).color;
    float wf = (*stroke).width;
    if (wf < 1.0f)
        wf = 1.0f;
    double halfW = (double) wf * 0.5;
    double r = (double) radius;
    double rIn = r - halfW;
    double rOut = r + halfW;
    if (rIn < 0.0)
        rIn = 0.0;
    double rIn2 = rIn * rIn;
    double rOut2 = rOut * rOut;
    int32_t x0 = (int32_t) (cx - rOut);
    int32_t y0 = (int32_t) (cy - rOut);
    int32_t x1 = (int32_t) (cx + rOut + 1.0);
    int32_t y1 = (int32_t) (cy + rOut + 1.0);
    for (int32_t py = y0; py < y1; py++) {
        for (int32_t px = x0; px < x1; px++) {
            double dx = (double) px + 0.5 - (double) cx;
            double dy = (double) py + 0.5 - (double) cy;
            double d2 = dx * dx + dy * dy;
            if (d2 >= rIn2 && d2 <= rOut2)
                putPixel(px, py, rgba);
        }
    }
    return true;
}

static bool implFillPath(const Shape *shape, const Brush *brush) {
    if (!shape || !ready())
        return false;
    uint32_t rgba;
    if (!brushRgba(brush, &rgba))
        return false;
    FlatPath fp;
    if (!flattenShape(shape, &fp))
        return false;
    scanlineFill(&fp, rgba);
    return true;
}

static bool implDrawPath(const Shape *shape, const Stroke *stroke) {
    if (!shape || !stroke || !ready())
        return false;
    uint32_t rgba = (*stroke).color;
    float wf = (*stroke).width;
    if (wf < 1.0f)
        wf = 1.0f;
    double halfW = (double) wf * 0.5;
    FlatPath fp;
    if (!flattenShape(shape, &fp))
        return false;
    uint32_t n = fp.count;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = (i + 1u) % n;
        if (fp.closed == false && j == 0u)
            break;
        double ax = (double) fp.pts[i * 2u];
        double ay = (double) fp.pts[i * 2u + 1u];
        double bx = (double) fp.pts[j * 2u];
        double by = (double) fp.pts[j * 2u + 1u];
        double minX = ax < bx ? ax : bx;
        double maxX = ax > bx ? ax : bx;
        double minY = ay < by ? ay : by;
        double maxY = ay > by ? ay : by;
        double dx = bx - ax;
        double dy = by - ay;
        double len2 = dx * dx + dy * dy;
        int32_t x0 = (int32_t) (minX - halfW);
        int32_t y0 = (int32_t) (minY - halfW);
        int32_t x1 = (int32_t) (maxX + halfW + 1.0);
        int32_t y1 = (int32_t) (maxY + halfW + 1.0);
        for (int32_t py = y0; py < y1; py++) {
            for (int32_t px = x0; px < x1; px++) {
                double qx = (double) px + 0.5 - ax;
                double qy = (double) py + 0.5 - ay;
                double t;
                if (len2 > 0.0) {
                    t = (qx * dx + qy * dy) / len2;
                    if (t < 0.0) t = 0.0;
                    if (t > 1.0) t = 1.0;
                } else {
                    t = 0.0;
                }
                double pxo = qx - t * dx;
                double pyo = qy - t * dy;
                double d2 = pxo * pxo + pyo * pyo;
                if (d2 <= halfW * halfW)
                    putPixel(px, py, rgba);
            }
        }
    }
    return true;
}

static bool implDrawImage(const Image *image, const Rectangle *dst) {
    if (!image || !dst || !ready())
        return false;
    float dx0 = (*dst).x;
    float dy0 = (*dst).y;
    float dw = (*dst).width;
    float dh = (*dst).height;
    if (dw <= 0.0f || dh <= 0.0f)
        return false;
    uint32_t srcW = (*image).width;
    uint32_t srcH = (*image).height;
    uint8_t *rgba = (*image).rgba;
    int32_t x0 = (int32_t) dx0;
    int32_t y0 = (int32_t) dy0;
    int32_t x1 = (int32_t) (dx0 + dw + 1.0f);
    int32_t y1 = (int32_t) (dy0 + dh + 1.0f);
    for (int32_t py = y0; py < y1; py++) {
        for (int32_t px = x0; px < x1; px++) {
            if (px < 0 || py < 0)
                continue;
            if (px >= (int32_t) rasterGraphics.width || py >= (int32_t) rasterGraphics.height)
                continue;
            if (rasterGraphics.clipEnabled) {
                float fx = (float) px;
                float fy = (float) py;
                if (fx < rasterGraphics.clipX || fy < rasterGraphics.clipY)
                    continue;
                if (fx >= rasterGraphics.clipX + rasterGraphics.clipW)
                    continue;
                if (fy >= rasterGraphics.clipY + rasterGraphics.clipH)
                    continue;
            }
            int32_t sx = (int32_t) (((float) (px - x0) + 0.5f) * (float) srcW / dw);
            int32_t sy = (int32_t) (((float) (py - y0) + 0.5f) * (float) srcH / dh);
            if (sx < 0) sx = 0;
            if (sy < 0) sy = 0;
            if (sx >= (int32_t) srcW) sx = (int32_t) srcW - 1;
            if (sy >= (int32_t) srcH) sy = (int32_t) srcH - 1;
            size_t si = ((size_t) sy * srcW + (size_t) sx) * 4u;
            uint32_t pixelRgba;
            if (rgba) {
                pixelRgba = ((uint32_t) rgba[si] << 24) |
                            ((uint32_t) rgba[si + 1u] << 16) |
                            ((uint32_t) rgba[si + 2u] << 8) |
                            (uint32_t) rgba[si + 3u];
            } else {
                pixelRgba = 0x404040FFu;  // no-shadow placeholder: dark gray, opaque
            }
            putPixel(px, py, pixelRgba);
        }
    }
    return true;
}

// --- the row ---------------------------------------------------------------

static const Graphics rasterRow = {
    .backendId = GRAPHICS_BACKEND_RASTER,
    .begin = implBegin,
    .end = implEnd,
    .present = implPresent,
    .resize = implResize,
    .clear = implClear,
    .clip = implClip,
    .fillRect = implFillRect,
    .drawRect = implDrawRect,
    .fillCircle = implFillCircle,
    .drawCircle = implDrawCircle,
    .fillPath = implFillPath,
    .drawPath = implDrawPath,
    .drawImage = implDrawImage,
};

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

// (none — RasterGraphics is a process-global static singleton)

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

const Graphics *RasterGraphics_getRow(void) {
    return &rasterRow;
}

bool RasterGraphics_resize(uint32_t width, uint32_t height) {
    return implResize(width, height);
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

// (none)

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
Buffer *RasterGraphics_getFramebuffer(void) {
    return rasterGraphics.framebuffer;
}

;;GETTER
uint32_t RasterGraphics_getWidth(void) {
    return rasterGraphics.width;
}

;;GETTER
uint32_t RasterGraphics_getHeight(void) {
    return rasterGraphics.height;
}

;;GETTER
bool RasterGraphics_isReady(void) {
    return ready();
}