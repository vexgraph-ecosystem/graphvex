#include "sync/fence.h"

#include <stdlib.h>

#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Fence (sync/fence.c)
 * LEVEL: L2 — Behavior (CPU-GPU join lifecycle stubs)
 * ============================================================================
 * CPU-GPU join handle. The CPU waits (bounded to FENCE_WAIT_TIMEOUT_NS per
 * the Bounded Wait Law — never an unbounded block on a joined path) until GPU work
 * signals completion. CPU-side stubs only: wait probes the signaled flag
 * and touches no Vulkan/Metal/Direct backend.
 *
 * STRUCT FIELDS (Mirroring sync/fence.h):
 * ----------------------------------------------------------------------------
 *   Fence {
 *     bool signaled; // true once GPU work completed (set by signal path)
 *     uint64_t typeId; // reserved stub type stamp (0 until registered)
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Fence()                        : Fence_0()
 *
 * Core Functions:
 *   - Fence_free(self)
 *   - Fence_reset(self)
 *   - Fence_wait(self, timeoutNs)
 *
 * Setters:
 *   - Fence_setSignaled(self, signaled)
 *
 * Getters:
 *   - Fence_isSignaled(self)
 *   - Fence_getTypeId(self)
 * ============================================================================
 */


// CONSTRUCTORS

Fence *Fence_0(void) {
    Fence *self = (Fence*) calloc(1, sizeof(Fence));
    if (!self)
        return nullptr;
    (*self).signaled = false;
    (*self).typeId = 0;
    return self;
}

// CORE FUNCTIONS

void Fence_free(Fence *self) {
    if (!self)
        return;
    free(self);
}

bool Fence_reset(Fence *self) {
    if (!self)
        return false;
    (*self).signaled = false;
    return true;
}

bool Fence_wait(Fence *self, uint64_t timeoutNs) {
    if (!self)
        return false;
    if (timeoutNs > FENCE_WAIT_TIMEOUT_NS)
        timeoutNs = FENCE_WAIT_TIMEOUT_NS;
    (void) timeoutNs;
    return (*self).signaled;
}

// SETTERS

void Fence_setSignaled(Fence *self, bool signaled) {
    if (!self)
        return;
    (*self).signaled = signaled;
}

// GETTERS

bool Fence_isSignaled(const Fence *self) {
    if (!self)
        return false;
    return (*self).signaled;
}

uint64_t Fence_getTypeId(const Fence *self) {
    if (!self)
        return 0;
    return (*self).typeId;
}
