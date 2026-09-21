#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#include <stdbool.h>
#include "annotation/overview.h"
#include "surface/surface.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Surface_cocoa (objc/surface_cocoa.m)
 * LEVEL: L4 — Self-Management (native IOSurface + CALayer presentation bridge)
 * ============================================================================
 * Implements the Apple zero-swapchain presentation pipeline. Allocates a
 * display-sized IOSurface in kernel memory, binds it as a CALayer's contents
 * with kCAGravityTopLeft, and manages atomic frame updates during live drag.
 *
 * STRUCT FIELDS: none — procedural platform shim.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - Surface_cocoaCreateIOSurface(w, h)
 *   - Surface_cocoaFreeIOSurface(surf)
 *   - Surface_cocoaAttach(parentView, ioSurface, scale)
 *   - Surface_cocoaDetach(caLayer)
 *   - Surface_cocoaSync(caLayer, ptW, ptH, scale)
 *   - Surface_cocoaPresent(caLayer)
 *   - Surface_cocoaCreateMetalTexture(ioSurface, mtlDevice)
 * ============================================================================
 */

void *Surface_cocoaCreateIOSurface(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return nullptr;

    @autoreleasepool {
        NSDictionary *props = @{
            (id)kIOSurfaceWidth: @(width),
            (id)kIOSurfaceHeight: @(height),
            (id)kIOSurfaceBytesPerElement: @4,
            (id)kIOSurfacePixelFormat: @(0x42475241) // 'BGRA'
        };

        IOSurfaceRef surf = IOSurfaceCreate((__bridge CFDictionaryRef)props);
        return (void*) surf;
    }
}

void Surface_cocoaFreeIOSurface(void *ioSurface) {
    if (ioSurface == nullptr)
        return;

    CFRelease((IOSurfaceRef) ioSurface);
}

void *Surface_cocoaAttach(void *parentView, void *ioSurface, float scale) {
    if (parentView == nullptr || ioSurface == nullptr)
        return nullptr;

    @autoreleasepool {
        NSView *view = (__bridge NSView*) parentView;
        if (![view wantsLayer])
            [view setWantsLayer:YES];

        CALayer *root = [view layer];
        if (root == nil)
            return nullptr;

        CALayer *layer = [CALayer layer];
        layer.name = @"vexgraph.surface";
        layer.contents = (__bridge id)(IOSurfaceRef) ioSurface;
        layer.contentsGravity = kCAGravityTopLeft;
        layer.anchorPoint = CGPointMake(0.0, 0.0);
        layer.geometryFlipped = YES;
        layer.autoresizingMask = kCALayerNotSizable;

        CGFloat s = (scale > 0.0f) ? (CGFloat) scale : 1.0;
        layer.contentsScale = s;

        NSRect b = [view bounds];
        layer.frame = b;
        layer.bounds = b;
        layer.position = CGPointMake(0.0, 0.0);

        [root addSublayer:layer];
        return (__bridge_retained void*) layer;
    }
}

void Surface_cocoaDetach(void *caLayer) {
    if (caLayer == nullptr)
        return;

    @autoreleasepool {
        CALayer *layer = (__bridge_transfer CALayer*) caLayer;
        [layer removeFromSuperlayer];
    }
}

void Surface_cocoaSync(void *caLayer, int ptW, int ptH, float scale) {
    if (caLayer == nullptr || ptW <= 0 || ptH <= 0)
        return;

    @autoreleasepool {
        CALayer *layer = (__bridge CALayer*) caLayer;
        CGFloat s = (scale > 0.0f) ? (CGFloat) scale : 1.0;
        layer.contentsScale = s;

        NSRect r = NSMakeRect(0.0, 0.0, (CGFloat) ptW, (CGFloat) ptH);
        layer.frame = r;
        layer.bounds = r;
        layer.position = CGPointMake(0.0, 0.0);
    }
}

void Surface_cocoaPresent(void *caLayer) {
    if (caLayer == nullptr)
        return;

    @autoreleasepool {
        CALayer *layer = (__bridge CALayer*) caLayer;
        [layer setNeedsDisplay];
    }
}

void *Surface_cocoaCreateMetalTexture(void *ioSurface, void *mtlDevice) {
    if (ioSurface == nullptr)
        return nullptr;

    @autoreleasepool {
        id<MTLDevice> dev = mtlDevice ? (__bridge id<MTLDevice>) mtlDevice : MTLCreateSystemDefaultDevice();
        if (dev == nil)
            return nullptr;

        IOSurfaceRef surf = (IOSurfaceRef) ioSurface;
        size_t w = IOSurfaceGetWidth(surf);
        size_t h = IOSurfaceGetHeight(surf);

        MTLTextureDescriptor *desc = [MTLTextureDescriptor 
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                         width:w
                                        height:h
                                     mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;

        id<MTLTexture> tex = [dev newTextureWithDescriptor:desc iosurface:surf plane:0];
        return (__bridge_retained void*) tex;
    }
}

void Surface_cocoaFreeMetalTexture(void *metalTexture) {
    if (metalTexture == nullptr)
        return;

    CFRelease((CFTypeRef) metalTexture);
}

