#include "sync/semaphore.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Semaphore
 * ============================================================================
 * Timeline synchronization handle coordinating execution between asynchronous GPU
 * queue submissions. Tracks monotonic timeline counter values, providing non-blocking
 * pipeline barriers and dependency progression.
 *
 * Adheres to the Unified Graphics Abstraction Law and the Bounded Wait Law: CPU threads
 * never block indefinitely on timeline semaphores, using wait queries to probe
 * monotonic progression without stalling host frame loops.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Semaphore (sync/semaphore.c)
 * LEVEL: L2 — Behavior (GPU-GPU ordering lifecycle stubs)
 * ============================================================================
 * SUMMARY:
 *   GPU-GPU ordering handle. One submission signals a timeline value and a
 *   later submission waits for it; the CPU never blocks here — wait only
 *   probes the counter (the Bounded Wait Law: no unbounded waits). CPU-side stubs only:
 *   signal/wait track the value flag and touch no Vulkan/Metal/Direct
 *   backend.
 *
 * STRUCT FIELDS (Mirroring sync/semaphore.h):
 * ----------------------------------------------------------------------------
 *   uint64_t value;  // timeline counter (monotonic: signal only raises)
 *   uint64_t typeId; // reserved stub type stamp (0 until registered)
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   (none)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Semaphore_0(void)                       : Zero-initialized semaphore handle
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Semaphore_free(self)                    : Release semaphore handle memory
 *   - Semaphore_signal(self, value)           : Advance timeline counter value
 *   - Semaphore_wait(self, value)             : Probe timeline counter progression
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - Semaphore_setValue(self, value)         : Directly set timeline counter
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Semaphore_getValue(self)                : Query current timeline value
 *   - Semaphore_getTypeId(self)               : Query type identity stamp
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// CONSTRUCTORS (PUBLIC & PRIVATE)

Semaphore *Semaphore_0(void) {
    Semaphore *self = (Semaphore*) calloc(1, sizeof(Semaphore));
    if (!self)
        return nullptr;
    (*self).value = 0;
    (*self).typeId = 0;
    return self;
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

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

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void Semaphore_setValue(Semaphore *self, uint64_t value) {
    if (!self)
        return;
    (*self).value = value;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
uint64_t Semaphore_getValue(const Semaphore *self) {
    if (!self)
        return 0;
    return (*self).value;
}

;;GETTER
uint64_t Semaphore_getTypeId(const Semaphore *self) {
    if (!self)
        return 0;
    return (*self).typeId;
}
