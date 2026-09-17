#include "graphvex/gfx_loop.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "annotation/overview.h"
#include "graphvex/type.h"
#include "time/nanotime.h"
#include "vulkan/vk.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: GfxLoop (graphvex/src/graphvex/gfx_loop.c)
 * LEVEL: L2 — Behavior (demand-driven frame scheduler, R3 Driver)
 * ============================================================================
 * Owns:
 *   1. Demand-driven frame loop (Present-On-Demand Law)
 *   2. Thread-0 event pump delegation (Window_pollEvents)
 *   3. 2-VkImage System (Scene Board + Content Board)
 *   4. Zero-gap live resize modal tick (Continuous Real-Time Live Resize Law)
 *   5. CoreAnimation atomic transaction brackets (GraphicsLayer_transactionBegin/Commit)
 *
 * STRUCT FIELDS (Mirroring graphvex/gfx_loop.h):
 * ----------------------------------------------------------------------------
 *   uint64_t typeId;             // TYPE_GFX_LOOP_SINGLETON
 *   GfxClient *clients;          // dynamic doubling client registry
 *   uint32_t clientCount;        // registered windows
 *   uint32_t clientCap;          // allocated capacity
 *   _Atomic bool running;        // active loop flag
 *   GfxPollFn pollFn;            // Window_pollEvents hook
 *   uint32_t targetFps;          // 60/120Hz cadence
 *   uint64_t lastTimeNs;         // previous frame timestamp
 *   uint32_t frameCounter;       // for FPS calculation
 *   uint64_t fpsTimerNs;         // 1-second telemetry accumulator
 *   uint32_t currentFps;         // live FPS
 *   uint32_t currentFrametimeUs; // live frametime in microseconds
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - GfxLoop_0()
 *   - GfxLoop_1(targetFps)
 * Core Functions:
 *   - GfxLoop_free(self)
 *   - GfxLoop_isValid(self)
 *   - GfxLoop_default()
 *   - GfxLoop_registerClient(self, window, app, sceneLayer, contentLayer, frameFn, userdata)
 *   - GfxLoop_unregisterClient(self, window)
 *   - GfxLoop_findClient(self, window)
 *   - GfxLoop_markDirty(self, window)
 *   - GfxLoop_installPoll(self, pollFn)
 *   - GfxLoop_step(self)
 *   - GfxLoop_run(self, app)
 *   - GfxLoop_runApplication(app)
 *   - GfxLoop_modalTick()
 * Getters:
 *   - GfxLoop_getClientCount(self)
 * ============================================================================
 */

static GfxLoop *s_defaultLoop = nullptr;

GfxLoop *GfxLoop_0(void) {
    return GfxLoop_1(60);
}

GfxLoop *GfxLoop_1(uint32_t targetFps) {
    GfxLoop *self = (GfxLoop*) calloc(1, sizeof(GfxLoop));
    if (!self)
        return nullptr;
    (*self).typeId = TYPE_GFX_LOOP_SINGLETON;
    (*self).clients = nullptr;
    (*self).clientCount = 0;
    (*self).clientCap = 0;
    atomic_store_explicit(&(*self).running, false, memory_order_relaxed);
    (*self).pollFn = nullptr;
    (*self).targetFps = (targetFps > 0) ? targetFps : 60;
    (*self).lastTimeNs = NanoTime_now();
    (*self).frameCounter = 0;
    (*self).fpsTimerNs = (*self).lastTimeNs;
    (*self).currentFps = 0;
    (*self).currentFrametimeUs = 0;

    if (!s_defaultLoop)
        s_defaultLoop = self;

    return self;
}

void GfxLoop_free(GfxLoop *self) {
    if (!GfxLoop_isValid(self))
        return;
    atomic_store_explicit(&(*self).running, false, memory_order_relaxed);
    if ((*self).clients) {
        free((*self).clients);
        (*self).clients = nullptr;
    }
    (*self).clientCount = 0;
    (*self).clientCap = 0;
    (*self).typeId = 0;
    if (s_defaultLoop == self)
        s_defaultLoop = nullptr;
    free(self);
}

bool GfxLoop_isValid(const GfxLoop *self) {
    return self && (*self).typeId == TYPE_GFX_LOOP_SINGLETON;
}

GfxLoop *GfxLoop_default(void) {
    if (!s_defaultLoop)
        s_defaultLoop = GfxLoop_0();
    return s_defaultLoop;
}

bool GfxLoop_registerClient(GfxLoop *self, void *window, void *app,
                            GraphicsLayer *sceneLayer, GraphicsLayer *contentLayer,
                            GfxFrameFn frameFn, void *userdata) {
    if (!GfxLoop_isValid(self) || !window)
        return false;
    for (uint32_t i = 0; i < (*self).clientCount; i++) {
        if ((*self).clients[i].window == window)
            return false;
    }
    if ((*self).clientCount >= (*self).clientCap) {
        uint32_t newCap = (*self).clientCap ? (*self).clientCap * 2 : 4;
        GfxClient *newSlots = (GfxClient*) realloc((*self).clients, newCap * sizeof(GfxClient));
        if (!newSlots)
            return false;
        (*self).clients = newSlots;
        (*self).clientCap = newCap;
    }
    GfxClient *client = &(*self).clients[(*self).clientCount++];
    (*client).window = window;
    (*client).app = app;
    (*client).sceneLayer = sceneLayer;
    (*client).contentLayer = contentLayer;
    (*client).frameFn = frameFn;
    (*client).userdata = userdata;
    atomic_store_explicit(&(*client).dirty, true, memory_order_relaxed);
    return true;
}

bool GfxLoop_unregisterClient(GfxLoop *self, void *window) {
    if (!GfxLoop_isValid(self) || !window)
        return false;
    for (uint32_t i = 0; i < (*self).clientCount; i++) {
        if ((*self).clients[i].window == window) {
            (*self).clients[i] = (*self).clients[--(*self).clientCount];
            return true;
        }
    }
    return false;
}

GfxClient *GfxLoop_findClient(GfxLoop *self, const void *window) {
    if (!GfxLoop_isValid(self) || !window)
        return nullptr;
    for (uint32_t i = 0; i < (*self).clientCount; i++) {
        if ((*self).clients[i].window == window)
            return &(*self).clients[i];
    }
    return nullptr;
}

uint32_t GfxLoop_getClientCount(const GfxLoop *self) {
    return GfxLoop_isValid(self) ? (*self).clientCount : 0;
}

void GfxLoop_markDirty(GfxLoop *self, void *window) {
    GfxClient *client = GfxLoop_findClient(self, window);
    if (client)
        atomic_store_explicit(&(*client).dirty, true, memory_order_relaxed);
}

void GfxLoop_installPoll(GfxLoop *self, GfxPollFn pollFn) {
    if (GfxLoop_isValid(self))
        (*self).pollFn = pollFn;
}

bool GfxLoop_step(GfxLoop *self) {
    if (!GfxLoop_isValid(self))
        return false;

    // 1. Thread 0 event pump
    if ((*self).pollFn)
        (*self).pollFn();

    // 2. Compute frame delta time
    uint64_t nowNs = NanoTime_now();
    if ((*self).lastTimeNs == 0)
        (*self).lastTimeNs = nowNs;
    uint64_t deltaNs = nowNs - (*self).lastTimeNs;
    (*self).lastTimeNs = nowNs;
    double dt = (double) deltaNs / 1000000000.0;

    // Telemetry tracking
    (*self).currentFrametimeUs = (uint32_t) (deltaNs / 1000ULL);
    (*self).frameCounter++;
    if (nowNs - (*self).fpsTimerNs >= 1000000000ULL) {
        (*self).currentFps = (*self).frameCounter;
        (*self).frameCounter = 0;
        (*self).fpsTimerNs = nowNs;

        // Write telemetry to registered Application manifests
        for (uint32_t i = 0; i < (*self).clientCount; i++) {
            void *appPtr = (*self).clients[i].app;
            if (appPtr) {
                // Application struct offset for fps and frametimeUs:
                // We write via atomic pointer casts without needing hotcwap/application.h
                // Field layout: fps is _Atomic uint32_t, frametimeUs is _Atomic uint32_t
                // In application.h:
                //   offset 53: _Atomic uint32_t fps
                //   offset 54: _Atomic uint32_t frametimeUs
                // We provide an extern setter or cast safely
                typedef struct AppTelemetryMirror {
                    char dummy[64 + 64 + 16 + 512 + sizeof(void*) * 16 + sizeof(uint32_t) + sizeof(_Atomic bool) + sizeof(void*)];
                    _Atomic uint32_t fps;
                    _Atomic uint32_t frametimeUs;
                } AppTelemetryMirror;
                AppTelemetryMirror *m = (AppTelemetryMirror*) appPtr;
                atomic_store_explicit(&(*m).fps, (*self).currentFps, memory_order_relaxed);
                atomic_store_explicit(&(*m).frametimeUs, (*self).currentFrametimeUs, memory_order_relaxed);
            }
        }
    }

    // 3. Demand-driven presentation pass (The Present-On-Demand Law)
    bool anyDirty = false;
    for (uint32_t i = 0; i < (*self).clientCount; i++) {
        GfxClient *client = &(*self).clients[i];
        if (atomic_load_explicit(&(*client).dirty, memory_order_relaxed)) {
            anyDirty = true;
            atomic_store_explicit(&(*client).dirty, false, memory_order_relaxed);
            if ((*client).frameFn)
                (*client).frameFn((*client).window, dt, (*client).userdata);
        }
    }

    if (anyDirty) {
        // Atomic CoreAnimation transaction bracket: presentsWithTransaction = YES
        GraphicsLayer_transactionBegin();
        if (Vk_ready())
            Vk_clearPresent();
        GraphicsLayer_transactionCommit();
    }

    return true;
}

int GfxLoop_run(GfxLoop *self, void *app) {
    if (!GfxLoop_isValid(self))
        return -1;

    atomic_store_explicit(&(*self).running, true, memory_order_relaxed);

    // Frame throttle (e.g. 60Hz = ~16.6ms)
    uint64_t targetSliceNs = 1000000000ULL / (*self).targetFps;
    const struct timespec restSlice = {0, 1000000L}; // 1ms sleep

    while (atomic_load_explicit(&(*self).running, memory_order_relaxed)) {
        uint64_t startNs = NanoTime_now();

        GfxLoop_step(self);

        // Check if all windows belonging to app closed
        if (app) {
            typedef struct AppFinishedMirror {
                char dummy[64 + 64 + 16 + 512 + sizeof(void*) * 16 + sizeof(uint32_t)];
                _Atomic bool running;
            } AppFinishedMirror;
            AppFinishedMirror *am = (AppFinishedMirror*) app;
            if (!atomic_load_explicit(&(*am).running, memory_order_relaxed))
                break;
        }

        uint64_t spentNs = NanoTime_now() - startNs;
        if (spentNs < targetSliceNs) {
            struct timespec rem = {0, (long)(targetSliceNs - spentNs)};
            nanosleep(&rem, NULL);
        } else {
            nanosleep(&restSlice, NULL);
        }
    }

    atomic_store_explicit(&(*self).running, false, memory_order_relaxed);
    return 0;
}

int GfxLoop_runApplication(void *app) {
    GfxLoop *loop = GfxLoop_default();
    return GfxLoop_run(loop, app);
}

void GfxLoop_modalTick(void) {
    GfxLoop *loop = GfxLoop_default();
    if (!loop)
        return;

    // 60Hz modal tick during live resize:
    // Guarantees CoreAnimation presents the freshest layer extent with NO gaps.
    GraphicsLayer_transactionBegin();
    if (Vk_ready())
        Vk_clearPresent();
    GraphicsLayer_transactionCommit();
}