#import <Cocoa/Cocoa.h>
#include <stdbool.h>
#include "annotation/overview.h"
#include "effect/visual_effect.h"

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: VisualEffect_cocoa (objc/visual_effect_cocoa.m)
 * LEVEL: L4 — Self-Management (native NSVisualEffectView platform bridge)
 * ============================================================================
 * Implements the macOS AppKit backing for graphvex VisualEffect. Owns the
 * creation, styling, and parenting of an NSVisualEffectView into the host
 * view hierarchy. Decoupled strictly from Window per the Window Decoupling
 * Law: Window is a dumb presentation surface; VisualEffect manages frosted
 * glass and background sampling.
 *
 * STRUCT FIELDS: none — procedural platform shim.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - VisualEffect_cocoaAttach(parentView, material, blur, vibrant)
 *   - VisualEffect_cocoaDetach(nativeHandle)
 *   - VisualEffect_cocoaSetBlur(nativeHandle, blur)
 *   - VisualEffect_cocoaSetMaterial(nativeHandle, material)
 *   - VisualEffect_cocoaSetVibrancy(nativeHandle, vibrant)
 *   - VisualEffect_cocoaSetActive(nativeHandle, active)
 * ============================================================================
 */

static NSVisualEffectMaterial resolveMaterial(int mat) {
    switch (mat) {
        case VISUAL_EFFECT_MATERIAL_SIDEBAR:
            return NSVisualEffectMaterialSidebar;
        case VISUAL_EFFECT_MATERIAL_HEADER:
            return NSVisualEffectMaterialHeaderView;
        case VISUAL_EFFECT_MATERIAL_FULLSCREEN:
            return NSVisualEffectMaterialFullScreenUI;
        case VISUAL_EFFECT_MATERIAL_HUD:
        default:
            return NSVisualEffectMaterialHUDWindow;
    }
}

void *VisualEffect_cocoaAttach(void *parentView, int material, float blur, bool vibrant) {
    if (parentView == nullptr)
        return nullptr;

    @autoreleasepool {
        NSView *parent = (__bridge NSView*) parentView;
        NSRect bounds = [parent bounds];

        NSVisualEffectView *vfx = [[NSVisualEffectView alloc] initWithFrame:bounds];
        [vfx setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
        [vfx setBlendingMode:NSVisualEffectBlendingModeBehindWindow];
        [vfx setMaterial:resolveMaterial(material)];
        [vfx setState:NSVisualEffectStateActive];

        if (blur >= 0.0f && blur <= 1.0f)
            [vfx setAlphaValue:(CGFloat) blur];

        (void) vibrant;

        [parent addSubview:vfx positioned:NSWindowBelow relativeTo:nil];
        return (__bridge_retained void*) vfx;
    }
}

void VisualEffect_cocoaDetach(void *nativeHandle) {
    if (nativeHandle == nullptr)
        return;

    @autoreleasepool {
        NSVisualEffectView *vfx = (__bridge_transfer NSVisualEffectView*) nativeHandle;
        [vfx removeFromSuperview];
    }
}

void VisualEffect_cocoaSetBlur(void *nativeHandle, float blur) {
    if (nativeHandle == nullptr)
        return;

    @autoreleasepool {
        NSVisualEffectView *vfx = (__bridge NSVisualEffectView*) nativeHandle;
        if (blur >= 0.0f && blur <= 1.0f)
            [vfx setAlphaValue:(CGFloat) blur];
    }
}

void VisualEffect_cocoaSetMaterial(void *nativeHandle, int material) {
    if (nativeHandle == nullptr)
        return;

    @autoreleasepool {
        NSVisualEffectView *vfx = (__bridge NSVisualEffectView*) nativeHandle;
        [vfx setMaterial:resolveMaterial(material)];
    }
}

void VisualEffect_cocoaSetVibrancy(void *nativeHandle, bool vibrant) {
    if (nativeHandle == nullptr)
        return;

    @autoreleasepool {
        NSVisualEffectView *vfx = (__bridge NSVisualEffectView*) nativeHandle;
        [vfx setBlendingMode:(vibrant ? NSVisualEffectBlendingModeWithinWindow : NSVisualEffectBlendingModeBehindWindow)];
    }
}

void VisualEffect_cocoaSetActive(void *nativeHandle, bool active) {
    if (nativeHandle == nullptr)
        return;

    @autoreleasepool {
        NSVisualEffectView *vfx = (__bridge NSVisualEffectView*) nativeHandle;
        [vfx setState:(active ? NSVisualEffectStateActive : NSVisualEffectStateInactive)];
    }
}
