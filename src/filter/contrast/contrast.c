#include <stdlib.h>

#include "filter/filter.h"
#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Contrast (filter/contrast/contrast.c)
 * ============================================================================
 * The contrast algorithm — one FilterRow (FILTER_CONTRAST) of the ordered
 * compositor stack. A per-channel RGB contrast about mid-grey: amount 1.0 is
 * neutral, >1 pushes values away from 0.5, <1 pulls them toward it. Alpha is
 * left untouched. The reference CPU path (cmdBuffer null) makes the ORDER of a
 * stack observable — blur-then-contrast is a different fold than
 * contrast-then-blur — which is the whole point of an ordered stack.
 *
 * The GPU path is cold-false until compute lands (the Cold-Strict, Hot-Minimal
 * Validation Law); the stack skips a node only by failing, so a GPU stack must
 * have its GPU nodes implemented before it runs on a device.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Contrast (filter/contrast/contrast.c)
 * LEVEL: L2 — Behavior (one filter algorithm row)
 * ============================================================================
 * SUMMARY:
 *   Registers the FILTER_CONTRAST FilterRow. CPU per-channel RGB contrast about
 *   mid-grey; the GPU path is cold-false until compute lands.
 *
 * STRUCT FIELDS: none — procedural algorithm file owning one private helper.
 *
 * PRIVATE HELPERS (kept file-local, no external API):
 * ----------------------------------------------------------------------------
 *   ContrastState
 *     float amount;   // 1.0 = neutral, >1 more contrast, <1 less
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Private Core Functions: (.c static)
 *   - contrastCreateState(desc) / contrastDestroyState(state)
 *   - contrastApply(state, cmdBuffer, input, output)
 *   - contrastSetParam(state, index, value) / contrastGetParam(state, index)
 * ============================================================================
 */

// SLOT RECORD state for FILTER_CONTRAST (owned by the row's createState).
typedef struct ContrastState {
    float amount;   // 1.0 = neutral, >1 more contrast, <1 less
} ContrastState;

#define CONTRAST_AMOUNT_MIN 0.0f
#define CONTRAST_AMOUNT_MAX 8.0f

static float clampAmount(float amount) {
    if (amount < CONTRAST_AMOUNT_MIN)
        return CONTRAST_AMOUNT_MIN;
    if (amount > CONTRAST_AMOUNT_MAX)
        return CONTRAST_AMOUNT_MAX;
    return amount;
}

static void *contrastCreateState(const FilterDesc *desc) {
    ContrastState *state = (ContrastState*) calloc(1, sizeof(ContrastState));
    if (state == nullptr)
        return nullptr;
    float amount = (desc != nullptr) ? (*desc).param[0] : 1.0f;
    (*state).amount = clampAmount(amount > 0.0f ? amount : 1.0f);
    return state;
}

static void contrastDestroyState(void *state) {
    free(state);
}

static bool contrastApply(void *state, void *cmdBuffer, Image *input, Image *output) {
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

    float amount = (*((ContrastState*) state)).amount;
    size_t px = (size_t) w * (size_t) h;
    for (size_t i = 0; i < px; i++) {
        const uint8_t *p = src + i * 4u;
        uint8_t *q = dst + i * 4u;
        for (int c = 0; c < 3; c++) {
            float v = (float) p[c] / 255.0f;
            v = (v - 0.5f) * amount + 0.5f;
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            q[c] = (uint8_t) (v * 255.0f + 0.5f);
        }
        q[3] = p[3]; // alpha untouched
    }
    return true;
}

static bool contrastSetParam(void *state, uint32_t index, float value) {
    if (state == nullptr || index != 0)
        return false;
    (*((ContrastState*) state)).amount = clampAmount(value);
    return true;
}

static float contrastGetParam(const void *state, uint32_t index) {
    if (state == nullptr || index != 0)
        return 0.0f;
    return (*((const ContrastState*) state)).amount;
}

static const FilterRow kContrastRow = {
    .kind = FILTER_CONTRAST,
    .name = "contrast",
    .createState = contrastCreateState,
    .destroyState = contrastDestroyState,
    .apply = contrastApply,
    .setParam = contrastSetParam,
    .getParam = contrastGetParam,
};

const FilterRow *Contrast_row(void) {
    return &kContrastRow;
}
