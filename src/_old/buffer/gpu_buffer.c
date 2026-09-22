#include "buffer/gpu_buffer.h"

#include <string.h>

#include "nio/mem.h"
#include "../graphics/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: GpuBuffer
 * ============================================================================
 * Device-visible storage, uniform, vertex, and index buffer abstraction with
 * an integrated host-side CPU shadow buffer.
 *
 * Maintains a coherent byte mirror of the underlying GPU memory range, permitting
 * headless testing, staging, and upload or download validation without requiring
 * an active graphics device context in compliance with the Unified Graphics Abstraction Law.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: GpuBuffer (buffer/gpu_buffer.c)
 * LEVEL: L2 — Behavior (device buffer behavior API)
 * ============================================================================
 * Device-visible storage/uniform/vertex/index buffer with a CPU-shadow stub.
 * The struct owns a byte mirror of the device range so upload/download paths
 * stay testable with no GPU backend linked; a real driver wires the shadow
 * to mapped device memory later. Usage flags select the device role
 * (storage / uniform / vertex / index) and are opaque to this stub.
 *
 * STRUCT FIELDS (Mirroring buffer/gpu_buffer.h):
 * ----------------------------------------------------------------------------
 *   GpuBuffer {
 *     size_t size;      // device range length in bytes
 *     uint32_t usage;   // opaque device-role flags (storage/uniform/vertex/index)
 *     uint64_t typeId;  // block-header type id (TYPE_GPU_BUFFER_SINGLETON)
 *     uint8_t *shadow;  // owned CPU mirror, nullptr when empty
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - GpuBuffer_0(void)                  : Allocate empty buffer instance
 *   - GpuBuffer_2(size, usage)           : Allocate sized buffer with role usage
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - GpuBuffer_free(self)               : Release buffer and shadow memory
 *   - GpuBuffer_upload(data, n, dest)    : Upload data into shadow mirror
 *   - GpuBuffer_download(self, data, n)  : Download data from shadow mirror
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - GpuBuffer_setSize(self, size)      : Mutate buffer capacity and shadow size
 *   - GpuBuffer_setUsage(self, usage)    : Mutate buffer usage flags
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - GpuBuffer_getSize(self)            : Query buffer byte size
 *   - GpuBuffer_getUsage(self)           : Query buffer usage flags
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

GpuBuffer *GpuBuffer_0(void) {
    GpuBuffer *self = (GpuBuffer*) Memory_alloc(TYPE_GPU_BUFFER_SINGLETON, sizeof(GpuBuffer));
    if (!self)
        return nullptr;
    (*self).size = 0;
    (*self).usage = 0;
    (*self).typeId = TYPE_GPU_BUFFER_SINGLETON;
    (*self).shadow = nullptr;
    return self;
}

GpuBuffer *GpuBuffer_2(size_t size, uint32_t usage) {
    GpuBuffer *self = (GpuBuffer*) Memory_alloc(TYPE_GPU_BUFFER_SINGLETON, sizeof(GpuBuffer));
    if (!self)
        return nullptr;
    (*self).size = 0;
    (*self).usage = usage;
    (*self).typeId = TYPE_GPU_BUFFER_SINGLETON;
    (*self).shadow = nullptr;
    if (size > 0) {
        uint8_t *shadow = (uint8_t*) Memory_alloc(TYPE_GPU_BUFFER_SINGLETON, size);
        if (!shadow) {
            Memory_free(self);
            return nullptr;
        }
        memset(shadow, 0, size);
        (*self).shadow = shadow;
        (*self).size = size;
    }
    return self;
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

void GpuBuffer_free(GpuBuffer *self) {
    if (!self)
        return;
    uint8_t *shadow = (*self).shadow;
    if (shadow)
        Memory_free(shadow);
    Memory_free(self);
}

bool GpuBuffer_upload(const uint8_t *data, size_t n, GpuBuffer *dest) {
    if (!dest)
        return false;
    if (n == 0)
        return true;
    if (!data)
        return false;
    uint8_t *shadow = (*dest).shadow;
    if (!shadow)
        return false;
    if (n > (*dest).size)
        return false;
    memcpy(shadow, data, n);
    return true;
}

bool GpuBuffer_download(const GpuBuffer *self, uint8_t *data, size_t n) {
    if (!self)
        return false;
    if (n == 0)
        return true;
    if (!data)
        return false;
    uint8_t *shadow = (*self).shadow;
    if (!shadow)
        return false;
    if (n > (*self).size)
        return false;
    memcpy(data, shadow, n);
    return true;
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void GpuBuffer_setSize(GpuBuffer *self, size_t size) {
    if (!self)
        return;
    if (size == (*self).size)
        return;
    if (size == 0) {
        uint8_t *old = (*self).shadow;
        if (old)
            Memory_free(old);
        (*self).shadow = nullptr;
        (*self).size = 0;
        return;
    }
    uint8_t *old = (*self).shadow;
    if (!old) {
        uint8_t *fresh = (uint8_t*) Memory_alloc(TYPE_GPU_BUFFER_SINGLETON, size);
        if (!fresh)
            return;
        memset(fresh, 0, size);
        (*self).shadow = fresh;
        (*self).size = size;
        return;
    }
    uint8_t *grown = (uint8_t*) Memory_realloc(old, size);
    if (!grown)
        return;
    size_t kept = (*self).size;
    if (size > kept)
        memset(grown + kept, 0, size - kept);
    (*self).shadow = grown;
    (*self).size = size;
}

;;SETTER
void GpuBuffer_setUsage(GpuBuffer *self, uint32_t usage) {
    if (!self)
        return;
    (*self).usage = usage;
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
size_t GpuBuffer_getSize(const GpuBuffer *self) {
    return self ? (*self).size : 0;
}

;;GETTER
uint32_t GpuBuffer_getUsage(const GpuBuffer *self) {
    return self ? (*self).usage : 0;
}
