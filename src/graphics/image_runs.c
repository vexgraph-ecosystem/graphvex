#include "graphics/image_runs.h"
#include "image.h"
#include "annotation/definition.h"
#include "annotation/overview.h"

#include <limits.h>
#include <math.h>

_Static_assert(SIZE_MAX / 4 >= UINT32_MAX, "RGBA row Bytes require supported 64-bit size_t");

;;DEFINITION
/* ImageRuns converts a borrowed straight RGBA8 CPU shadow into nearest-sampled
 * horizontal solid-color runs for correctness-first backend image rendering. */
;;OVERVIEW
/* Stateless module. Public: ImageRuns_visit. No fields/resources. Private:
 * validRect checks finite positive geometry; sample reads stride-aware RGBA8.
 * Iteration is bounded and synchronous; failed callbacks may leave partial
 * consumer output. This reference path is not production texture-upload proof. */

static bool validRect(Rect rect) {
    return isfinite(rect.x) && isfinite(rect.y) && isfinite(rect.w) &&
           isfinite(rect.h) && rect.w > 0 && rect.h > 0 &&
           isfinite(rect.x + rect.w) && isfinite(rect.y + rect.h);
}

// Reads one RGBA8 sample, returning transparent black outside the image extent.
static Color sample(const uint8_t *pixels, size_t stride, uint32_t width,
                    uint32_t height, Rect destination, int32_t x, int32_t y) {
    double u = ((double) x + 0.5 - destination.x) / destination.w;
    double v = ((double) y + 0.5 - destination.y) / destination.h;
    uint32_t sx = (uint32_t) fmin(fmax(floor(u * width), 0), width - 1);
    uint32_t sy = (uint32_t) fmin(fmax(floor(v * height), 0), height - 1);
    const uint8_t *pixel = pixels + (size_t) sy * stride + (size_t) sx * 4;
    return COLOR_RGBA(pixel[0], pixel[1], pixel[2], pixel[3]);
}

// Visits contiguous horizontal runs of equal sampled color within the clipped destination.
bool ImageRuns_visit(const Image *image, Rect destination, Rect clip,
                     ImageRunsFn visit, void *context) {
    if (!image || !visit || !Image_isValid(image) || !validRect(destination) ||
        !validRect(clip) || Image_format(image) != IMAGE_FORMAT_RGBA8)
        return false;
    const uint8_t *pixels = Image_pixels(image);
    uint32_t width = Image_width(image), height = Image_height(image);
    size_t stride = Image_stride(image);
    if (!pixels || stride < (size_t) width * 4 ||
        (height > 1 && stride > (SIZE_MAX - (size_t) width * 4) / (height - 1)))
        return false;
    Rect visible = Rect_intersect(destination, clip);
    if (Rect_isEmpty(visible))
        return true;
    double left = ceil((double) visible.x - 0.5);
    double top = ceil((double) visible.y - 0.5);
    double right = ceil((double) visible.x + visible.w - 0.5);
    double bottom = ceil((double) visible.y + visible.h - 0.5);
    if (left < INT32_MIN || top < INT32_MIN || right > INT32_MAX || bottom > INT32_MAX ||
        right - left > 16777216 || bottom - top > 16777216 ||
        (right - left) * (bottom - top) > 16777216)
        return false;
    int32_t x0 = (int32_t) left, y0 = (int32_t) top;
    int32_t x1 = (int32_t) right, y1 = (int32_t) bottom;
    for (int32_t y = y0; y < y1; ++y) {
        int32_t start = x0;
        while (start < x1) {
            Color color = sample(pixels, stride, width, height, destination, start, y);
            int32_t end = start + 1;
            while (end < x1 && sample(pixels, stride, width, height, destination, end, y) == color)
                ++end;
            if (Color_alpha(color)) {
                Rect run = {(float) start, (float) y, (float) (end - start), 1};
                if (!visit(run, color, context))
                    return false;
            }
            start = end;
        }
    }
    return true;
}
