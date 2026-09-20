#include "graphvex/graphics_loop.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"
#include "graphvex/type.h"
#include "time/nanotime.h"
#include "vulkan/vk.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: GraphicsLoop
 * ============================================================================
 * Demand-driven frame scheduler and presentation loop orchestrating per-window
 * rendering cycles in compliance with the Present-On-Demand Law and Continuous
 * Real-Time Live Resize Law. Manages an elastic array of registered graphics window
 * clients, coordinating event polling on thread zero with demand evaluation hooks.
 * Gates presentation through readiness predicates and executes buffer presentation
 * within atomic CoreAnimation transaction brackets (presentsWithTransaction = YES)
 * to guarantee tear-free visual updates during both steady-state rendering and
 * interactive modal resize operations.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: GraphicsLoop (graphvex/src/graphvex/graphics_loop.c)
 * LEVEL: L2 — Behavior (demand-driven frame scheduler, R3 Driver)
 * ============================================================================
 * Owns:
 *   1. Demand-driven frame loop (Present-On-Demand Law)
 *   2. Thread-0 event pump delegation (Window_pollEvents)
 *   3. 2-VkImage System (Scene Board + Content Board)
 *   4. Per-Window Presentation Loop (checks mtklayer, scenepanel & contentpanel)
 *   5. Zero-gap live resize modal tick (Continuous Real-Time Live Resize Law)
 *   6. CoreAnimation atomic transaction brackets (GraphicsLayer_transactionBegin/Commit)
 *
 * STRUCT FIELDS (Mirroring graphvex/graphics_loop.h):
 * ----------------------------------------------------------------------------
 *   uint64_t typeId;             // TYPE_GRAPHICS_LOOP_SINGLETON
 *   GraphicsClient *clients;     // dynamic doubling client registry
 *   uint32_t clientCount;        // registered windows
 *   uint32_t clientCap;          // allocated capacity
 *   _Atomic bool running;        // active loop flag
 *   GraphicsPollFn pollFn;       // Window_pollEvents hook
 *   uint32_t targetFps;          // 60/120Hz cadence
 *   uint64_t lastTimeNs;         // previous frame timestamp
 *   uint32_t frameCounter;       // for FPS calculation
 *   uint64_t fpsTimerNs;         // 1-second telemetry accumulator
 *   uint32_t currentFps;         // live FPS
 *   uint32_t currentFrametimeUs; // live frametime in microseconds
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - GraphicsLoop_0(void)                                                     : Default loop at 60 FPS
 *   - GraphicsLoop_1(targetFps)                                                : Sized loop with target FPS
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - GraphicsLoop_free(self)                                                  : Release loop resources
 *   - GraphicsLoop_default(void)                                               : Singleton default loop
 *   - GraphicsLoop_registerClient(self, window, app, sceneLayer, contentLayer, frameFn, userdata) : Register client
 *   - GraphicsLoop_unregisterClient(self, window)                             : Unregister client
 *   - GraphicsLoop_findClient(self, window)                                   : Lookup client
 *   - GraphicsLoop_markDirty(self, window)                                     : Mark client dirty
 *   - GraphicsLoop_installPoll(self, pollFn)                                   : Install event pump
 *   - GraphicsLoop_step(self)                                                  : Frame execution step
 *   - GraphicsLoop_run(self, context, continueFn)                              : Run throttled frame loop
 *   - GraphicsLoop_runApplication(context, continueFn, pollFn)                 : Run application frame loop
 *   - GfxLoop_runApplication(context, continueFn, pollFn)                      : Compatibility alias
 *   - GraphicsLoop_modalTick(void)                                             : Synchronous modal resize tick
 *   - GraphicsLoop_modalTickForced(window)                                     : Forced modal resize tick
 *   - runGraphics(void)                                                        : Advance default loop step
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - GraphicsLoop_setClientMtkLayer(self, window, mtkLayer)                   : Set client metal layer
 *   - GraphicsLoop_setClientReadyFn(self, window, readyFn)                     : Set client ready check
 *   - GraphicsLoop_setClientPresentFn(self, window, presentFn)                 : Set client present hook
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - GraphicsLoop_isValid(self)                                               : Check type tag validity
 *   - GraphicsLoop_getClientCount(self)                                        : Query client count
 *   - GraphicsLoop_getClientMtkLayer(self, window)                             : Query client metal layer
 *   - checkGraphics(void)                                                      : Query bespoke state
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

static GraphicsLoop *s_defaultLoop = nullptr;

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

GraphicsLoop *GraphicsLoop_0(void) {
    return GraphicsLoop_1(60);
}

GraphicsLoop *GraphicsLoop_1(uint32_t targetFps) {
    GraphicsLoop *self = (GraphicsLoop*) calloc(1, sizeof(GraphicsLoop));
    if (!self)
        return nullptr;
    (*self).typeId = TYPE_GRAPHICS_LOOP_SINGLETON;
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

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

void GraphicsLoop_free(GraphicsLoop *self) {
    if (!GraphicsLoop_isValid(self))
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

GraphicsLoop *GraphicsLoop_default(void) {
    if (!s_defaultLoop)
        s_defaultLoop = GraphicsLoop_0();
    return s_defaultLoop;
}

bool GraphicsLoop_registerClient(GraphicsLoop *self, void *window, void *app,
                                 GraphicsLayer *sceneLayer, GraphicsLayer *contentLayer,
                                 GraphicsFrameFn frameFn, void *userdata) {
    if (!GraphicsLoop_isValid(self) || !window)
        return false;
    for (uint32_t i = 0; i < (*self).clientCount; i++) {
        if ((*self).clients[i].window == window)
            return false;
    }
    if ((*self).clientCount >= (*self).clientCap) {
        uint32_t newCap = (*self).clientCap ? (*self).clientCap * 2 : 4;
        GraphicsClient *newSlots = (GraphicsClient*) realloc((*self).clients, newCap * sizeof(GraphicsClient));
        if (!newSlots)
            return false;
        (*self).clients = newSlots;
        (*self).clientCap = newCap;
    }
    GraphicsClient *client = &(*self).clients[(*self).clientCount++];
    (*client).window = window;
    (*client).app = app;
    (*client).mtkLayer = nullptr;
    (*client).sceneLayer = sceneLayer;
    (*client).contentLayer = contentLayer;
    (*client).frameFn = frameFn;
    (*client).readyFn = nullptr;
    (*client).presentFn = nullptr;
    (*client).userdata = userdata;
    (*client).hasPresented = false;
    atomic_store_explicit(&(*client).dirty, true, memory_order_relaxed);
    return true;
}

bool GraphicsLoop_unregisterClient(GraphicsLoop *self, void *window) {
    if (!GraphicsLoop_isValid(self) || !window)
        return false;
    for (uint32_t i = 0; i < (*self).clientCount; i++) {
        if ((*self).clients[i].window == window) {
            (*self).clients[i] = (*self).clients[--(*self).clientCount];
            return true;
        }
    }
    return false;
}

GraphicsClient *GraphicsLoop_findClient(GraphicsLoop *self, const void *window) {
    if (!GraphicsLoop_isValid(self) || !window)
        return nullptr;
    for (uint32_t i = 0; i < (*self).clientCount; i++) {
        if ((*self).clients[i].window == window)
            return &(*self).clients[i];
    }
    return nullptr;
}

void GraphicsLoop_markDirty(GraphicsLoop *self, void *window) {
    GraphicsClient *client = GraphicsLoop_findClient(self, window);
    if (client)
        atomic_store_explicit(&(*client).dirty, true, memory_order_relaxed);
}

void GraphicsLoop_installPoll(GraphicsLoop *self, GraphicsPollFn pollFn) {
    if (GraphicsLoop_isValid(self))
        (*self).pollFn = pollFn;
}

bool GraphicsLoop_step(GraphicsLoop *self) {
    if (!GraphicsLoop_isValid(self))
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

    // 3. Loop 1: Demand probe loop (the Present-On-Demand Law).
    // Every client is PROBED each step (frameFn = the demand probe: it
    // observes caret phases, tree dirt and live resize, then
    // re-arms the dirty flag via GraphicsLoop_markDirty).
    for (uint32_t i = 0; i < (*self).clientCount; i++) {
        GraphicsClient *client = &(*self).clients[i];
        if ((*client).frameFn)
            (*client).frameFn((*client).window, dt, (*client).userdata);
        if (!(*client).hasPresented)
            atomic_store_explicit(&(*client).dirty, true, memory_order_relaxed);
    }

    // 4. Loop 2: Per-Window Presentation Loop.
    // Checks each window individually:
    //   - Verifies the window is marked dirty.
    //   - Checks that the window's mtklayer and scenepanel & contentpanel are ready.
    //   - If ready, plasters into the mtklayer and presents with CATransaction (presentsWithTransaction = YES).
    for (uint32_t i = 0; i < (*self).clientCount; i++) {
        GraphicsClient *client = &(*self).clients[i];
        if (!atomic_load_explicit(&(*client).dirty, memory_order_relaxed))
            continue;

        bool ready = true;
        if ((*client).readyFn) {
            ready = (*client).readyFn((*client).window, (*client).userdata);
        } else if ((*client).sceneLayer || (*client).contentLayer) {
            if ((*client).sceneLayer && !GraphicsLayer_isAttached((*client).sceneLayer))
                ready = false;
            if ((*client).contentLayer && !GraphicsLayer_isAttached((*client).contentLayer))
                ready = false;
        }

        if (!ready) {
            // Panels or mtklayer not ready yet — keep dirty armed and defer present
            continue;
        }

        // Atomic CoreAnimation transaction bracket: presentsWithTransaction = YES
        GraphicsLayer_transactionBegin();
        bool presented = false;
        if ((*client).presentFn) {
            presented = (*client).presentFn((*client).window, (*client).userdata);
        } else if (Vk_ready()) {
            presented = Vk_clearPresent();
        }
        GraphicsLayer_transactionCommit();

        if (presented) {
            (*client).hasPresented = true;
            atomic_store_explicit(&(*client).dirty, false, memory_order_relaxed);
        }
    }

    return true;
}

int GraphicsLoop_run(GraphicsLoop *self, void *context, GraphicsContinueFn continueFn) {
    if (!GraphicsLoop_isValid(self))
        return -1;

    atomic_store_explicit(&(*self).running, true, memory_order_relaxed);

    // Frame throttle (e.g. 60Hz = ~16.6ms)
    uint64_t targetSliceNs = 1000000000ULL / (*self).targetFps;
    const struct timespec restSlice = {0, 1000000L}; // 1ms sleep

    while (atomic_load_explicit(&(*self).running, memory_order_relaxed)) {
        if (continueFn && !continueFn(context))
            break;

        uint64_t startNs = NanoTime_now();

        GraphicsLoop_step(self);

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

int GraphicsLoop_runApplication(void *context, GraphicsContinueFn continueFn, GraphicsPollFn pollFn) {
    GraphicsLoop *loop = GraphicsLoop_default();
    if (pollFn)
        GraphicsLoop_installPoll(loop, pollFn);
    return GraphicsLoop_run(loop, context, continueFn);
}

// Global alias for backwards compatibility with hotcwap weak link
int GfxLoop_runApplication(void *context, GraphicsContinueFn continueFn, GraphicsPollFn pollFn) {
    return GraphicsLoop_runApplication(context, continueFn, pollFn);
}

void GraphicsLoop_modalTick(void) {
    GraphicsLoop *loop = GraphicsLoop_default();
    if (!loop)
        return;

    // Per-drag-step modal tick during live resize (the Continuous Real-Time
    // Live Resize Law): presents only DIRTY+ready clients atomically per
    // step — the resizing window renders at the NEW size and presents
    // immediately; idle windows rest (the Present-On-Demand Law). The
    // resize hook (frameCocoaResizeHook) re-arms the client dirty before
    // calling this, so the drag step is the ticket that summons the
    // synchronous present.
    for (uint32_t i = 0; i < (*loop).clientCount; i++) {
        GraphicsClient *client = &(*loop).clients[i];
        if (!atomic_load_explicit(&(*client).dirty, memory_order_relaxed))
            continue;
        bool ready = true;
        if ((*client).readyFn)
            ready = (*client).readyFn((*client).window, (*client).userdata);
        if (!ready)
            continue;

        GraphicsLayer_transactionBegin();
        bool presented = false;
        if ((*client).presentFn) {
            presented = (*client).presentFn((*client).window, (*client).userdata);
        } else if (Vk_ready()) {
            presented = Vk_clearPresent();
        }
        GraphicsLayer_transactionCommit();

        if (presented) {
            (*client).hasPresented = true;
            atomic_store_explicit(&(*client).dirty, false, memory_order_relaxed);
        }
    }
}

void GraphicsLoop_modalTickForced(void *window) {
    GraphicsLoop *loop = GraphicsLoop_default();
    if (loop == nullptr || window == nullptr)
        return;
    GraphicsClient *client = GraphicsLoop_findClient(loop, window);
    if (client == nullptr)
        return;

    // Forced live-drag present for ONE window (the Continuous Real-Time
    // Live Resize Law, aggressive demand): the drag step IS the ticket, so
    // dirty + readyFn are ignored — this path CANNOT skip on not-dirty or
    // not-ready. Minimized + Vk_ready + bounded GPU waits stay honored
    // inside the present path (the per-window presentFn keeps its own
    // minimized + Vk_ready guards; the bare path checks Vk_ready and
    // Vk_clearPresentLive gates device-lost + minimized with the same
    // 100ms fence / 25ms acquire bounds, retrying only the try-lock in
    // ~1ms slices up to ~8ms). A drop keeps dirty armed so the next drag
    // step retries with fresher state (the Present-On-Demand Law); the
    // caller (frameCocoaResizeHook) owns the outer CATransaction this
    // begin/commit pair nests inside. No new thread, no unbounded wait.
    GraphicsPresentFn presentFn = (*client).presentFn;
    void *clientWindow = (*client).window;
    void *clientData = (*client).userdata;
    GraphicsLayer_transactionBegin();
    bool presented = false;
    if (presentFn != nullptr)
        presented = presentFn(clientWindow, clientData);
    else if (Vk_ready())
        presented = Vk_clearPresentLive();
    GraphicsLayer_transactionCommit();

    if (presented) {
        (*client).hasPresented = true;
        atomic_store_explicit(&(*client).dirty, false, memory_order_relaxed);
    } else {
        atomic_store_explicit(&(*client).dirty, true, memory_order_relaxed);
    }
}

bool runGraphics(void) {
    GraphicsLoop *loop = GraphicsLoop_default();
    if (!loop)
        return false;
    return GraphicsLoop_step(loop);
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void GraphicsLoop_setClientMtkLayer(GraphicsLoop *self, void *window, void *mtkLayer) {
    GraphicsClient *client = GraphicsLoop_findClient(self, window);
    if (client)
        (*client).mtkLayer = mtkLayer;
}

;;SETTER
void GraphicsLoop_setClientReadyFn(GraphicsLoop *self, void *window, GraphicsReadyFn readyFn) {
    GraphicsClient *client = GraphicsLoop_findClient(self, window);
    if (client)
        (*client).readyFn = readyFn;
}

;;SETTER
void GraphicsLoop_setClientPresentFn(GraphicsLoop *self, void *window, GraphicsPresentFn presentFn) {
    GraphicsClient *client = GraphicsLoop_findClient(self, window);
    if (client)
        (*client).presentFn = presentFn;
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
bool GraphicsLoop_isValid(const GraphicsLoop *self) {
    return self && (*self).typeId == TYPE_GRAPHICS_LOOP_SINGLETON;
}

;;GETTER
uint32_t GraphicsLoop_getClientCount(const GraphicsLoop *self) {
    return GraphicsLoop_isValid(self) ? (*self).clientCount : 0;
}

;;GETTER
void *GraphicsLoop_getClientMtkLayer(const GraphicsLoop *self, const void *window) {
    GraphicsClient *client = GraphicsLoop_findClient((GraphicsLoop*) self, window);
    return client ? (*client).mtkLayer : nullptr;
}

;;GETTER
BespokeState checkGraphics(void) {
    GraphicsLoop *loop = GraphicsLoop_default();
    if (!loop)
        return BESPOKE_ABSENT;
    if (GraphicsLoop_getClientCount(loop) == 0)
        return BESPOKE_LATE;
    if (atomic_load_explicit(&(*loop).running, memory_order_relaxed))
        return BESPOKE_RUNNING;
    return BESPOKE_READY;
}
