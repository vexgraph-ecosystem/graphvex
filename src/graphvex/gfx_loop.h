#ifndef GRAPHVEX_GFX_LOOP_H
#define GRAPHVEX_GFX_LOOP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "vulkan/graphics_layer.h"

// src/graphvex/gfx_loop.h — the demand-driven frame scheduler seam (the Vertical Integration Law:
// R3 DRIVER; the frame loop + event pump + presentation live HERE, never in
// the Kernel). Consumed by hotcwap's window bridge and by R5 applications.
//
// The 2-VkImage System:
//   Screen (Window) <- CAMetalLayer (MTK/Metal, presentsWithTransaction = YES)
//   <- 2 VkImages (scene board [bottom] + content board [top])
//   <- Scene/panel children of each board
//
// GfxLoop owns:
//   - Demand-probed frame loop (the Present-On-Demand Law): every client is
//     PROBED each step (frameFn observes caret/tree/pane demand and re-arms
//     GfxLoop_markDirty); a present fires only on demand, never on rest.
//   - Thread-0 event pump delegation (Window_pollEvents)
//   - Frame delta time (dt) and telemetry (fps, frametimeUs)
//   - Zero-gap live resize modal tick (GfxLoop_modalTick)
//   - CoreAnimation transaction bounds (GraphicsLayer_transactionBegin/Commit)

typedef struct GfxLoop GfxLoop;

// Frame callback signature: (window, dt, userdata)
typedef void (*GfxFrameFn)(void *window, double dt, void *userdata);

// Event pump hook signature: () -> void (delegates to Window_pollEvents)
typedef void (*GfxPollFn)(void);

// Client registration slot
typedef struct GfxClient {
    void *window;               // opaque hotcwap Window*
    void *app;                  // opaque hotcwap Application*
    GraphicsLayer *sceneLayer;   // bottom board (scenepane VkImage)
    GraphicsLayer *contentLayer; // top board (contentpane VkImage)
    GfxFrameFn frameFn;         // optional client frame callback
    void *userdata;
    _Atomic bool dirty;         // demand-driven dirty flag
} GfxClient;

struct GfxLoop {
    uint64_t typeId;
    GfxClient *clients;
    uint32_t clientCount;
    uint32_t clientCap;
    _Atomic bool running;
    GfxPollFn pollFn;           // installed Window_pollEvents
    uint32_t targetFps;         // target cadence (e.g. 60 or 120)
    uint64_t lastTimeNs;
    uint32_t frameCounter;
    uint64_t fpsTimerNs;
    uint32_t currentFps;
    uint32_t currentFrametimeUs;
};

// Constructors
GfxLoop *GfxLoop_0(void);
GfxLoop *GfxLoop_1(uint32_t targetFps);

#define GfxLoop(...) CONSTRUCTOR_DISPATCH(GfxLoop, __VA_ARGS__)

void GfxLoop_free(GfxLoop *self);
bool GfxLoop_isValid(const GfxLoop *self);

// Default singleton access
GfxLoop *GfxLoop_default(void);

// Client registration (dynamic doubling, Anti-Hardcoding Law)
bool GfxLoop_registerClient(GfxLoop *self, void *window, void *app,
                            GraphicsLayer *sceneLayer, GraphicsLayer *contentLayer,
                            GfxFrameFn frameFn, void *userdata);
bool GfxLoop_unregisterClient(GfxLoop *self, void *window);
GfxClient *GfxLoop_findClient(GfxLoop *self, const void *window);
uint32_t GfxLoop_getClientCount(const GfxLoop *self);

// Mark window demand-dirty
void GfxLoop_markDirty(GfxLoop *self, void *window);

// Hook installation for OS event pump
void GfxLoop_installPoll(GfxLoop *self, GfxPollFn pollFn);

// One frame step (calculates dt, pumps events, probes every client, presents
// on demand per the Present-On-Demand Law)
bool GfxLoop_step(GfxLoop *self);

// Full blocking run loop for an application (used by Kernel_runApplication).
// The LIFECYCLE CONTRACT is explicit — graphvex never inspects a foreign
// object's layout:
//   continueFn — "keep looping?" query, asked once per pass. It owns the
//                completion predicate AND any per-pass host servicing (e.g.
//                hot-reload polls). Null means loop until GfxLoop_stop.
//   pollFn     — the Thread-0 event pump, installed on the default loop for
//                the duration of the run. Null keeps whatever is installed.
typedef bool (*GfxContinueFn)(void *context);
int GfxLoop_run(GfxLoop *self, void *context, GfxContinueFn continueFn);

// Global bridge for hotcwap Kernel_runApplication
int GfxLoop_runApplication(void *context, GfxContinueFn continueFn, GfxPollFn pollFn);

// Modal tracking bridge: keeps presentation alive through live-resize drags
void GfxLoop_modalTick(void);

#endif