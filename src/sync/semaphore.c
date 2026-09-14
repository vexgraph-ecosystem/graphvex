#include "sync/semaphore.h"

#include <stdlib.h>

#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Semaphore (sync/semaphore.c)
 * LEVEL: L2 — Behavior (GPU-GPU ordering lifecycle stubs)
 * ============================================================================
 * GPU-GPU ordering handle. One submission signals a timeline value and a
 * later submission waits for it; the CPU never blocks here — wait only
 * probes the counter (the Bounded Wait Law: no unbounded waits). CPU-side stubs only:
 * signal/wait track the value flag and touch no Vulkan/Metal/Direct
 * backend.
 *
 * STRUCT FIELDS (Mirroring sync/semaphore.h):
 * ----------------------------------------------------------------------------
 *   Semaphore {
 *     uint64_t value; // timeline counter (monotonic: signal only raises)
 *     uint64_t typeId; // reserved stub type stamp (0 until registered)
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Semaphore()                    : Semaphore_0()
 *
 * Core Functions:
 *   - Semaphore_free(self)
 *   - Semaphore_signal(self, value)
 *   - Semaphore_wait(self, value)
 *
 * Setters:
 *   - Semaphore_setValue(self, value)
 *
 * Getters:
 *   - Semaphore_getValue(self)
 *   - Semaphore_getTypeId(self)
 * ============================================================================
 */


// CONSTRUCTORS

Semaphore *Semaphore_0(void) {
    Semaphore *self = (Semaphore*) calloc(1, sizeof(Semaphore));
    if (!self)
        return nullptr;
    (*self).value = 0;
    (*self).typeId = 0;
    return self;
}

// CORE FUNCTIONS

void Semaphore_free(Semaphore *self) {
    if (!self)
        return;
    free(self);
}

void Semaphore_signal(Semaphore *self, uint64_t value) {
    if (!self)
        return;
    if (value > (*self).value)
        (*self).value = value;
}

bool Semaphore_wait(Semaphore *self, uint64_t value) {
    if (!self)
        return false;
    return (*self).value >= value;
}

// SETTERS

void Semaphore_setValue(Semaphore *self, uint64_t value) {
    if (!self)
        return;
    (*self).value = value;
}

// GETTERS

uint64_t Semaphore_getValue(const Semaphore *self) {
    if (!self)
        return 0;
    return (*self).value;
}

uint64_t Semaphore_getTypeId(const Semaphore *self) {
    if (!self)
        return 0;
    return (*self).typeId;
}
