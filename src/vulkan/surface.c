#include "vulkan/surface.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/intention.h"

;;INTENTION("Surface caps coalesce render/present demand without sleeping or "
            "retiming independent scene workers. The host polls pending demand; "
            "offscreen capture can refresh pixels without bypassing publication caps.")

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Surface (vulkan/surface.c)
 * ============================================================================
 * The presentation seam. *** NO SWAPCHAIN. *** A Surface is a host-borrowed
 * native destination (a CAMetalLayer on Apple, HWND on Windows, an xcb window
 * on Linux) plus ONE retained present Image we render into. Surface_present()
 * hands the completed image to the host seam; we never create, acquire, or
 * present a VkSwapchainKHR (the Single-Seam Canvas Law).
 *
 * A surface may carry several boards (scene + content). Surface_revalidate is
 * the one call a Frame makes: revalidate each attached board — which renders
 * its scene/content into the board — then present the finished image. Boards
 * are BORROWED; the surface never frees one.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Surface (vulkan/surface.c)
 * ============================================================================
 * Host-borrowed native destination + one retained present Image + borrowed
 * boards.
 *
 * STRUCT FIELDS:
 * ----------------------------------------------------------------------------
 *   void    *native;      // borrowed CAMetalLayer / HWND / xcb window
 *   uint32_t width, height; // native px
 *   Image   *present;     // the retained target we render into (owned)
 *   bool     presented;   // diagnostic: did the last present succeed
 *   SurfacePresentFn presentFn; // borrowed host blit (NULL = offscreen)
 *   void    *presentUser; // passed to presentFn
 *   Board  **boards;      // borrowed layers (owned by the caller)
 *   int      boardCount, boardCap;
 *
 * FUNCTION REGISTRY (exported by vulkan/surface.h):
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Surface_0, Surface_2, Surface_destroy
 * Core:
 *   - Surface_resize, Surface_onPresent, Surface_present
 * Queries / Getters:
 *   - Surface_width, Surface_height, Surface_isValid, Surface_handle,
 *     Surface_presentImage
 * Revalidation:
 *   - Surface_addBoard, Surface_removeBoard, Surface_revalidate
 * ============================================================================
 */

struct Surface {
    void *native;      // borrowed CAMetalLayer / HWND / xcb window
    uint32_t width;
    uint32_t height;
    Image *present;    // the retained target we render into
    bool presented;    // diagnostic
    SurfacePresentFn presentFn;   // borrowed host blit (nullable = offscreen)
    void *presentUser;            // passed to presentFn
    Board **boards;    // borrowed layers composited into the present image
    int boardCount, boardCap;
    int fpsCap;
    uint64_t lastPresent, presentCount;
    bool pending;
    SurfaceClockFn clockFn;
    void *clockUser;
};

Surface *Surface_0(void) { return Surface_2(NULL, 0, 0); }

Surface *Surface_2(void *native, uint32_t width, uint32_t height) {
    Surface *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    (*s).native = native;
    (*s).fpsCap = -1;
    (*s).width = width;
    (*s).height = height;
    ImageDesc d = {width ? width : 1u, height ? height : 1u, IMAGE_FORMAT_RGBA8,
                   IMAGE_USAGE_RENDER | IMAGE_USAGE_TRANSFER | IMAGE_USAGE_SAMPLED};
    (*s).present = Image_new(&d);
    if (!(*s).present) {
        free(s);
        return NULL;
    }
    return s;
}

void Surface_destroy(Surface *surface) {
    if (!surface) return;
    Image_destroy((*surface).present);
    free((*surface).boards);
    free(surface);
}

bool Surface_resize(Surface *surface, uint32_t width, uint32_t height) {
    if (!surface || !(*surface).present) return false;
    (*surface).width = width;
    (*surface).height = height;
    return Image_resize((*surface).present, width ? width : 1u, height ? height : 1u);
}

uint32_t Surface_width(const Surface *surface) { return surface ? (*surface).width : 0u; }
uint32_t Surface_height(const Surface *surface) { return surface ? (*surface).height : 0u; }
bool Surface_isValid(const Surface *surface) { return surface && (*surface).present != NULL; }
void *Surface_handle(const Surface *surface) { return surface ? (*surface).native : NULL; }
Image *Surface_presentImage(Surface *surface) { return surface ? (*surface).present : NULL; }

void Surface_onPresent(Surface *surface, SurfacePresentFn fn, void *userdata) {
    if (!surface) return;
    (*surface).presentFn = fn;
    (*surface).presentUser = userdata;
}

static uint64_t surfaceNow(Surface *surface) {
    if ((*surface).clockFn) return (*surface).clockFn((*surface).clockUser);
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static bool surfaceDue(Surface *surface) {
    return (*surface).fpsCap == -1 || !(*surface).presentCount ||
        surfaceNow(surface) - (*surface).lastPresent >=
        (1000000000ULL + (uint64_t)(*surface).fpsCap - 1) / (uint64_t)(*surface).fpsCap;
}

void Surface_setFPSCap(Surface *surface, int fps) {
    if (!surface || (fps != -1 && fps <= 0) || (*surface).fpsCap == fps) return;
    (*surface).fpsCap = fps;
}
int Surface_getFPSCap(const Surface *surface) { return surface ? (*surface).fpsCap : 0; }
uint64_t Surface_getPresentCount(const Surface *surface) { return surface ? (*surface).presentCount : 0; }
void Surface_setClock(Surface *surface, SurfaceClockFn fn, void *userdata) {
    if (!surface) return;
    (*surface).clockFn = fn; (*surface).clockUser = userdata;
    (*surface).lastPresent = 0; (*surface).presentCount = 0;
}

bool Surface_present(Surface *surface) {
    if (!surface || !(*surface).present) return false;
    if (!surfaceDue(surface)) { (*surface).pending = true; return false; }
    // The host owns the drawable; we only hand it the finished image. With no
    // blit installed (an offscreen surface) there is nowhere to present.
    (*surface).presented = (*surface).presentFn
        ? (*surface).presentFn(surface, (*surface).presentUser)
        : false;
    if ((*surface).presented) {
        (*surface).lastPresent = surfaceNow(surface);
        (*surface).presentCount++;
        (*surface).pending = false;
    }
    return (*surface).presented;
}

// ── revalidation ────────────────────────────────────────────────────────────
void Surface_addBoard(Surface *surface, Board *board) {
    if (!surface || !board) return;
    if ((*surface).boardCount == (*surface).boardCap) {
        int cap = (*surface).boardCap ? (*surface).boardCap * 2 : 2;
        Board **grown = realloc((*surface).boards, (size_t)cap * sizeof *grown);
        if (!grown) return;
        (*surface).boards = grown;
        (*surface).boardCap = cap;
    }
    (*surface).boards[(*surface).boardCount++] = board;
}

void Surface_removeBoard(Surface *surface, Board *board) {
    if (!surface || !board) return;
    for (int i = 0; i < (*surface).boardCount; i++) {
        if ((*surface).boards[i] != board) continue;
        memmove(&(*surface).boards[i], &(*surface).boards[i + 1],
                (size_t)((*surface).boardCount - i - 1) * sizeof *(*surface).boards);
        (*surface).boardCount--;
        return;
    }
}

// The one call a Frame makes: revalidate each board (which renders its
// scene/content), then present the finished image to the host.
void Surface_revalidate(Surface *surface) {
    if (!surface) return;
    (*surface).pending = true;
    Surface_poll(surface);
}

void Surface_poll(Surface *surface) {
    if (!surface || !(*surface).pending || !surfaceDue(surface)) return;
    Surface_revalidateNow(surface);
}

void Surface_revalidateNow(Surface *surface) {
    if (!surface) return;
    for (int i = 0; i < (*surface).boardCount; i++)
        Board_revalidate((*surface).boards[i]);
    Surface_present(surface);
}
