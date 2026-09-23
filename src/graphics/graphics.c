#include "lang/graphics.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Graphics
 * ============================================================================
 * The unified Graphics seam: one table of drawable verbs, one row per backend.
 * Backends (RasterGraphics, VkGraphics, MetalGraphics) export a `const Graphics *`
 * and register it; Graphics_setGraphics stamps the chosen row as current; the
 * Graphics_<verb> forwarders call through it. Callers switch renderers with one
 * line and never touch a backend type.
 *
 * The registry is the same shape as the Device and Filter registries: one
 * contract, one row per implementation, zero backend types in caller code.
 *
 * Lifetime: the registry is a fixed, doubling table; the current row is a bare
 * pointer into it. Zero steady-state allocation.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Graphics (graphics/graphics.c)
 * LEVEL: L2 — Behavior (the unified draw-verb seam + backend registry)
 * ============================================================================
 * SUMMARY:
 *   Holds the backend row registry + the current selection. Every Graphics_<verb>
 *   forwarder calls through the current row (null-guarded).
 *
 * STRUCT FIELDS: none — file-local registry + current-row pointer.
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   registryGrow(void)   : double the backend table (Anti-Hardcoding Law)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Core Functions: (.h)
 *   - Graphics_registerRow(row) / Graphics_setGraphics(backendId)
 *   - Graphics_getCurrent(void) / Graphics_getGraphicsId(void)
 *   - Graphics_getClip(dest) : snapshot scissor for nested widget painting
 *   - Graphics_<verb>(...) forwarders
 *   - Graphics_drawImageFit(image, dst, mode, anchor, windowW, windowH, outFit)
 *     : fit resolve + scissor + draw + parent-scissor restoration (the picture one-call)
 *
 * Private Core Functions: (.c static)
 *   - registryGrow(void)
 * ============================================================================
 */

// --- Backend registry (fixed table, doubling growth) ---
static const Graphics **s_rows = nullptr;
static uint32_t s_rowCount = 0;
static uint32_t s_rowCap = 0;
static Rectangle s_clip = {0};
static bool s_hasClip = false;
static const Graphics *s_current = nullptr;   // the active row (null = none)

// The all-cold row: every forwarder answers false when nothing is selected.
static const Graphics kNullRow = { .backendId = LANG_BACKEND_NONE };

static bool registryGrow(void) {
    if (s_rowCount < s_rowCap)
        return true;
    uint32_t newCap = (s_rowCap == 0) ? 4 : s_rowCap * 2;
    const Graphics **nb = (const Graphics**) realloc((void*) s_rows,
                                                     (size_t) newCap * sizeof(*nb));
    if (nb == nullptr)
        return false;
    s_rows = nb;
    s_rowCap = newCap;
    return true;
}

bool Graphics_registerRow(const Graphics *row) {
    if (row == nullptr || (*row).backendId == LANG_BACKEND_NONE)
        return false;
    if ((*row).fillRect == nullptr)
        return false;
    for (uint32_t i = 0; i < s_rowCount; i++) {
        if ((*s_rows[i]).backendId == (*row).backendId) {
            s_rows[i] = row;
            return true;
        }
    }
    if (!registryGrow())
        return false;
    s_rows[s_rowCount++] = row;
    return true;
}

bool Graphics_setGraphics(uint32_t backendId) {
    for (uint32_t i = 0; i < s_rowCount; i++) {
        if ((*s_rows[i]).backendId == backendId) {
            s_current = s_rows[i];
            Graphics_clip(nullptr);
            return true;
        }
    }
    return false;   // unknown id: the previous row stays active
}

const Graphics *Graphics_getCurrent(void) {
    return s_current ? s_current : &kNullRow;
}

uint32_t Graphics_getGraphicsId(void) {
    return s_current ? (*s_current).backendId : LANG_BACKEND_NONE;
}

// --- Forwarders ---

bool Graphics_begin(void) {
    const Graphics *g = s_current;
    return (g != nullptr && (*g).begin != nullptr) ? (*g).begin() : false;
}

bool Graphics_end(void) {
    const Graphics *g = s_current;
    return (g != nullptr && (*g).end != nullptr) ? (*g).end() : false;
}

bool Graphics_present(void) {
    const Graphics *g = s_current;
    return (g != nullptr && (*g).present != nullptr) ? (*g).present() : false;
}

bool Graphics_resize(uint32_t width, uint32_t height) {
    const Graphics *g = s_current;
    if (g == nullptr || (*g).resize == nullptr || !(*g).resize(width, height))
        return false;
    return Graphics_clip(nullptr);
}

bool Graphics_clear(uint32_t color) {
    const Graphics *g = s_current;
    return (g != nullptr && (*g).clear != nullptr) ? (*g).clear(color) : false;
}

bool Graphics_clip(const Rectangle *rect) {
    const Graphics *g = s_current;
    if (g == nullptr || (*g).clip == nullptr || !(*g).clip(rect))
        return false;
    s_hasClip = rect != nullptr;
    if (rect)
        s_clip = *rect;
    return true;
}

bool Graphics_getClip(Rectangle *dest) {
    if (dest)
        *dest = s_clip;
    return s_hasClip;
}

bool Graphics_fillRect(const Rectangle *rect, const Brush *brush) {
    const Graphics *g = s_current;
    return (g != nullptr && (*g).fillRect != nullptr) ? (*g).fillRect(rect, brush) : false;
}

bool Graphics_drawRect(const Rectangle *rect, const Stroke *stroke) {
    const Graphics *g = s_current;
    return (g != nullptr && (*g).drawRect != nullptr) ? (*g).drawRect(rect, stroke) : false;
}

bool Graphics_fillCircle(float cx, float cy, float radius, const Brush *brush) {
    const Graphics *g = s_current;
    return (g != nullptr && (*g).fillCircle != nullptr) ? (*g).fillCircle(cx, cy, radius, brush) : false;
}

bool Graphics_drawCircle(float cx, float cy, float radius, const Stroke *stroke) {
    const Graphics *g = s_current;
    return (g != nullptr && (*g).drawCircle != nullptr) ? (*g).drawCircle(cx, cy, radius, stroke) : false;
}

bool Graphics_fillPath(const Shape *shape, const Brush *brush) {
    const Graphics *g = s_current;
    return (g != nullptr && (*g).fillPath != nullptr) ? (*g).fillPath(shape, brush) : false;
}

bool Graphics_drawPath(const Shape *shape, const Stroke *stroke) {
    const Graphics *g = s_current;
    return (g != nullptr && (*g).drawPath != nullptr) ? (*g).drawPath(shape, stroke) : false;
}

bool Graphics_drawImage(const Image *image, const Rectangle *dst) {
    const Graphics *g = s_current;
    return (g != nullptr && (*g).drawImage != nullptr) ? (*g).drawImage(image, dst) : false;
}

bool Graphics_drawImageFit(const Image *image, const Rectangle *dst, ImageFitMode mode,
                           ImageAnchor anchor, float windowW, float windowH, ImageFit *outFit) {
    ImageFit fit;
    if (!Image_fitRect(image, dst, mode, anchor, windowW, windowH, &fit))
        return false;
    Rectangle previous, clip = fit.clip;
    bool hadClip = Graphics_getClip(&previous);
    if (fit.needsClip) {
        if (hadClip)
            Rectangle_intersection(&previous, &fit.clip, &clip);
        Graphics_clip(&clip);
    }
    bool ok = Graphics_drawImage(image, &fit.dst);
    if (fit.needsClip)
        Graphics_clip(hadClip ? &previous : nullptr);
    if (outFit != nullptr)
        *outFit = fit;
    return ok;
}

bool Graphics_drawText(const Rectangle *rect, const char *text, const Brush *brush) {
    const Graphics *g = s_current;
    return (g != nullptr && (*g).drawText != nullptr) ? (*g).drawText(rect, text, brush) : false;
}
