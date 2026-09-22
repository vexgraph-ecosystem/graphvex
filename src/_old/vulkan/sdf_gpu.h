#ifndef ANTI_SDF_GPU_H
#define ANTI_SDF_GPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// vulkan/sdf_gpu.h — GPU jump-flood SDF baker for font atlases.
//
// During install Vulkan/Metal is already awake, so whole coverage pages bake
// in ~24 compute dispatches instead of thousands of per-glyph CPU SDF
// rasters (ms each). Output bytes match stbtt (onedge 128, scale 16), so
// GPU and CPU pages are interchangeable inside one .antifont.
//
// Any failure returns false and the caller falls back to the threaded CPU
// path — including headless processes, where init never runs.

bool SdfGpu_initModule(void *instance, void *gpa, void *phys, void *device,
                       void *queue, uint32_t queueFamily);
void SdfGpu_shutdown(void);

// True once init built pipelines successfully.
bool SdfGpu_available(void);

// Bakes one dim x dim coverage page (0 = outside, >127 = inside) into SDF
// bytes. dim must equal SdfGpu_pageDim(). False on any failure.
bool SdfGpu_bakePage(const uint8_t *coverage, int dim, uint8_t *outSdf);

// Fixed atlas page dimension this module is built for.
int SdfGpu_pageDim(void);

#endif // ANTI_SDF_GPU_H
