#ifndef GRAPHVEX_GFX_LOOP_H
#define GRAPHVEX_GFX_LOOP_H

// src/graphvex/gfx_loop.h — the demand-driven frame scheduler seam (the Vertical Integration Law:
// R3 D RIVER; the frame loop + event pump + presentation live HERE, never in
// the Kernel). Consumed by hotcwap's window bridge and by R5 applications.

// ;;DRAFT — the full GfxLoop class (frame cadence, event pump on Thread 0,
// present-on-demand per the Present-On-Demand Law, continuous live-resize motion per the Continuous Real-Time Live Resize Law)
// lands with the dedicated graphvex commit. Only the modal-tracking bridge is
// live today so the window's AppKit runloop can keep the presenter awake
// during model modal loops.

// Called by the Window's NSTimer bridge (NSEventTrackingRunLoopMode /
// NSModalPanelRunLoopMode, 60Hz) while Thread 0 is parked inside an AppKit
// model loop; keeps presentation alive through live-resize drags without
// blocking. Safe no-op until the GfxLoop is registered.
void GfxLoop_modalTick(void);

#endif