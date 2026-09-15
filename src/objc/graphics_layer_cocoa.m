#import <QuartzCore/QuartzCore.h>
#import <Cocoa/Cocoa.h>
#include <stdbool.h>
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: GraphicsLayer_cocoa (objc/graphics_layer_cocoa.m)
 * LEVEL: L4 — Self-Management (platform CAMetalLayer factory for GraphicsLayer)
 * ============================================================================
 * The Apple backend of the GraphicsLayer class (same GraphicsLayer_* prefix,
 * no second struct per the Single Class Per File Law): creates bare CAMetalLayer boards, applies
 * native-pixel drawable sizes, and stores the opaque device handle. The
 * window shim never calls here — the renderer (graphvex Vulkan side) does,
 * then hands the bare void* handle to Window_setTopLayer / setBottomLayer
 * for PARENTING ONLY. Content vs parenting stays split across the seam.
 *
 * Every layer pins top-left (kCAGravityTopLeft, anchorPoint (0,0),
 * geometryFlipped YES) and presents with the WindowServer transaction
 * (presentsWithTransaction YES) per the Window Compositing Layer Order Law — edge-locked, zero CPU catch-up.
 * device starts nil: the renderer assigns the shared MTLDevice afterMake.
 *
 * STRUCT FIELDS: none — procedural factory (no struct, no behavior state).
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - GraphicsLayer_makeLayer(role)              : bare CAMetalLayer as void*
 *   - GraphicsLayer_freeLayer(layer)
 *   - GraphicsLayer_applySize(layer, pxW, pxH, scale)
 *   - GraphicsLayer_setDevice(layer, device)
 * ============================================================================
 */

// One bare board: TopLeft-pinned, transaction-presented, device unset.
// role is stored nowhere on the layer — the GraphicsLayer struct owns it.
void *GraphicsLayer_makeLayer(int role) {
    (void) role;
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.contentsGravity = kCAGravityTopLeft;
    layer.anchorPoint = CGPointMake(0.0, 0.0);
    layer.geometryFlipped = YES;
    layer.opaque = NO;
    layer.presentsWithTransaction = YES;
    layer.device = nil;
    return (__bridge_retained void*) layer;
}

void GraphicsLayer_freeLayer(void *layer) {
    if (!layer)
        return;
    @autoreleasepool {
        (void) (__bridge_transfer CAMetalLayer*) layer;
    }
}

// Native-pixel commit: drawableSize lands in hardware pixels, contentsScale
// maps them 1:1 to logical points (the Native Pixel Law). False on hostile input.
bool GraphicsLayer_applySize(void *layer, int pxW, int pxH, float scale) {
    if (!layer)
        return false;
    if (pxW <= 0)
        return false;
    if (pxH <= 0)
        return false;
    if (scale <= 0.0f)
        return false;
    @autoreleasepool {
        CAMetalLayer *metal = (__bridge CAMetalLayer*) layer;
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        metal.drawableSize = CGSizeMake((CGFloat) pxW, (CGFloat) pxH);
        metal.contentsScale = (CGFloat) scale;
        metal.contentsGravity = kCAGravityTopLeft;
        [CATransaction commit];
    }
    return true;
}

// Opaque device store: retained by the layer, never dereferenced here.
void GraphicsLayer_setDevice(void *layer, void *device) {
    if (!layer)
        return;
    @autoreleasepool {
        CAMetalLayer *metal = (__bridge CAMetalLayer*) layer;
        metal.device = (__bridge id<MTLDevice>) device;
    }
}
