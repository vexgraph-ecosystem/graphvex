#include "image/bindless_heap.h"

#include <stdlib.h>
#include <string.h>

#include "image/image.h"
#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: BindlessHeap (image/bindless_heap.c)
 * LEVEL: L2 — Behavior (stable handle registry behavior API, CPU stub)
 * ============================================================================
 * Stable uint32 handle registry for Images: handles are stable indices
 * into an owned slot array (null = free/bad). Slot 0 is reserved for the
 * dummy white 1x1 image so bad handles resolve to 0/NULL, never to a
 * real entry. The struct rides the vexspoke arena via
 * Memory_alloc(TYPE_BINDLESS_HEAP_SINGLETON) with a calloc fallback for
 * standalone builds; the slot array is always calloc-owned system memory.
 * Registered Images are borrowed (only slot 0 dummy is owned and freed
 * by BindlessHeap_free). No Vulkan/Metal/DirectX includes: pure CPU stub.
 *
 * STRUCT FIELDS (Mirroring image/bindless_heap.h):
 * ----------------------------------------------------------------------------
 *   BindlessHeap {
 *     uint32_t count; // live entries including slot 0 dummy
 *     uint32_t capacity; // allocated slots length
 *     uint64_t typeId; // block-header type id (TYPE_BINDLESS_HEAP_SINGLETON)
 *     Image **slots; // OWNED slot array, slots[0] = dummy white 1x1, null = free/bad; Images borrowed except slot 0
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - BindlessHeap()                 : BindlessHeap_0()
 *
 * Core Functions:
 *   - BindlessHeap_register(heap, img)
 *   - BindlessHeap_lookup(heap, handle)
 *   - BindlessHeap_free(heap)
 *
 * Setters:
 *   - (none)
 *
 * Getters:
 *   - BindlessHeap_getCount(heap)
 *   - BindlessHeap_getCapacity(heap)
 * ============================================================================
 */

// image/bindless_heap.c — Stable uint32 handle registry implementation (CPU stub).

static void bindlessHeapFreeStorage(BindlessHeap *heap) {
    if (Memory_length(heap) != 0)
        Memory_free(heap);
    else
        free(heap);
}

// CONSTRUCTORS
BindlessHeap *BindlessHeap_0() {
    BindlessHeap *heap = Memory_alloc(TYPE_BINDLESS_HEAP_SINGLETON, sizeof(BindlessHeap));
    if (!heap)
        heap = (BindlessHeap*) calloc(1, sizeof(BindlessHeap));
    if (!heap)
        return nullptr;
    uint32_t cap = 8;
    Image **slots = calloc(cap, sizeof(Image*));
    if (!slots) {
        bindlessHeapFreeStorage(heap);
        return nullptr;
    }
    Image *dummy = Image_0();
    if (!dummy) {
        free(slots);
        bindlessHeapFreeStorage(heap);
        return nullptr;
    }
    uint8_t *px = (*dummy).rgba;
    if (px) {
        px[0] = 255;
        px[1] = 255;
        px[2] = 255;
        px[3] = 255;
    }
    (*heap).count = 1;
    (*heap).capacity = cap;
    (*heap).typeId = TYPE_BINDLESS_HEAP_SINGLETON;
    (*heap).slots = slots;
    (*heap).slots[0] = dummy;
    return heap;
}

// CORE FUNCTIONS
uint32_t BindlessHeap_register(BindlessHeap *heap, Image *img) {
    if (!heap)
        return 0;
    if (!img)
        return 0;
    if (!(*heap).slots)
        return 0;
    for (uint32_t i = 1; i < (*heap).capacity; i++) {
        if ((*heap).slots[i] == nullptr) {
            (*heap).slots[i] = img;
            (*heap).count++;
            return i;
        }
    }
    uint32_t oldCap = (*heap).capacity;
    uint32_t newCap = oldCap * 2;
    if (newCap < 8)
        newCap = 8;
    if (newCap <= oldCap)
        return 0;
    Image **grown = (Image**) realloc((*heap).slots, (size_t)newCap * sizeof(Image *));
    if (!grown)
        return 0;
    memset(&grown[oldCap], 0, (size_t)(newCap - oldCap) * sizeof(Image *));
    grown[oldCap] = img;
    (*heap).slots = grown;
    (*heap).capacity = newCap;
    (*heap).count++;
    return oldCap;
}

Image *BindlessHeap_lookup(const BindlessHeap *heap, uint32_t handle) {
    if (!heap)
        return nullptr;
    if (!(*heap).slots)
        return nullptr;
    if (handle >= (*heap).capacity)
        return nullptr;
    return (*heap).slots[handle];
}

void BindlessHeap_free(BindlessHeap *heap) {
    if (!heap)
        return;
    Image **slots = (*heap).slots;
    if (slots) {
        Image *dummy = slots[0];
        if (dummy)
            Image_free(dummy);
        free(slots);
        (*heap).slots = nullptr;
    }
    (*heap).count = 0;
    (*heap).capacity = 0;
    bindlessHeapFreeStorage(heap);
}

// GETTERS
uint32_t BindlessHeap_getCount(const BindlessHeap *heap) {
    return heap ? (*heap).count : 0;
}

uint32_t BindlessHeap_getCapacity(const BindlessHeap *heap) {
    return heap ? (*heap).capacity : 0;
}
