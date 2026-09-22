#ifndef IMAGE_SWAPCHAIN_H
#define IMAGE_SWAPCHAIN_H

#include <stdbool.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "../graphics/type.h"
#include "image/image.h"

// image/swapchain.h — Backend-agnostic presentation swapchain (CPU shadow stub).
//
// Manages a pool of 1 to 4 CPU shadow framebuffer images for presentation
// to a native display surface. Supports acquire/present buffer cycling and
// surface resizing.

typedef struct Swapchain {
    void *nativeHandle;    // borrowed native handle (NSWindow, CAMetalLayer, etc.)
    uint32_t width;        // swapchain surface width in pixels
    uint32_t height;       // swapchain surface height in pixels
    uint32_t imageCount;   // buffer count (1 to 4)
    uint32_t currentIndex; // current acquired/active image index
    uint64_t typeId;       // block-header type id (TYPE_SWAPCHAIN_SINGLETON)
    bool vsync;            // vertical sync enabled flag
    Image *images[4];      // owned CPU shadow framebuffer images
} Swapchain;

// Constructors
Swapchain *Swapchain_0(void);
Swapchain *Swapchain_1(void *nativeHandle);
Swapchain *Swapchain_2(void *nativeHandle, uint32_t count);
Swapchain *Swapchain_3(void *nativeHandle, uint32_t w, uint32_t h);
Swapchain *Swapchain_4(void *nativeHandle, uint32_t w, uint32_t h, uint32_t count);

// Core functions
void Swapchain_free(Swapchain *self);
int32_t Swapchain_acquire(Swapchain *self);
bool Swapchain_present(Swapchain *self);
bool Swapchain_resize(Swapchain *self, uint32_t w, uint32_t h);
Image *Swapchain_currentImage(Swapchain *self);
Image *Swapchain_getImage(const Swapchain *self, uint32_t index);

// Symmetric Setters
void Swapchain_setWidth(Swapchain *self, uint32_t width);
void Swapchain_setHeight(Swapchain *self, uint32_t height);
void Swapchain_setImageCount(Swapchain *self, uint32_t count);
void Swapchain_setCurrentIndex(Swapchain *self, uint32_t index);
void Swapchain_setVsync(Swapchain *self, bool vsync);
void Swapchain_setNativeHandle(Swapchain *self, void *nativeHandle);

// Symmetric Getters
uint32_t Swapchain_getWidth(const Swapchain *self);
uint32_t Swapchain_getHeight(const Swapchain *self);
uint32_t Swapchain_getImageCount(const Swapchain *self);
uint32_t Swapchain_getCurrentIndex(const Swapchain *self);
bool Swapchain_isVsync(const Swapchain *self);
void *Swapchain_getNativeHandle(const Swapchain *self);
uint64_t Swapchain_getTypeId(const Swapchain *self);

#define Swapchain(...) CONSTRUCTOR_DISPATCH(Swapchain, __VA_ARGS__)

#endif
