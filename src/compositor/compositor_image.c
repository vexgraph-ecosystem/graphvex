#include "compositor/compositor_image.h"

#include <math.h>
#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/* Explicit Image boundary for the CPU compositor. Borrowed
 * straight-sRGB RGBA8 CPU shadows become owned origin-aware linear-premult
 * surfaces; export reverses that conversion into a newly owned Image. No GPU
 * readback, submission, lifetime retention, or implicit color policy. */
;;OVERVIEW
/* Public: fromImage/toImage with failure-atomic out parameters.
 * Private: srgbDecode/srgbEncode implement the transfer function; export
 * clamps HDR only at the byte boundary. No class fields owned by this adapter. */

static float srgbDecode(float encoded) {
    return encoded <= 0.04045f ? encoded / 12.92f :
        powf((encoded + 0.055f) / 1.055f, 2.4f);
}
static uint8_t srgbEncode(float linear) {
    if (linear <= 0)
        return 0;
    if (linear >= 1)
        return 255;
    float encoded = linear <= 0.0031308f ? linear * 12.92f :
        1.055f * powf(linear, 1.0f / 2.4f) - 0.055f;
    return (uint8_t) lroundf(encoded * 255.0f);
}

CompositorStatus CompositorSurface_fromImage(const Image *image,
                                             int32_t x, int32_t y,
                                             CompositorSurface **out) {
    if (!out || !Image_isValid(image))
        return COMPOSITOR_INVALID;
    if (Image_format(image) != IMAGE_FORMAT_RGBA8)
        return COMPOSITOR_UNSUPPORTED;
    const uint8_t *pixels = Image_pixels(image);
    if (!pixels)
        return COMPOSITOR_INVALID;
    CompositorBounds b = {x, y, Image_width(image), Image_height(image)};
    CompositorSurface *surface = nullptr;
    CompositorStatus status = CompositorSurface_create(b, &surface);
    if (status != COMPOSITOR_OK)
        return status;
    size_t stride = Image_stride(image);
    if (stride < (size_t) b.width * 4 ||
        (b.height > 1 && stride > (SIZE_MAX - (size_t) b.width * 4) / (b.height - 1))) {
        CompositorSurface_destroy(surface);
        return COMPOSITOR_LIMIT;
    }
    float *dest = CompositorSurface_pixels(surface);
    for (uint32_t iy = 0; iy < b.height; ++iy) {
        const uint8_t *row = pixels + (size_t) iy * stride;
        for (uint32_t ix = 0; ix < b.width; ++ix) {
            const uint8_t *p = row + (size_t) ix * 4;
            float *q = dest + 4 * ((size_t) iy * b.width + ix);
            float alpha = p[3] / 255.0f;
            for (unsigned c = 0; c < 3; ++c)
                q[c] = srgbDecode(p[c] / 255.0f) * alpha;
            q[3] = alpha;
        }
    }
    *out = surface;
    return COMPOSITOR_OK;
}

CompositorStatus CompositorSurface_toImage(const CompositorSurface *surface,
                                           Image **out) {
    if (!out)
        return COMPOSITOR_INVALID;
    CompositorStatus status = CompositorSurface_validate(surface);
    if (status != COMPOSITOR_OK)
        return status;
    CompositorBounds b = CompositorSurface_bounds(surface);
    if (!b.width)
        return COMPOSITOR_INVALID;
    Image *image = Image_2(b.width, b.height);
    if (!image)
        return COMPOSITOR_NO_MEMORY;
    if (!Image_ensureShadow(image, b.width, b.height)) {
        Image_destroy(image);
        return COMPOSITOR_NO_MEMORY;
    }
    const float *source = CompositorSurface_constPixels(surface);
    uint8_t *pixels = Image_pixels(image);
    size_t stride = Image_stride(image);
    for (uint32_t y = 0; y < b.height; ++y) {
        for (uint32_t x = 0; x < b.width; ++x) {
            const float *p = source + 4 * ((size_t) y * b.width + x);
            uint8_t *q = pixels + (size_t) y * stride + (size_t) x * 4;
            for (unsigned c = 0; c < 3; ++c)
                q[c] = p[3] > 0 ? srgbEncode(p[c] / p[3]) : 0;
            q[3] = (uint8_t) lroundf(p[3] * 255.0f);
        }
    }
    *out = image;
    return COMPOSITOR_OK;
}
