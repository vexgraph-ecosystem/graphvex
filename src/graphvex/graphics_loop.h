#ifndef GRAPHVEX_GRAPHICS_LOOP_H
#define GRAPHVEX_GRAPHICS_LOOP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "vulkan/graphics_layer.h"
#include "spoke/bespoke.h"

// src/graphvex/graphics_loop.h — the demand-driven frame scheduler seam (the Vertical Integration Law:
// R3 DRIVER; the frame loop + event pump + presentation live HERE, never in
// the Kernel). Consumed by hotcwap's window bridge and by R5 applications.
//
// The 2-VkImage System:
//   Screen (Window) <- CAMetalLayer (MTKLayer / Metal, presentsWithTransaction = YES)
//   <- 2 VkImages (scene board [bottom] + content board [top])
//   <- Scene/panel children of each board
//
// GraphicsLoop owns:
//   - Demand-probed frame loop (the Present-On-Demand Law): every client is
//     PROBED each step (frameFn observes caret/tree demand and re-arms
//     GraphicsLoop_markDirty); a present fires only on demand, never on rest.
//   - Per-Window Presentation Loop: checks each window's mtklayer and panel readiness
//     (scenePanel and contentPanel) before plastering and presenting with CATransaction
//     (presentsWithTransaction = YES).
//   - Thread-0 event pump delegation (Window_pollEvents)
//   - Frame delta time (dt) and telemetry (fps, frametimeUs)
//   - Zero-gap live resize modal tick (GraphicsLoop_modalTick)
//   - CoreAnimation transaction bounds (GraphicsLayer_transactionBegin/Commit)

typedef struct GraphicsLoop GraphicsLoop;

// Frame callback signature: (window, dt, userdata)
typedef void (*GraphicsFrameFn)(void *window, double dt, void *userdata);

// Event pump hook signature: () -> void (delegates to Window_pollEvents)
typedef void (*GraphicsPollFn)(void);

// Readiness check hook signature: () -> bool (checks mtklayer, scenepanel & contentpanel)
typedef bool (*GraphicsReadyFn)(void *window, void *userdata);

// Per-window present hook signature: () -> bool (plasters & presents into window mtklayer)
typedef bool (*GraphicsPresentFn)(void *window, void *userdata);

// Client registration slot
typedef struct GraphicsClient {
    void *window;                // opaque hotcwap Window*
    void *app;                   // opaque hotcwap Application*
    void *mtkLayer;              // CAMetalLayer (MTKLayer) for this window
    GraphicsLayer *sceneLayer;   // bottom board (scenepane VkImage)
    GraphicsLayer *contentLayer; // top board (contentpane VkImage)
    GraphicsFrameFn frameFn;     // optional client frame callback (probe)
    GraphicsReadyFn readyFn;     // optional panel readiness callback
    GraphicsPresentFn presentFn; // optional per-window present callback
    void *userdata;              // client context (e.g. Frame*)
    _Atomic bool dirty;          // demand-driven dirty flag
    bool hasPresented;           // has at least one frame successfully presented
} GraphicsClient;

struct GraphicsLoop {
    uint64_t typeId;
    GraphicsClient *clients;
    uint32_t clientCount;
    uint32_t clientCap;
    _Atomic bool running;
    GraphicsPollFn pollFn;       // installed Window_pollEvents
    uint32_t targetFps;          // target cadence (e.g. 60 or 120)
    uint64_t lastTimeNs;
    uint32_t frameCounter;
    uint64_t fpsTimerNs;
    uint32_t currentFps;
    uint32_t currentFrametimeUs;
};

// Constructors
GraphicsLoop *GraphicsLoop_0(void);
GraphicsLoop *GraphicsLoop_1(uint32_t targetFps);

#define GraphicsLoop(...) CONSTRUCTOR_DISPATCH(GraphicsLoop, __VA_ARGS__)

void GraphicsLoop_free(GraphicsLoop *self);
bool GraphicsLoop_isValid(const GraphicsLoop *self);

// Default singleton access
GraphicsLoop *GraphicsLoop_default(void);

// Client registration (dynamic doubling, Anti-Hardcoding Law)
bool GraphicsLoop_registerClient(GraphicsLoop *self, void *window, void *app,
                                 GraphicsLayer *sceneLayer, GraphicsLayer *contentLayer,
                                 GraphicsFrameFn frameFn, void *userdata);
bool GraphicsLoop_unregisterClient(GraphicsLoop *self, void *window);
GraphicsClient *GraphicsLoop_findClient(GraphicsLoop *self, const void *window);
uint32_t GraphicsLoop_getClientCount(const GraphicsLoop *self);

// Mark window demand-dirty
void GraphicsLoop_markDirty(GraphicsLoop *self, void *window);

// Hook installation for OS event pump
void GraphicsLoop_installPoll(GraphicsLoop *self, GraphicsPollFn pollFn);

// Per-client setters & getters
void GraphicsLoop_setClientMtkLayer(GraphicsLoop *self, void *window, void *mtkLayer);
void GraphicsLoop_setClientReadyFn(GraphicsLoop *self, void *window, GraphicsReadyFn readyFn);
void GraphicsLoop_setClientPresentFn(GraphicsLoop *self, void *window, GraphicsPresentFn presentFn);
void *GraphicsLoop_getClientMtkLayer(const GraphicsLoop *self, const void *window);

// One frame step (calculates dt, pumps events, probes every client, then
// loops each window, checks mtklayer + panel readiness, and presents on demand)
bool GraphicsLoop_step(GraphicsLoop *self);

// Full blocking run loop for an application (used by Kernel_runApplication).
typedef bool (*GraphicsContinueFn)(void *context);
int GraphicsLoop_run(GraphicsLoop *self, void *context, GraphicsContinueFn continueFn);

// Global bridge for hotcwap Kernel_runApplication
int GraphicsLoop_runApplication(void *context, GraphicsContinueFn continueFn, GraphicsPollFn pollFn);

// Modal tracking bridge: keeps presentation alive through live-resize drags
void GraphicsLoop_modalTick(void);

// Forced live-drag present for ONE window: ignores dirty + readyFn (the drag
// step IS the demand ticket) while minimized + Vk_ready + bounded GPU waits
// stay honored inside the present path; drops keep dirty armed.
void GraphicsLoop_modalTickForced(void *window);

// Bespoke Bridge implementation (connected to vexspoke spoke/bespoke.h)
bool         runGraphics(void);
BespokeState checkGraphics(void);

// --- Backwards-compatibility aliases ---
typedef GraphicsLoop GfxLoop;
typedef GraphicsClient GfxClient;
typedef GraphicsFrameFn GfxFrameFn;
typedef GraphicsPollFn GfxPollFn;
typedef GraphicsReadyFn GfxReadyFn;
typedef GraphicsPresentFn GfxPresentFn;
typedef GraphicsContinueFn GfxContinueFn;

#define GfxLoop_0 GraphicsLoop_0
#define GfxLoop_1 GraphicsLoop_1
#define GfxLoop GraphicsLoop
#define GfxLoop_free GraphicsLoop_free
#define GfxLoop_isValid GraphicsLoop_isValid
#define GfxLoop_default GraphicsLoop_default
#define GfxLoop_registerClient GraphicsLoop_registerClient
#define GfxLoop_unregisterClient GraphicsLoop_unregisterClient
#define GfxLoop_findClient GraphicsLoop_findClient
#define GfxLoop_getClientCount GraphicsLoop_getClientCount
#define GfxLoop_markDirty GraphicsLoop_markDirty
#define GfxLoop_installPoll GraphicsLoop_installPoll
#define GfxLoop_setClientMtkLayer GraphicsLoop_setClientMtkLayer
#define GfxLoop_setClientReadyFn GraphicsLoop_setClientReadyFn
#define GfxLoop_setClientPresentFn GraphicsLoop_setClientPresentFn
#define GfxLoop_getClientMtkLayer GraphicsLoop_getClientMtkLayer
#define GfxLoop_step GraphicsLoop_step
#define GfxLoop_run GraphicsLoop_run
int GfxLoop_runApplication(void *context, GraphicsContinueFn continueFn, GraphicsPollFn pollFn);
#define GfxLoop_modalTick GraphicsLoop_modalTick
#define GfxLoop_modalTickForced GraphicsLoop_modalTickForced

#endif // GRAPHVEX_GRAPHICS_LOOP_H
