#ifndef BUFFER_GPU_BUFFER_H
#define BUFFER_GPU_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"

// buffer/gpu_buffer.h — device-visible storage/uniform/vertex/index buffer.
//
// A GpuBuffer describes one GPU-side byte range (storage, uniform, vertex,
// or index — selected by the opaque usage flags) plus an owned CPU-shadow
// stub that mirrors its bytes until a real backend maps device memory.
// No Vulkan/Metal/DirectX headers here: this file is pure C23.

typedef struct GpuBuffer {
    size_t size;
    uint32_t usage;
    uint64_t typeId;
    uint8_t *shadow;
} GpuBuffer;

// Empty buffer: zero size/usage, null shadow.
GpuBuffer *GpuBuffer_0(void);

// Sized buffer: zeroed shadow of size bytes, null shadow when size is 0.
GpuBuffer *GpuBuffer_2(size_t size, uint32_t usage);

// Release the shadow and the struct itself; null-safe.
void GpuBuffer_free(GpuBuffer *self);

// Copy n bytes from data into dest's shadow; dest last. False on null,
// missing shadow, or n larger than dest size.
bool GpuBuffer_upload(const uint8_t *data, size_t n, GpuBuffer *dest);

// Copy n bytes from self's shadow out into data. False on null, missing
// shadow, or n larger than self size.
bool GpuBuffer_download(const GpuBuffer *self, uint8_t *data, size_t n);

// Size mutator (grows/shrinks the shadow, preserving overlap, zero-filling
// growth); usage mutator.
void GpuBuffer_setSize(GpuBuffer *self, size_t size);
void GpuBuffer_setUsage(GpuBuffer *self, uint32_t usage);

// Null-safe inspectors: 0 when self is null.
size_t GpuBuffer_getSize(const GpuBuffer *self);
uint32_t GpuBuffer_getUsage(const GpuBuffer *self);

#define GpuBuffer(...) CONSTRUCTOR_DISPATCH(GpuBuffer, __VA_ARGS__)

#endif
