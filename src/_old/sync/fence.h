#ifndef SYNC_FENCE_H
#define SYNC_FENCE_H

#include <stdbool.h>
#include <stdint.h>

#include "c23/constructor.h"

// sync/fence.h — CPU-GPU join handle (stub).
//
// A Fence joins CPU execution with GPU completion: the CPU waits (bounded,
// 100ms per the Bounded Wait Law) until the GPU signals. All lifecycle functions are
// CPU-side stubs: no Vulkan/Metal/Direct backend is touched here.

#ifndef FENCE_WAIT_TIMEOUT_NS
#define FENCE_WAIT_TIMEOUT_NS 100000000ULL
#endif

typedef struct Fence {
    bool signaled;
    uint64_t typeId;
} Fence;

Fence *Fence_0(void);

void Fence_free(Fence *self);
bool Fence_reset(Fence *self);
bool Fence_wait(Fence *self, uint64_t timeoutNs);

void Fence_setSignaled(Fence *self, bool signaled);
bool Fence_isSignaled(const Fence *self);
uint64_t Fence_getTypeId(const Fence *self);

#define Fence(...) CONSTRUCTOR_DISPATCH(Fence, __VA_ARGS__)
#endif
