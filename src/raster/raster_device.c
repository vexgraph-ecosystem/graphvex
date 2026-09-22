#include "raster/raster_device.h"

#include <stdlib.h>

#include "lang/image.h"
#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: RasterDevice (raster/raster_device.c)
 * ============================================================================
 * The raster dialect of the Device contract — the CPU software device. It owns
 * one Image: the CPU framebuffer it presents. present() just marks the frame
 * done (there is nothing to swap); resize() re-grows the framebuffer. Because
 * it needs no GPU, no window, and no loader, it is the headless/CI dialect and
 * the pixel-exact reference the GPU dialects are checked against.
 *
 * native() hands back the framebuffer Image — the CPU pixels every headless
 * consumer reads. Zero steady-state allocation beyond the framebuffer itself.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: RasterDevice (raster/raster_device.c)
 * LEVEL: L4 — Self-Management (owns the CPU framebuffer across the process)
 * ============================================================================
 * SUMMARY:
 *   Raster DeviceRow. createState allocates the CPU framebuffer; present marks
 *   the frame done; resize re-grows it. Fully headless.
 *
 * STRUCT FIELDS: none — the state is the private RasterState helper.
 *
 * PRIVATE HELPERS (kept file-local, no external API):
 * ----------------------------------------------------------------------------
 *   RasterState
 *     uint32_t width, height;  // framebuffer extent, native px
 *     Image *target;           // OWNED CPU framebuffer (the drawable)
 *     bool presented;          // last present succeeded
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Private Core Functions: (.c static)
 *   - rasterCreateState(desc) / rasterDestroyState(state)
 *   - rasterPresent(state) / rasterResize(state, w, h)
 *   - rasterWidth(state) / rasterHeight(state) / rasterIsReady(state) / rasterNative(state)
 * ============================================================================
 */

// SLOT RECORD state for LANG_BACKEND_RASTER (owned by the row's createState).
typedef struct RasterState {
    uint32_t width;      // framebuffer extent, native px
    uint32_t height;
    Image *target;       // OWNED CPU framebuffer (the drawable)
    bool presented;      // last present succeeded
} RasterState;

static bool rasterEnsureTarget(RasterState *state, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return false;
    if ((*state).target != nullptr && (*state).width == width && (*state).height == height)
        return true;
    Image *fresh = Image_2(width, height);
    if (fresh == nullptr)
        return false;
    Image_destroy((*state).target);
    (*state).target = fresh;
    (*state).width = width;
    (*state).height = height;
    return true;
}

static void *rasterCreateState(const DeviceDesc *desc) {
    RasterState *state = (RasterState*) calloc(1, sizeof(RasterState));
    if (state == nullptr)
        return nullptr;
    uint32_t w = (desc != nullptr && (*desc).width > 0) ? (*desc).width : 1u;
    uint32_t h = (desc != nullptr && (*desc).height > 0) ? (*desc).height : 1u;
    if (!rasterEnsureTarget(state, w, h)) {
        free(state);
        return nullptr;
    }
    return state;
}

static void rasterDestroyState(void *state) {
    if (state == nullptr)
        return;
    Image_destroy((*((RasterState*) state)).target);
    free(state);
}

static bool rasterPresent(void *state) {
    if (state == nullptr)
        return false;
    (*((RasterState*) state)).presented = true;
    return true; // the CPU framebuffer is already "on screen" — nothing to swap.
}

static bool rasterResize(void *state, uint32_t width, uint32_t height) {
    if (state == nullptr)
        return false;
    return rasterEnsureTarget((RasterState*) state, width, height);
}

static uint32_t rasterWidth(const void *state) {
    return state ? (*((const RasterState*) state)).width : 0u;
}

static uint32_t rasterHeight(const void *state) {
    return state ? (*((const RasterState*) state)).height : 0u;
}

static bool rasterIsReady(const void *state) {
    return state != nullptr && (*((const RasterState*) state)).target != nullptr;
}

static void *rasterNative(const void *state) {
    return state ? (void*) (*((const RasterState*) state)).target : nullptr;
}

static const DeviceRow kRasterRow = {
    .backend = LANG_BACKEND_RASTER,
    .name = "raster",
    .createState = rasterCreateState,
    .destroyState = rasterDestroyState,
    .present = rasterPresent,
    .resize = rasterResize,
    .width = rasterWidth,
    .height = rasterHeight,
    .isReady = rasterIsReady,
    .native = rasterNative,
};

const DeviceRow *Raster_row(void) {
    return &kRasterRow;
}
