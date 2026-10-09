#include "image.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "exception/throw.h"

#include <stdlib.h>
#include <string.h>

// graphvex R3 — image.c
;;DEFINITION
/* Image is the backend-neutral pixel identity: optional CPU RGBA shadow plus
 * an owned reference to an immutable SampledImage. GPU-only filter outputs need
 * no readback to become drawable. CPU edits invalidate the sampled reference;
 * a recorded frame retains its own reference until its fence completes. */
;;OVERVIEW
/* CLASS: Image. Fields: width,height (logical native extent), capW,capH (CPU
 * allocation/stride), format,usage (pixel contract), pixels (owned CPU shadow),
 * native (borrowed dialect handle), ioSurface (borrowed native surface), layer
 * (legacy atlas index), gpuResource (owned opaque texture), gpuDevice (borrowed
 * owner identity), gpuDescriptor (borrowed from texture), gpuRetain/gpuRelease
 * (reference callbacks). Public: existing
 * constructors/new/destroy; resize/ensureShadow/upload/fill; dimensions/format/
 * usage/stride/pixels/validity/native/IOSurface/layer; bindGpu/clearGpu and
 * gpuResource/gpuDevice/gpuDescriptor/isDrawable. Admission validates resource
 * dimensions/readiness in its owning driver; bind rejects incomplete fn-tables.
 * Owner-thread or external synchronization. Raw pixel edits clear the sampled
 * binding explicitly; borrowed native fields carry no lifetime ownership. */

struct Image {
    uint32_t width;
    uint32_t height;
    uint32_t capW;         // allocation width (grow-only); row stride is capW*4
    uint32_t capH;         // allocation height
    uint32_t format;
    uint32_t usage;
    uint8_t *pixels;       // CPU shadow (RGBA8)
    void *native;          // opaque dialect handle
    void *ioSurface;       // opaque IOSurfaceRef
    uint32_t layer;        // atlas layer / sampler index
    void *gpuResource; // owned opaque texture; renderer retains independently
    void *gpuDevice; // borrowed driver/device identity
    void *gpuDescriptor; // borrowed descriptor from owned resource
    ImageGpuRefFn gpuRetain;
    ImageGpuRefFn gpuRelease;
};

static void clamp_dims(uint32_t *w, uint32_t *h) {
    if (*w == 0) *w = 1;
    if (*h == 0) *h = 1;
}

// Allocates image metadata and normalizes zero dimensions to one; pixel storage is lazy.
Image *Image_new(const ImageDesc *desc) {
    Image *img = calloc(1, sizeof *img);
    if (!img) return nullptr;
    (*img).width = desc ? (*desc).width : 1;
    (*img).height = desc ? (*desc).height : 1;
    (*img).format = desc ? (*desc).format : IMAGE_FORMAT_RGBA8;
    (*img).usage = desc ? (*desc).usage : IMAGE_USAGE_NONE;
    clamp_dims(&(*img).width, &(*img).height);
    return img;
}

// Creates an empty 1x1 RGBA image descriptor with no usage flags.
Image *Image_0(void) { return Image_new(nullptr); }
// Creates an RGBA image descriptor with the requested dimensions and no usage flags.
Image *Image_2(uint32_t width, uint32_t height) {
    ImageDesc d = {width, height, IMAGE_FORMAT_RGBA8, IMAGE_USAGE_NONE};
    return Image_new(&d);
}
// Creates an image descriptor with explicit dimensions, format, and usage.
Image *Image_4(uint32_t width, uint32_t height, uint32_t format, uint32_t usage) {
    ImageDesc d = {width, height, format, usage};
    return Image_new(&d);
}

// Releases GPU binding and owned CPU pixels, then frees metadata; a failed GPU release preserves the image.
void Image_destroy(Image *image) {
    if (!image) return;
    if (!Image_clearGpu(image))
        return;
    free((*image).pixels);
    free(image);
}

// Ensures CPU RGBA shadow capacity, growing only when needed and updating logical extent.
bool Image_ensureShadow(Image *image, uint32_t width, uint32_t height) {
    if (!image) return false;
    clamp_dims(&width, &height);
    if ((uint64_t) width * height > SIZE_MAX / 4u)
        return false;
    if (!Image_clearGpu(image))
        return false;
    // GROW-ONLY: reuse the existing allocation whenever it already fits, so a
    // shrinking viewport (or a resize wobble) never reallocs and never refills.
    if ((*image).pixels && width <= (*image).capW && height <= (*image).capH) {
        (*image).width = width;
        (*image).height = height;
        return true;
    }
    uint32_t nw = (*image).capW > width ? (*image).capW : width;
    uint32_t nh = (*image).capH > height ? (*image).capH : height;
    while (nw < width) nw += nw / 2 + 64;
    while (nh < height) nh += nh / 2 + 64;
    if ((uint64_t) nw * nh > SIZE_MAX / 4u)
        return false;
    uint8_t *grown = realloc((*image).pixels, (size_t)nw * (size_t)nh * 4u);
    if (!grown) return false;
    (*image).pixels = grown;
    (*image).capW = nw;
    (*image).capH = nh;
    (*image).width = width;
    (*image).height = height;
    return true;
}

// Resizes the image's CPU shadow through the capacity-preserving ensure path.
bool Image_resize(Image *image, uint32_t width, uint32_t height) {
    return Image_ensureShadow(image, width, height);
}

// Copies tightly packed RGBA rows into the destination shadow, honoring its padded row stride.
bool Image_upload(const uint8_t *rgba, uint32_t width, uint32_t height, Image *dest) {
    if (!dest || !rgba) return false;
    if (!Image_ensureShadow(dest, width, height)) return false;
    size_t stride = (size_t)((*dest).capW) * 4u;
    for (uint32_t y = 0; y < height; y++) {
        memcpy((*dest).pixels + (size_t)y * stride, rgba + (size_t)y * width * 4u, (size_t)width * 4u);
    }
    return true;
}

// Fills the current logical extent of the CPU shadow with one RGBA color.
void Image_fill(Image *image, Color color) {
    if (!image) return;
    if (!Image_clearGpu(image))
        return;
    if (!(*image).pixels && !Image_ensureShadow(image, (*image).width, (*image).height)) return;
    uint8_t r = (uint8_t)Color_red(color);
    uint8_t g = (uint8_t)Color_green(color);
    uint8_t b = (uint8_t)Color_blue(color);
    uint8_t a = (uint8_t)Color_alpha(color);
    size_t stride = (size_t)((*image).capW) * 4u;
    for (uint32_t y = 0; y < (*image).height; y++) {
        uint8_t *row = (*image).pixels + (size_t)y * stride;
        for (uint32_t x = 0; x < (*image).width; x++) {
            row[x * 4 + 0] = r;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = b;
            row[x * 4 + 3] = a;
        }
    }
}

// Returns the logical image width, or zero when image is null.
uint32_t Image_width(const Image *image) { return image ? (*image).width : 0u; }
// Returns the logical image height, or zero when image is null.
uint32_t Image_height(const Image *image) { return image ? (*image).height : 0u; }
// Returns the declared pixel format, or the RGBA8 default for null.
uint32_t Image_format(const Image *image) { return image ? (*image).format : IMAGE_FORMAT_RGBA8; }
// Returns the declared usage flags, or IMAGE_USAGE_NONE for null.
uint32_t Image_usage(const Image *image) { return image ? (*image).usage : IMAGE_USAGE_NONE; }
// Returns allocated RGBA row stride in bytes, or zero for null.
size_t Image_stride(const Image *image) { return image ? (size_t)((*image).capW) * 4u : 0u; }
// Returns the mutable CPU pixel buffer, or nullptr when image is null.
uint8_t *Image_pixels(const Image *image) { return image ? (*image).pixels : nullptr; }
// Reports whether the logical image extent is nonempty.
bool Image_isValid(const Image *image) { return image && (*image).width > 0 && (*image).height > 0; }

// Returns the borrowed opaque backend-native handle.
void *Image_native(const Image *image) { return image ? (*image).native : nullptr; }
// Returns the borrowed opaque IOSurface handle.
void *Image_iosurface(const Image *image) { return image ? (*image).ioSurface : nullptr; }
// Stores a borrowed opaque backend-native handle without taking ownership.
void Image_setNative(Image *image, void *native) { if (image) (*image).native = native; }
// Stores a borrowed opaque IOSurface handle without taking ownership.
void Image_setIOSurface(Image *image, void *ioSurface) { if (image) (*image).ioSurface = ioSurface; }
// Returns the image's atlas or sampler layer index.
uint32_t Image_layer(const Image *image) { return image ? (*image).layer : 0u; }
// Sets the image's atlas or sampler layer index.
void Image_setLayer(Image *image, uint32_t layer) { if (image) (*image).layer = layer; }

// Returns the owned opaque GPU resource pointer.
void *Image_gpuResource(const Image *image) { return image ? (*image).gpuResource : nullptr; }
// Returns the borrowed identity of the GPU device that owns the resource.
void *Image_gpuDevice(const Image *image) { return image ? (*image).gpuDevice : nullptr; }
// Returns the borrowed GPU sampling descriptor.
void *Image_gpuDescriptor(const Image *image) { return image ? (*image).gpuDescriptor : nullptr; }
// Reports whether the image has a valid extent and either CPU pixels or a GPU descriptor.
bool Image_isDrawable(const Image *image) {
    return Image_isValid(image) && (Image_pixels(image) || Image_gpuDescriptor(image));
}
// Releases the retained GPU resource; on release failure the existing binding remains intact.
bool Image_clearGpu(Image *image) {
    if (!image)
        return false;
    if ((*image).gpuResource && !(*image).gpuRelease((*image).gpuResource))
        return false;
    (*image).gpuResource = nullptr;
    (*image).gpuDevice = nullptr;
    (*image).gpuDescriptor = nullptr;
    (*image).gpuRetain = nullptr;
    (*image).gpuRelease = nullptr;
    return true;
}
// Retains and installs a complete GPU binding, replacing the previous binding only after admission.
bool Image_bindGpu(Image *image, void *resource, void *device, void *descriptor,
                   ImageGpuRefFn retain, ImageGpuRefFn release) {
    if (!image || !resource || !device || !descriptor || !retain || !release) {
        THROW("Image GPU binding requires a complete validated resource");
        return false;
    }
    if ((*image).gpuResource == resource && (*image).gpuDevice == device &&
        (*image).gpuDescriptor == descriptor && (*image).gpuRetain == retain && (*image).gpuRelease == release)
        return true;
    if (!retain(resource)) {
        THROW("Image GPU reference rejected");
        return false;
    }
    if (!Image_clearGpu(image)) {
        release(resource);
        return false;
    }
    (*image).gpuResource = resource;
    (*image).gpuDevice = device;
    (*image).gpuDescriptor = descriptor;
    (*image).gpuRetain = retain;
    (*image).gpuRelease = release;
    return true;
}
