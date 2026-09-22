#ifndef IMAGE_BINDLESS_HEAP_H
#define IMAGE_BINDLESS_HEAP_H

#include <stdint.h>
#include "c23/constructor.h"
#include "../graphics/type.h"

// image/bindless_heap.h — Stable uint32 handle registry for Images, CPU stub.
//
// Slot 0 is reserved for the dummy white 1x1 image; handles are stable
// indices into an owned slot array (null = free/bad). No Vulkan/Metal/
// DirectX includes here or in the .c file.

typedef struct Image Image;

typedef struct BindlessHeap {
    uint32_t count; // live entries including slot 0 dummy
    uint32_t capacity; // allocated slots length
    uint64_t typeId; // block-header type id (TYPE_BINDLESS_HEAP_SINGLETON)
    Image **slots; // OWNED slot array, slots[0] = dummy white 1x1, null = free/bad; Images borrowed except slot 0
} BindlessHeap;

// Empty heap with slot 0 dummy white 1x1; null on OOM
BindlessHeap *BindlessHeap_0(void);

// Register img, return stable handle (>= 1); 0 on null heap/img or OOM (slot 0 never returned)
uint32_t BindlessHeap_register(BindlessHeap *heap, Image *img);

// Resolve handle to Image* (slot 0 = dummy); null on null heap or bad handle
Image *BindlessHeap_lookup(const BindlessHeap *heap, uint32_t handle);

// Release slot 0 dummy + slot array + struct (Images except slot 0 borrowed); null-safe no-op
void BindlessHeap_free(BindlessHeap *heap);

// Null-safe inspectors (null yields 0)
uint32_t BindlessHeap_getCount(const BindlessHeap *heap);
uint32_t BindlessHeap_getCapacity(const BindlessHeap *heap);

#define BindlessHeap(...) CONSTRUCTOR_DISPATCH(BindlessHeap, __VA_ARGS__)
#endif
