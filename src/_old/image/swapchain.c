#include "image/swapchain.h"

#include <stdlib.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Swapchain
 * ============================================================================
 * Backend-agnostic presentation swapchain coordinating double- or triple-buffered
 * surface image presentation to native display surfaces (such as NSWindow or
 * CAMetalLayer) across CPU shadows and GPU raster backends.
 *
 * Maintains a pool of 1 to 4 owned Image instances that cycle deterministically
 * through acquire and present phases under strict vsync governance. Surface
 * resizing reallocates image storage safely across the frame pool without tearing
 * or invalidating active presentation handles. Struct allocations are serviced
 * through the vexspoke typed memory arena (TYPE_SWAPCHAIN_SINGLETON) when
 * available, with an automatic calloc fallback for standalone targets.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Swapchain (image/swapchain.c)
 * LEVEL: L4 — Self-Management (display surface lifecycle and presentation)
 * ============================================================================
 * SUMMARY:
 *   Backend-agnostic swapchain managing a pool of 1 to 4 CPU shadow framebuffer
 *   images for presentation to a native display surface (NSWindow, CAMetalLayer,
 *   etc.). Supports double and triple buffering, bounded acquire/present cadences,
 *   and dynamic surface resize.
 *
 * STRUCT FIELDS (Mirroring image/swapchain.h):
 * ----------------------------------------------------------------------------
 *   void *nativeHandle;    // borrowed native handle (NSWindow, CAMetalLayer, etc.)
 *   uint32_t width;        // swapchain surface width in pixels
 *   uint32_t height;       // swapchain surface height in pixels
 *   uint32_t imageCount;   // buffer count (1 to 4)
 *   uint32_t currentIndex; // current acquired/active image index
 *   uint64_t typeId;       // block-header type id (TYPE_SWAPCHAIN_SINGLETON)
 *   bool vsync;            // vertical sync enabled flag
 *   Image *images[4];      // owned CPU shadow framebuffer images
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Swapchain_0(void)                                  : Default swapchain (1x1, double-buffered)
 *   - Swapchain_1(nativeHandle)                          : Swapchain bound to native handle
 *   - Swapchain_2(nativeHandle, count)                   : Swapchain with custom image buffer count
 *   - Swapchain_3(nativeHandle, w, h)                    : Swapchain with initial surface dimensions
 *   - Swapchain_4(nativeHandle, w, h, count)             : Fully specified swapchain instance
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Swapchain_free(self)                               : Release swapchain and owned frame images
 *   - Swapchain_acquire(self)                            : Acquire next available buffer index
 *   - Swapchain_present(self)                            : Advance swapchain buffer cycle
 *   - Swapchain_resize(self, w, h)                       : Resize all owned surface framebuffers
 *   - Swapchain_currentImage(self)                       : Query currently acquired image pointer
 *   - Swapchain_getImage(self, index)                    : Access framebuffer image by index
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - Swapchain_setWidth(self, width)                    : Mutate surface width
 *   - Swapchain_setHeight(self, height)                  : Mutate surface height
 *   - Swapchain_setImageCount(self, count)               : Mutate swapchain image buffer count
 *   - Swapchain_setCurrentIndex(self, index)             : Mutate active image index
 *   - Swapchain_setVsync(self, vsync)                    : Enable or disable vertical synchronization
 *   - Swapchain_setNativeHandle(self, nativeHandle)      : Assign underlying window or layer handle
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Swapchain_getWidth(self)                           : Query surface width in pixels
 *   - Swapchain_getHeight(self)                          : Query surface height in pixels
 *   - Swapchain_getImageCount(self)                      : Query allocated buffer count
 *   - Swapchain_getCurrentIndex(self)                    : Query active image index
 *   - Swapchain_isVsync(self)                            : Query vertical sync flag
 *   - Swapchain_getNativeHandle(self)                    : Query native window/layer handle
 *   - Swapchain_getTypeId(self)                          : Query type identity stamp
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// CONSTRUCTORS (PUBLIC & PRIVATE)

Swapchain *Swapchain_0(void) {
    return Swapchain_4(nullptr, 1, 1, 2);
}

Swapchain *Swapchain_1(void *nativeHandle) {
    return Swapchain_4(nativeHandle, 1, 1, 2);
}

Swapchain *Swapchain_2(void *nativeHandle, uint32_t count) {
    return Swapchain_4(nativeHandle, 1u, 1u, count);
}

Swapchain *Swapchain_3(void *nativeHandle, uint32_t w, uint32_t h) {
    return Swapchain_4(nativeHandle, w, h, 2);
}

Swapchain *Swapchain_4(void *nativeHandle, uint32_t w, uint32_t h, uint32_t count) {
    if (count < 1)
        count = 1;
    if (count > 4)
        count = 4;
    Swapchain *self = (Swapchain*) Memory_alloc(TYPE_SWAPCHAIN_SINGLETON, sizeof(Swapchain));
    if (!self)
        self = (Swapchain*) calloc(1, sizeof(Swapchain));
    if (!self)
        return nullptr;
    (*self).nativeHandle = nativeHandle;
    (*self).width = w;
    (*self).height = h;
    (*self).imageCount = count;
    (*self).currentIndex = 0;
    (*self).typeId = TYPE_SWAPCHAIN_SINGLETON;
    (*self).vsync = true;
    for (uint32_t i = 0; i < 4; i++) {
        if (i < count && w > 0 && h > 0) {
            (*self).images[i] = Image_2(w, h);
            if (!(*self).images[i]) {
                Swapchain_free(self);
                return nullptr;
            }
        } else {
            (*self).images[i] = nullptr;
        }
    }
    return self;
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

void Swapchain_free(Swapchain *self) {
    if (!self)
        return;
    for (uint32_t i = 0; i < 4; i++) {
        if ((*self).images[i]) {
            Image_free((*self).images[i]);
            (*self).images[i] = nullptr;
        }
    }
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

int32_t Swapchain_acquire(Swapchain *self) {
    if (!self || (*self).imageCount == 0)
        return -1;
    if ((*self).currentIndex >= (*self).imageCount)
        (*self).currentIndex = 0;
    return (int32_t) (*self).currentIndex;
}

bool Swapchain_present(Swapchain *self) {
    if (!self || (*self).imageCount == 0)
        return false;
    (*self).currentIndex = ((*self).currentIndex + 1) % (*self).imageCount;
    return true;
}

bool Swapchain_resize(Swapchain *self, uint32_t w, uint32_t h) {
    if (!self || w == 0 || h == 0)
        return false;
    if ((*self).width == w && (*self).height == h)
        return true;
    Image *newImages[4] = { nullptr, nullptr, nullptr, nullptr };
    for (uint32_t i = 0; i < (*self).imageCount; i++) {
        newImages[i] = Image_2(w, h);
        if (!newImages[i]) {
            for (uint32_t j = 0; j < i; j++)
                Image_free(newImages[j]);
            return false;
        }
    }
    for (uint32_t i = 0; i < (*self).imageCount; i++) {
        if ((*self).images[i])
            Image_free((*self).images[i]);
        (*self).images[i] = newImages[i];
    }
    (*self).width = w;
    (*self).height = h;
    (*self).currentIndex = 0;
    return true;
}

Image *Swapchain_currentImage(Swapchain *self) {
    if (!self || (*self).imageCount == 0)
        return nullptr;
    if ((*self).currentIndex >= (*self).imageCount)
        return nullptr;
    return (*self).images[(*self).currentIndex];
}

Image *Swapchain_getImage(const Swapchain *self, uint32_t index) {
    if (!self || index >= (*self).imageCount || index >= 4)
        return nullptr;
    return (*self).images[index];
}

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void Swapchain_setWidth(Swapchain *self, uint32_t width) {
    if (!self)
        return;
    if (width != (*self).width && (*self).height > 0 && width > 0)
        Swapchain_resize(self, width, (*self).height);
    else
        (*self).width = width;
}

;;SETTER
void Swapchain_setHeight(Swapchain *self, uint32_t height) {
    if (!self)
        return;
    if (height != (*self).height && (*self).width > 0 && height > 0)
        Swapchain_resize(self, (*self).width, height);
    else
        (*self).height = height;
}

;;SETTER
void Swapchain_setImageCount(Swapchain *self, uint32_t count) {
    if (!self)
        return;
    if (count < 1)
        count = 1;
    if (count > 4)
        count = 4;
    if (count == (*self).imageCount)
        return;
    if (count < (*self).imageCount) {
        for (uint32_t i = count; i < (*self).imageCount; i++) {
            if ((*self).images[i]) {
                Image_free((*self).images[i]);
                (*self).images[i] = nullptr;
            }
        }
    } else {
        for (uint32_t i = (*self).imageCount; i < count; i++) {
            if ((*self).width > 0 && (*self).height > 0)
                (*self).images[i] = Image_2((*self).width, (*self).height);
            else
                (*self).images[i] = nullptr;
        }
    }
    (*self).imageCount = count;
    if ((*self).currentIndex >= count)
        (*self).currentIndex = 0;
}

;;SETTER
void Swapchain_setCurrentIndex(Swapchain *self, uint32_t index) {
    if (!self)
        return;
    if ((*self).imageCount > 0 && index < (*self).imageCount)
        (*self).currentIndex = index;
}

;;SETTER
void Swapchain_setVsync(Swapchain *self, bool vsync) {
    if (!self)
        return;
    (*self).vsync = vsync;
}

;;SETTER
void Swapchain_setNativeHandle(Swapchain *self, void *nativeHandle) {
    if (!self)
        return;
    (*self).nativeHandle = nativeHandle;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
uint32_t Swapchain_getWidth(const Swapchain *self) {
    return self ? (*self).width : 0;
}

;;GETTER
uint32_t Swapchain_getHeight(const Swapchain *self) {
    return self ? (*self).height : 0;
}

;;GETTER
uint32_t Swapchain_getImageCount(const Swapchain *self) {
    return self ? (*self).imageCount : 0;
}

;;GETTER
uint32_t Swapchain_getCurrentIndex(const Swapchain *self) {
    return self ? (*self).currentIndex : 0;
}

;;GETTER
bool Swapchain_isVsync(const Swapchain *self) {
    return self ? (*self).vsync : false;
}

;;GETTER
void *Swapchain_getNativeHandle(const Swapchain *self) {
    return self ? (*self).nativeHandle : nullptr;
}

;;GETTER
uint64_t Swapchain_getTypeId(const Swapchain *self) {
    return self ? (*self).typeId : 0;
}
