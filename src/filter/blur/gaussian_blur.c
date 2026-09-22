#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "filter/filter.h"
#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: GaussianBlur (filter/blur/gaussian_blur.c)
 * ============================================================================
 * The gaussian-blur algorithm — one FilterRow (FILTER_BLUR_GAUSSIAN) of the
 * ordered compositor stack, and the sibling of box_blur.c (same family, one
 * file per algorithm). A separable gaussian: a weighted horizontal pass, then
 * a weighted vertical one, both over the RGBA8 CPU shadow. Sigma is the single
 * knob (param[0]); the kernel is built once at createState (cold) and reused.
 *
 * Box blur is a cheap approximation of this; gaussian is the smooth, correctly
 * weighted one. Both are FILTER_BLUR_* kinds, so a stack may carry either or
 * both — the stack never names the algorithm.
 *
 * The GPU path is cold-false until compute lands (the Cold-Strict, Hot-Minimal
 * Validation Law). The scratch buffer grows only on extent drift, never per
 * steady-state apply.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: GaussianBlur (filter/blur/gaussian_blur.c)
 * LEVEL: L2 — Behavior (one filter algorithm row)
 * ============================================================================
 * SUMMARY:
 *   Registers the FILTER_BLUR_GAUSSIAN FilterRow. Separable CPU gaussian blur
 *   over an Image's RGBA8 shadow; the GPU path is cold-false until compute lands.
 *
 * STRUCT FIELDS: none — procedural algorithm file owning one private helper.
 *
 * PRIVATE HELPERS (kept file-local, no external API):
 * ----------------------------------------------------------------------------
 *   GaussianState
 *     float sigma;       // blur standard deviation (clamped 0..32)
 *     int radius;        // kernel half-width = ceil(3*sigma)
 *     float *kernel;     // OWNED normalized 1-D kernel, 2*radius+1 taps
 *     uint32_t width;    // scratch extent, native px
 *     uint32_t height;
 *     uint8_t *scratch;  // OWNED separable intermediate, width*height*4 RGBA8
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Private Core Functions: (.c static)
 *   - buildKernel(state, sigma)     : (re)build the normalized 1-D kernel
 *   - blurH(src, dst, w, h, state)  : horizontal weighted pass
 *   - blurV(src, dst, w, h, state)  : vertical weighted pass
 *   - ensureScratch(state, w, h)    : grow the intermediate only on drift
 *   - gaussCreateState(desc) / gaussDestroyState(state)
 *   - gaussApply(state, cmdBuffer, input, output)
 *   - gaussSetParam(state, index, value) / gaussGetParam(state, index)
 * ============================================================================
 */

// SLOT RECORD state for FILTER_BLUR_GAUSSIAN (owned by the row's createState).
typedef struct GaussianState {
    float sigma;       // blur standard deviation (clamped 0..32)
    int radius;        // kernel half-width = ceil(3*sigma)
    float *kernel;     // OWNED normalized 1-D kernel, 2*radius+1 taps
    uint32_t width;    // scratch extent, native px
    uint32_t height;
    uint8_t *scratch;  // OWNED separable intermediate, width*height*4 RGBA8
} GaussianState;

#define GAUSS_SIGMA_MIN 0.0f
#define GAUSS_SIGMA_MAX 32.0f

// (Re)build the normalized 1-D gaussian kernel for sigma. sigma 0 -> radius 0
// (a 1-tap identity kernel). Returns false on OOM (drop-degrade).
static bool buildKernel(GaussianState *state, float sigma) {
    if (sigma < GAUSS_SIGMA_MIN) sigma = GAUSS_SIGMA_MIN;
    if (sigma > GAUSS_SIGMA_MAX) sigma = GAUSS_SIGMA_MAX;
    int radius = (sigma > 0.0f) ? (int) ceilf(3.0f * sigma) : 0;
    int taps = 2 * radius + 1;
    float *kernel = (float*) realloc((*state).kernel, (size_t) taps * sizeof(float));
    if (kernel == nullptr)
        return false;
    float twoSigmaSq = 2.0f * sigma * sigma;
    float sum = 0.0f;
    for (int i = 0; i < taps; i++) {
        float d = (float) (i - radius);
        kernel[i] = (sigma > 0.0f) ? expf(-(d * d) / twoSigmaSq) : 1.0f;
        sum += kernel[i];
    }
    for (int i = 0; i < taps; i++)
        kernel[i] /= sum;
    (*state).kernel = kernel;
    (*state).radius = radius;
    (*state).sigma = sigma;
    return true;
}

// Horizontal weighted pass over RGBA8 (clamped edges).
static void blurH(const uint8_t *src, uint8_t *dst, uint32_t w, uint32_t h,
                  const GaussianState *state) {
    int radius = (*state).radius;
    const float *k = (*state).kernel;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *row = src + (size_t) y * (size_t) w * 4u;
        uint8_t *out = dst + (size_t) y * (size_t) w * 4u;
        for (uint32_t x = 0; x < w; x++) {
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (int t = -radius; t <= radius; t++) {
                int sx = (int) x + t;
                if (sx < 0) sx = 0;
                if (sx > (int) w - 1) sx = (int) w - 1;
                const uint8_t *p = row + (size_t) sx * 4u;
                float wt = k[t + radius];
                acc[0] += wt * p[0]; acc[1] += wt * p[1];
                acc[2] += wt * p[2]; acc[3] += wt * p[3];
            }
            uint8_t *q = out + (size_t) x * 4u;
            q[0] = (uint8_t) (acc[0] + 0.5f);
            q[1] = (uint8_t) (acc[1] + 0.5f);
            q[2] = (uint8_t) (acc[2] + 0.5f);
            q[3] = (uint8_t) (acc[3] + 0.5f);
        }
    }
}

// Vertical weighted pass over RGBA8 (clamped edges).
static void blurV(const uint8_t *src, uint8_t *dst, uint32_t w, uint32_t h,
                  const GaussianState *state) {
    int radius = (*state).radius;
    const float *k = (*state).kernel;
    for (uint32_t x = 0; x < w; x++) {
        for (uint32_t y = 0; y < h; y++) {
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (int t = -radius; t <= radius; t++) {
                int sy = (int) y + t;
                if (sy < 0) sy = 0;
                if (sy > (int) h - 1) sy = (int) h - 1;
                const uint8_t *p = src + ((size_t) sy * (size_t) w + (size_t) x) * 4u;
                float wt = k[t + radius];
                acc[0] += wt * p[0]; acc[1] += wt * p[1];
                acc[2] += wt * p[2]; acc[3] += wt * p[3];
            }
            uint8_t *q = dst + ((size_t) y * (size_t) w + (size_t) x) * 4u;
            q[0] = (uint8_t) (acc[0] + 0.5f);
            q[1] = (uint8_t) (acc[1] + 0.5f);
            q[2] = (uint8_t) (acc[2] + 0.5f);
            q[3] = (uint8_t) (acc[3] + 0.5f);
        }
    }
}

// Grow the separable intermediate only when the extent drifts (resize-class,
// never per steady-state apply). Returns false on OOM (drop-degrade).
static bool ensureScratch(GaussianState *state, uint32_t w, uint32_t h) {
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

static void *gaussCreateState(const FilterDesc *desc) {
    GaussianState *state = (GaussianState*) calloc(1, sizeof(GaussianState));
    if (state == nullptr)
        return nullptr;
    float sigma = (desc != nullptr && (*desc).param[0] > 0.0f) ? (*desc).param[0] : 2.0f;
    if (!buildKernel(state, sigma)) {
        free(state);
        return nullptr;
    }
    return state;
}

static void gaussDestroyState(void *state) {
    if (state == nullptr)
        return;
    free((*((GaussianState*) state)).kernel);
    free((*((GaussianState*) state)).scratch);
    free(state);
}

static bool gaussApply(void *state, void *cmdBuffer, Image *input, Image *output) {
    if (state == nullptr || input == nullptr || output == nullptr)
        return false;
    if (cmdBuffer != nullptr)
        return false; // GPU path not implemented yet.

    uint32_t w = Image_width(input);
    uint32_t h = Image_height(input);
    if (w == 0 || h == 0 || Image_width(output) != w || Image_height(output) != h)
        return false;
    const uint8_t *src = Image_pixels(input);
    uint8_t *dst = Image_pixels(output);
    if (src == nullptr || dst == nullptr)
        return false;

    GaussianState *self = (GaussianState*) state;
    if (!ensureScratch(self, w, h))
        return false;
    blurH(src, (*self).scratch, w, h, self);
    blurV((*self).scratch, dst, w, h, self);
    return true;
}

static bool gaussSetParam(void *state, uint32_t index, float value) {
    if (state == nullptr || index != 0)
        return false;
    if (value < GAUSS_SIGMA_MIN) value = GAUSS_SIGMA_MIN;
    if (value > GAUSS_SIGMA_MAX) value = GAUSS_SIGMA_MAX;
    return buildKernel((GaussianState*) state, value);
}

static float gaussGetParam(const void *state, uint32_t index) {
    if (state == nullptr || index != 0)
        return 0.0f;
    return (*((const GaussianState*) state)).sigma;
}

static const FilterRow kGaussianBlurRow = {
    .kind = FILTER_BLUR_GAUSSIAN,
    .name = "blur.gaussian",
    .createState = gaussCreateState,
    .destroyState = gaussDestroyState,
    .apply = gaussApply,
    .setParam = gaussSetParam,
    .getParam = gaussGetParam,
};

const FilterRow *GaussianBlur_row(void) {
    return &kGaussianBlurRow;
}
