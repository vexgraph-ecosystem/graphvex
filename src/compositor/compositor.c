#include "lang/compositor.h"

#include <stdlib.h>
#include <string.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Compositor
 * ============================================================================
 * The board compositor: it folds the window's boards into ONE image in z-order
 * (scene bottom, content top), then runs the ordered filter stack over the
 * result. This is the seam's collage — the step that turns "two boards with
 * their own worlds" into the single image a device presents.
 *
 * The compositor BORROWS its boards and its filter stack and OWNS only the
 * canvas (the un-filtered composite, one Image for the whole window). The final
 * result lands in the caller's `out`, so the device presents exactly what the
 * compositor produced.
 *
 * The CPU path (cmdBuffer null) blends the RGBA8 shadows and runs the CPU
 * filters — fully headless on the raster device. The GPU path is cold-false
 * until the dialect compositor lands (the Cold-Strict, Hot-Minimal Validation
 * Law).
 *
 * Lifetime: the canvas is heap-owned; the boards/stack are borrowed. Teardown
 * is top-down (the Teardown Order Law): free the boards, then the compositor.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Compositor (compositor/compositor.c)
 * LEVEL: L2 — Behavior (z-order board collage + filter chain)
 * ============================================================================
 * SUMMARY:
 *   Borrows scene/content boards + a FilterStack; owns one canvas Image.
 *   Compositor_composite folds the boards into the canvas, runs the stack, and
 *   writes the result into out.
 *
 * STRUCT FIELDS (Mirroring lang/compositor.h incomplete tag — completed here):
 * ----------------------------------------------------------------------------
 *   Image *canvas;        // OWNED un-filtered composite (one per window)
 *   Image *scene;         // borrowed bottom board (nullable)
 *   Image *content;       // borrowed top board (nullable)
 *   FilterStack *filters; // borrowed ordered stack (nullable = no filtering)
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   ensureCanvas(comp, w, h)     : grow the canvas to the out extent
 *   blendOver(src, dst)          : straight-alpha "over" over the shared region
 *   copyShadow(src, dst)         : RGBA8 shadow copy
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Compositor_0(void) / Compositor_2(w,h)
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Compositor_destroy(compositor)
 *   - Compositor_composite(compositor, cmdBuffer, out)
 *
 * Private Core Functions: (.c static)
 *   - ensureCanvas(comp, w, h)
 *   - blendOver(src, dst)
 *   - copyShadow(src, dst)
 *
 * Public Setters: (.h)
 *   - Compositor_setScene(compositor, scene)
 *   - Compositor_setContent(compositor, content)
 *   - Compositor_setFilters(compositor, stack)
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Compositor_getScene(compositor) / Compositor_getContent(compositor)
 *   - Compositor_getFilters(compositor) / Compositor_getCanvas(compositor)
 *   - Compositor_width(compositor) / Compositor_height(compositor)
 *   - Compositor_isValid(compositor)
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

struct Compositor {
    Image *canvas;        // OWNED un-filtered composite (one per window)
    Image *scene;         // borrowed bottom board (nullable)
    Image *content;       // borrowed top board (nullable)
    FilterStack *filters; // borrowed ordered stack (nullable = no filtering)
};

// Grow the canvas to the requested extent. Returns false on zero/OOM.
static bool ensureCanvas(Compositor *compositor, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0)
        return false;
    if ((*compositor).canvas != nullptr
        && Image_width((*compositor).canvas) == w
        && Image_height((*compositor).canvas) == h)
        return true;
    Image *fresh = Image_2(w, h);
    if (fresh == nullptr)
        return false;
    Image_destroy((*compositor).canvas);
    (*compositor).canvas = fresh;
    return true;
}

// Straight-alpha "over": src over dst, over the shared (min) region. dst keeps
// its extent; a smaller src blends only where both exist.
static void blendOver(const Image *src, Image *dst) {
    uint32_t w = Image_width(src) < Image_width(dst) ? Image_width(src) : Image_width(dst);
    uint32_t h = Image_height(src) < Image_height(dst) ? Image_height(src) : Image_height(dst);
    const uint8_t *s = Image_pixels(src);
    uint8_t *d = Image_pixels(dst);
    if (s == nullptr || d == nullptr)
        return;
    uint32_t dstW = Image_width(dst);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t *sp = s + ((size_t) y * w + x) * 4u;
            uint8_t *dp = d + ((size_t) y * dstW + x) * 4u;
            float sa = (float) sp[3] / 255.0f;
            if (sa <= 0.0f)
                continue;
            float da = (float) dp[3] / 255.0f;
            for (int c = 0; c < 3; c++)
                dp[c] = (uint8_t) ((float) sp[c] * sa + (float) dp[c] * (1.0f - sa) + 0.5f);
            dp[3] = (uint8_t) ((sa + da * (1.0f - sa)) * 255.0f + 0.5f);
        }
    }
}

// RGBA8 shadow copy (same extent assumed by the caller).
static bool copyShadow(Image *src, Image *dst) {
    uint32_t w = Image_width(src);
    uint32_t h = Image_height(src);
    if (w == 0 || h == 0 || Image_width(dst) != w || Image_height(dst) != h)
        return false;
    const uint8_t *s = Image_pixels(src);
    uint8_t *d = Image_pixels(dst);
    if (s == nullptr || d == nullptr)
        return false;
    memcpy(d, s, (size_t) w * (size_t) h * 4u);
    return true;
}

// CONSTRUCTORS (PUBLIC & PRIVATE)

static Compositor *compositorCreate(uint32_t width, uint32_t height) {
    Compositor *compositor = (Compositor*) calloc(1, sizeof(Compositor));
    if (compositor == nullptr)
        return nullptr;
    if (width > 0 && height > 0) {
        (*compositor).canvas = Image_2(width, height);
        if ((*compositor).canvas == nullptr) {
            free(compositor);
            return nullptr;
        }
    }
    return compositor;
}

Compositor *Compositor_0(void) {
    return compositorCreate(0, 0);
}

Compositor *Compositor_2(uint32_t width, uint32_t height) {
    return compositorCreate(width, height);
}

void Compositor_destroy(Compositor *compositor) {
    if (compositor == nullptr)
        return;
    Image_destroy((*compositor).canvas);
    free(compositor);
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

bool Compositor_composite(Compositor *compositor, void *cmdBuffer, Image *out) {
    if (compositor == nullptr || out == nullptr)
        return false;
    if (cmdBuffer != nullptr)
        return false; // GPU path not implemented yet.

    uint32_t w = Image_width(out);
    uint32_t h = Image_height(out);
    if (w == 0 || h == 0)
        return false;
    if (!ensureCanvas(compositor, w, h))
        return false;

    // 1. clear the canvas, then over-composite the boards in z-order.
    uint8_t *canvasPx = Image_pixels((*compositor).canvas);
    memset(canvasPx, 0, (size_t) w * (size_t) h * 4u);
    if ((*compositor).scene != nullptr)
        blendOver((*compositor).scene, (*compositor).canvas);
    if ((*compositor).content != nullptr)
        blendOver((*compositor).content, (*compositor).canvas);

    // 2. the filter stack folds canvas -> out; no stack copies canvas -> out.
    if ((*compositor).filters != nullptr)
        return FilterStack_apply((*compositor).filters, cmdBuffer, (*compositor).canvas, out);
    return copyShadow((*compositor).canvas, out);
}

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void Compositor_setScene(Compositor *compositor, Image *scene) {
    if (compositor == nullptr)
        return;
    (*compositor).scene = scene;
}

;;SETTER
void Compositor_setContent(Compositor *compositor, Image *content) {
    if (compositor == nullptr)
        return;
    (*compositor).content = content;
}

;;SETTER
void Compositor_setFilters(Compositor *compositor, FilterStack *stack) {
    if (compositor == nullptr)
        return;
    (*compositor).filters = stack;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
Image *Compositor_getScene(const Compositor *compositor) {
    return compositor ? (*compositor).scene : nullptr;
}

;;GETTER
Image *Compositor_getContent(const Compositor *compositor) {
    return compositor ? (*compositor).content : nullptr;
}

;;GETTER
FilterStack *Compositor_getFilters(const Compositor *compositor) {
    return compositor ? (*compositor).filters : nullptr;
}

;;GETTER
Image *Compositor_getCanvas(const Compositor *compositor) {
    return compositor ? (*compositor).canvas : nullptr;
}

;;GETTER
uint32_t Compositor_width(const Compositor *compositor) {
    if (compositor == nullptr || (*compositor).canvas == nullptr)
        return 0u;
    return Image_width((*compositor).canvas);
}

;;GETTER
uint32_t Compositor_height(const Compositor *compositor) {
    if (compositor == nullptr || (*compositor).canvas == nullptr)
        return 0u;
    return Image_height((*compositor).canvas);
}

;;GETTER
bool Compositor_isValid(const Compositor *compositor) {
    return compositor != nullptr;
}
