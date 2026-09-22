#ifndef SYNC_SEMAPHORE_H
#define SYNC_SEMAPHORE_H

#include <stdbool.h>
#include <stdint.h>

#include "c23/constructor.h"

// sync/semaphore.h — GPU-GPU ordering handle (stub).
//
// A Semaphore orders GPU work against GPU work (timeline counter): one
// submission signals a value, a later submission waits for it. The CPU never
// blocks here; Semaphore_wait only probes the counter. All lifecycle
// functions are CPU-side stubs: no Vulkan/Metal/Direct backend is touched
// here.

typedef struct Semaphore {
    uint64_t value;
    uint64_t typeId;
} Semaphore;

Semaphore *Semaphore_0(void);

void Semaphore_free(Semaphore *self);
void Semaphore_signal(Semaphore *self, uint64_t value);
bool Semaphore_wait(Semaphore *self, uint64_t value);

void Semaphore_setValue(Semaphore *self, uint64_t value);
uint64_t Semaphore_getValue(const Semaphore *self);
uint64_t Semaphore_getTypeId(const Semaphore *self);

#define Semaphore(...) CONSTRUCTOR_DISPATCH(Semaphore, __VA_ARGS__)
#endif
