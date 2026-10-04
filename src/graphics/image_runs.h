#ifndef GRAPHVEX_IMAGE_RUNS_H
#define GRAPHVEX_IMAGE_RUNS_H

#include "graphics/graphics.h"

// Correctness-first CPU-shadow image adapter shared by raster and Vulkan.
// Samples a whole RGBA8 image with nearest pixel centers into horizontal color
// runs, clipped to the destination/viewport rectangle. Rounded masking and
// source-over remain the consumer's responsibility. No allocation/texture upload.
// Borrow image and callback context synchronously; no retained references.
// Validate all geometry/format/stride/work bounds before callbacks. A false
// consumer result stops iteration; previous consumer writes are not rolled back.
// Cold/reference path, not an optimized texture sampler; 16M visible-pixel budget.
typedef bool (*ImageRunsFn)(Rect run, Color color, void *context);
bool ImageRuns_visit(const Image *image, Rect destination, Rect clip,
                     ImageRunsFn visit, void *context);

#endif
