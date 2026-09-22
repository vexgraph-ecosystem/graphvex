#include "image/bindless_heap.h"

#include <stdlib.h>
#include <string.h>

#include "image/image.h"
#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: BindlessHeap
 * ============================================================================
 * Centralized stable descriptor index registry for dynamically binding graphics
 * Image handles without pipeline state switches or individual descriptor sets.
 * Maps 32-bit stable non-zero integer handles to Image pointers across GPU draw
 * and paint dispatches.
 *
 * Slot 0 is strictly reserved as an internal 1x1 opaque white dummy fallback
 * Image owned directly by the heap, ensuring invalid or unmapped handles resolve
 * safely to valid visual data rather than crashing hardware shaders. Struct
 * allocations are governed by the vexspoke typed memory arena
 * (TYPE_BINDLESS_HEAP_SINGLETON) with fallback to calloc for standalone targets.
 * Registered image pointers are borrowed references whose life cycle remains
 * managed by their original creators.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: BindlessHeap (image/bindless_heap.c)
 * LEVEL: L2 — Behavior (stable handle registry behavior API, CPU stub)
 * ============================================================================
 * SUMMARY:
 *   Stable uint32 handle registry for Images where handles are indices into
 *   an owned dynamic slot array. Slot 0 is reserved for an owned dummy white
 *   1x1 image so invalid or zero handles never trigger null pointer dereferences.
 *   Registered Images are borrowed references.
 *
 * STRUCT FIELDS (Mirroring image/bindless_heap.h):
 * ----------------------------------------------------------------------------
 *   uint32_t count;    // live entries including slot 0 dummy
 *   uint32_t capacity; // allocated slots length
 *   uint64_t typeId;   // block-header type id (TYPE_BINDLESS_HEAP_SINGLETON)
 *   Image **slots;     // OWNED slot array, slots[0] = dummy white 1x1, null = free/bad; Images borrowed except slot 0
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - BindlessHeap_0(void)                               : Empty heap with slot 0 dummy white 1x1
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - BindlessHeap_register(heap, img)                   : Register image and return stable handle index
 *   - BindlessHeap_lookup(heap, handle)                  : Resolve stable handle to borrowed Image pointer
 *   - BindlessHeap_free(heap)                            : Release slot array, dummy image, and heap memory
 *
 * Private Core Functions: (.c static)
 *   - bindlessHeapFreeStorage(heap)                      : Release arena or heap struct block
 *
 * Public Setters: (.h)
 *   - (none)
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - BindlessHeap_getCount(heap)                        : Query live slot count including dummy
 *   - BindlessHeap_getCapacity(heap)                     : Query allocated slot array capacity
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// CONSTRUCTORS (PUBLIC & PRIVATE)

static void bindlessHeapFreeStorage(BindlessHeap *heap);

BindlessHeap *BindlessHeap_0(void) {
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

// CORE FUNCTIONS (PUBLIC & PRIVATE)

static void bindlessHeapFreeStorage(BindlessHeap *heap) {
    if (Memory_length(heap) != 0)
        Memory_free(heap);
    else
        free(heap);
}

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

// SETTERS (PUBLIC & PRIVATE)

// (none)

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
uint32_t BindlessHeap_getCount(const BindlessHeap *heap) {
    return heap ? (*heap).count : 0;
}

;;GETTER
uint32_t BindlessHeap_getCapacity(const BindlessHeap *heap) {
    return heap ? (*heap).capacity : 0;
}
