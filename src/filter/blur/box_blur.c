#include <stdlib.h>
#include <string.h>

#include "filter/filter.h"
#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: BoxBlur (filter/blur/box_blur.c)
 * ============================================================================
 * The box-blur algorithm — one FilterRow (FILTER_BLUR_BOX) of the ordered
 * compositor stack. A separable box blur: a horizontal sliding average, then a
 * vertical one, both over the RGBA8 CPU shadow. This is the reference CPU
 * path (cmdBuffer null): it proves the filter contract end-to-end headless and
 * gives the raster dialect a working blur without a device.
 *
 * The GPU path (cmdBuffer non-null) is not implemented here yet — it answers
 * false (cold-false, the Cold-Strict, Hot-Minimal Validation Law) until the
 * Vulkan/Metal compute blur lands; the stack simply skips the node.
 *
 * The scratch buffer is owned by the state and re-grown only when the working
 * extent changes (a resize-class event), never per steady-state apply.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: BoxBlur (filter/blur/box_blur.c)
 * LEVEL: L2 — Behavior (one filter algorithm row)
 * ============================================================================
 * SUMMARY:
 *   Registers the FILTER_BLUR_BOX FilterRow. Separable CPU box blur over an
 *   Image's RGBA8 shadow; the GPU path is cold-false until compute lands.
 *
 * STRUCT FIELDS: none — procedural algorithm file owning one private helper.
 *
 * PRIVATE HELPERS (kept file-local, no external API):
 * ----------------------------------------------------------------------------
 *   BoxBlurState
 *     int radius;        // box half-width in pixels (clamped 0..64)
 *     uint32_t width;    // scratch extent, native px
 *     uint32_t height;
 *     uint8_t *scratch;  // OWNED separable intermediate, width*height*4 RGBA8
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Private Core Functions: (.c static)
 *   - blurH(src, dst, w, h, radius) : horizontal sliding-average pass
 *   - blurV(src, dst, w, h, radius) : vertical sliding-average pass
 *   - ensureScratch(state, w, h)    : grow the intermediate only on drift
 *   - boxCreateState(desc) / boxDestroyState(state)
 *   - boxApply(state, cmdBuffer, input, output)
 *   - boxSetParam(state, index, value) / boxGetParam(state, index)
 * ============================================================================
 */

// SLOT RECORD state for FILTER_BLUR_BOX (owned by the row's createState).
typedef struct BoxBlurState {
    int radius;        // box half-width in pixels (clamped 0..64)
    uint32_t width;    // scratch extent, native px
    uint32_t height;
    uint8_t *scratch;  // OWNED separable intermediate, width*height*4 RGBA8
} BoxBlurState;

#define BOX_BLUR_RADIUS_MAX 64

static int clampRadius(int radius) {
    if (radius < 0)
        return 0;
    if (radius > BOX_BLUR_RADIUS_MAX)
        return BOX_BLUR_RADIUS_MAX;
    return radius;
}

// Horizontal sliding-average pass over RGBA8 (clamped edges). radius 0 copies.
static void blurH(const uint8_t *src, uint8_t *dst, uint32_t w, uint32_t h, int radius) {
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *row = src + (size_t) y * (size_t) w * 4u;
        uint8_t *out = dst + (size_t) y * (size_t) w * 4u;
        for (uint32_t x = 0; x < w; x++) {
            int r = 0, g = 0, b = 0, a = 0, n = 0;
            int x0 = (int) x - radius;
            int x1 = (int) x + radius;
            if (x0 < 0) x0 = 0;
            if (x1 > (int) w - 1) x1 = (int) w - 1;
            for (int i = x0; i <= x1; i++) {
                const uint8_t *p = row + (size_t) i * 4u;
                r += p[0]; g += p[1]; b += p[2]; a += p[3];
                n++;
            }
            uint8_t *q = out + (size_t) x * 4u;
            q[0] = (uint8_t) (r / n);
            q[1] = (uint8_t) (g / n);
            q[2] = (uint8_t) (b / n);
            q[3] = (uint8_t) (a / n);
        }
    }
}

// Vertical sliding-average pass over RGBA8 (clamped edges). radius 0 copies.
static void blurV(const uint8_t *src, uint8_t *dst, uint32_t w, uint32_t h, int radius) {
    for (uint32_t x = 0; x < w; x++) {
        for (uint32_t y = 0; y < h; y++) {
            int r = 0, g = 0, b = 0, a = 0, n = 0;
            int y0 = (int) y - radius;
            int y1 = (int) y + radius;
            if (y0 < 0) y0 = 0;
            if (y1 > (int) h - 1) y1 = (int) h - 1;
            for (int i = y0; i <= y1; i++) {
                const uint8_t *p = src + ((size_t) i * (size_t) w + (size_t) x) * 4u;
                r += p[0]; g += p[1]; b += p[2]; a += p[3];
                n++;
            }
            uint8_t *q = dst + ((size_t) y * (size_t) w + (size_t) x) * 4u;
            q[0] = (uint8_t) (r / n);
            q[1] = (uint8_t) (g / n);
            q[2] = (uint8_t) (b / n);
            q[3] = (uint8_t) (a / n);
        }
    }
}

// Grow the separable intermediate only when the extent drifts (resize-class,
// never per steady-state apply). Returns false on OOM (drop-degrade).
static bool ensureScratch(BoxBlurState *state, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0)
        return false;
    if ((*state).scratch != nullptr && (*state).width == w && (*state).height == h)
        return true;
    size_t bytes = (size_t) w * (size_t) h * 4u;
    if (bytes / 4u != (size_t) w * (size_t) h)
        return false; // overflow
    uint8_t *fresh = (uint8_t*) realloc((*state).scratch, bytes);
    if (fresh == nullptr)
        return false;
    (*state).scratch = fresh;
    (*state).width = w;
    (*state).height = h;
    return true;
}

static void *boxCreateState(const FilterDesc *desc) {
    BoxBlurState *state = (BoxBlurState*) calloc(1, sizeof(BoxBlurState));
    if (state == nullptr)
        return nullptr;
    float radius = (desc != nullptr) ? (*desc).param[0] : 1.0f;
    (*state).radius = clampRadius((int) (radius + 0.5f));
    return state;
}

static void boxDestroyState(void *state) {
    if (state == nullptr)
        return;
    free((*((BoxBlurState*) state)).scratch);
    free(state);
}

static bool boxApply(void *state, void *cmdBuffer, Image *input, Image *output) {
    if (state == nullptr || input == nullptr || output == nullptr)
        return false;
    // GPU path not implemented yet — cold-false until compute blur lands.
    if (cmdBuffer != nullptr)
        return false;

    uint32_t w = Image_width(input);
    uint32_t h = Image_height(input);
    if (w == 0 || h == 0 || Image_width(output) != w || Image_height(output) != h)
        return false;
    const uint8_t *src = Image_pixels(input);
    uint8_t *dst = Image_pixels(output);
    if (src == nullptr || dst == nullptr)
        return false;

    BoxBlurState *self = (BoxBlurState*) state;
    if (!ensureScratch(self, w, h))
        return false;
    int radius = (*self).radius;
    blurH(src, (*self).scratch, w, h, radius);
    blurV((*self).scratch, dst, w, h, radius);
    return true;
}

static bool boxSetParam(void *state, uint32_t index, float value) {
    if (state == nullptr || index != 0)
        return false;
    (*((BoxBlurState*) state)).radius = clampRadius((int) (value + 0.5f));
    return true;
}

static float boxGetParam(const void *state, uint32_t index) {
    if (state == nullptr || index != 0)
        return 0.0f;
    return (float) (*((const BoxBlurState*) state)).radius;
}

// The algorithm's row: one static table, registered once at boot.
static const FilterRow kBoxBlurRow = {
    .kind = FILTER_BLUR_BOX,
    .name = "blur.box",
    .createState = boxCreateState,
    .destroyState = boxDestroyState,
    .apply = boxApply,
    .setParam = boxSetParam,
    .getParam = boxGetParam,
};

// The dialect-style export: the registry takes the row, never the type.
const FilterRow *BoxBlur_row(void) {
    return &kBoxBlurRow;
}
