#ifndef GRAPHICS_RENDER_LOOP_H
#define GRAPHICS_RENDER_LOOP_H

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>

// graphvex R3 — graphics/render_loop.h
//
// THE PRESENT-ON-DEMAND LOOP (restored + trimmed from the old graphics_loop).
// A client presents only when it has demand:
//
//     present  <=  painting event  ∨  markDirty(window)  ∨  a newer board generation
//
// No fixed cadence: a resting app presents ZERO times. vexspoke reactive setters
// raise the (coalesced) painting ticket; this loop drains it and presents once,
// so N state changes collapse into ONE frame. The loop owns no GPU resource and
// calls no verb — it is pure scheduling; the present hook is the seam.

typedef struct RenderLoop RenderLoop;

// Frame probe: called each step BEFORE the demand decision. Draws nothing;
// a client whose content changed calls RenderLoop_markDirty here.
typedef void (*FrameFn)(void *userdata, double dtSeconds);

// Present hook: build the display list, Graphics_submit it, present. Returns true
// only when a frame was actually presented.
typedef bool (*PresentFn)(void *window, double dtSeconds, void *userdata);

typedef struct Client {
    void *window;              // identity key (an R1 window handle)
    FrameFn frameFn;         // demand probe (nullable)
    PresentFn presentFn;     // composite + present (nullable = never presents)
    void *userdata;
    _Atomic bool dirty;
    uint64_t lastContentGen;   // last presented board generation
    uint64_t contentGen;       // current board generation
    bool hasPresented;
} Client;

RenderLoop *RenderLoop_0(void);
RenderLoop *RenderLoop_fps(uint32_t targetFps);   // 0 = pure demand
void RenderLoop_free(RenderLoop *loop);
RenderLoop *RenderLoop_default(void);             // process singleton

bool RenderLoop_addClient(RenderLoop *loop, const Client *client);
bool RenderLoop_removeClient(RenderLoop *loop, void *window);
Client *RenderLoop_findClient(RenderLoop *loop, const void *window);

void RenderLoop_markDirty(RenderLoop *loop, void *window);
void RenderLoop_setContentGen(RenderLoop *loop, void *window, uint64_t gen);
void RenderLoop_notify(RenderLoop *loop);         // thread-safe wake

// One demand-driven step. Returns true when at least one client presented.
bool RenderLoop_step(RenderLoop *loop);

#endif // GRAPHICS_RENDER_LOOP_H
