#include "graphvex/gfx_loop.h"

#include "annotation/draft.h"
#include "annotation/intention.h"
#include "annotation/overview.h"

;;DRAFT
;;INTENTION("GfxLoop_modalTick is the Window-bridge seam requested by the Vertical Integration Law + the Continuous Real-Time Live Resize Law: the AppKit NSTimer during NSEventTracking spins this at 60Hz so the compositor keeps presenting through live-resize drags. The real present walk + frame scheduler lands with the GfxLoop class commit; today the seam is a guarded no-op so the window bridge links green.")

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: GfxLoop (graphvex/src/graphvex/gfx_loop.c)
 * LEVEL: L2 — Behavior
 * ============================================================================
 * The demand-driven frame scheduler seam that owns the frame loop, the
 * Thread-0 event pump, and present-on-demand presentation (the Window
 * Compositing Layer Order Law, the Present-On-Demand Law, and the Vertical
 * Integration Law). Lives in R3 graphvex so the R1 Kernel stays a pure storage + dispatch
 * object — the Kernel never runs a loop. Today only the modal-tracking
 * bridge is live: GfxLoop_modalTick, invoked by the Window's NSTimer during
 * AppKit modal tracking loops (live resize) so presentation survives Thread 0
 * being parked. The full class lands in its own commit.
 *
 * STRUCT FIELDS: none (seam-only, ;;DRAFT).
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - GfxLoop_modalTick() : keep the presenter awake during AppKit modal loops
 * ============================================================================
 */

void GfxLoop_modalTick(void) {
    return;
}