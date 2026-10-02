#include "vulkan/surface.h"

#include <stdlib.h>

// graphvex R3 — vulkan/surface.c
// The presentation seam. Owns a retained present Image; presents by handing it
// to the host. There is deliberately NO swapchain here.

struct Surface {
    void *native;      // borrowed CAMetalLayer / HWND / xcb window
    uint32_t width;
    uint32_t height;
    Image *present;    // the retained target we render into
    bool presented;    // diagnostic
};

Surface *Surface_0(void) { return Surface_2(NULL, 0, 0); }

Surface *Surface_2(void *native, uint32_t width, uint32_t height) {
    Surface *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    (*s).native = native;
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

bool Surface_present(Surface *surface) {
    if (!surface || !(*surface).present) return false;
    // TODO(seam): blit (*surface).present into the borrowed native destination
    // (CAMetalLayer on Apple). NO swapchain present — the host owns the drawable.
    (*surface).presented = false;
    return false;
}
