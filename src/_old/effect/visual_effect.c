#include "effect/visual_effect.h"
#include <stdlib.h>
#include <string.h>
#include "annotation/definition.h"
#include "annotation/overview.h"

#if defined(__APPLE__)
extern void *VisualEffect_cocoaAttach(void *parentView, int material, float blur, bool vibrant);
extern void  VisualEffect_cocoaDetach(void *nativeHandle);
extern void  VisualEffect_cocoaSetBlur(void *nativeHandle, float blur);
extern void  VisualEffect_cocoaSetMaterial(void *nativeHandle, int material);
extern void  VisualEffect_cocoaSetVibrancy(void *nativeHandle, bool vibrant);
extern void  VisualEffect_cocoaSetActive(void *nativeHandle, bool active);
#endif

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: VisualEffect
 * ============================================================================
 * Hardware-accelerated background blur, frosted glass, and vibrancy surface.
 * Decoupled from Window according to the Window Decoupling Law, living in
 * graphvex at R3. Owns the platform-native material view (NSVisualEffectView
 * on macOS) and manages its lifecycle, blur alpha, and material properties.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VisualEffect (effect/visual_effect.c)
 * LEVEL: L3 — Module Code (graphvex display effect surface)
 * ============================================================================
 * SUMMARY:
 *   Manages frosted glass and vibrancy effects independently of the window.
 *   On macOS, bridges into NSVisualEffectView; on other platforms, degrades
 *   safely to a transparent or tint placeholder.
 *
 * STRUCT FIELDS (Mirroring effect/visual_effect.h):
 * ----------------------------------------------------------------------------
 *   void *nativeHandle;     // OS view pointer (NSVisualEffectView on macOS)
 *   void *parentView;       // Parent NSView / container
 *   float blur;             // Alpha / blur strength [0.0f .. 1.0f]
 *   int material;           // Active material preset
 *   bool vibrant;           // Vibrancy toggle
 *   bool active;            // Active state toggle
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - VisualEffect_0(void)
 *   - VisualEffect_1(nativeView)
 *
 * Public Core Functions: (.h)
 *   - VisualEffect_free(vfx)
 *   - VisualEffect_attach(vfx, nativeView)
 *   - VisualEffect_detach(vfx)
 *   - VisualEffect_nativeHandle(vfx)
 *
 * Public Setters: (.h)
 *   - VisualEffect_setBlur(vfx, blur)
 *   - VisualEffect_setMaterial(vfx, material)
 *   - VisualEffect_setVibrancy(vfx, vibrant)
 *   - VisualEffect_setActive(vfx, active)
 *
 * Public Getters: (.h)
 *   - VisualEffect_getBlur(vfx)
 *   - VisualEffect_getMaterial(vfx)
 *   - VisualEffect_isVibrant(vfx)
 *   - VisualEffect_isActive(vfx)
 * ============================================================================
 */

VisualEffect *VisualEffect_0(void) {
    VisualEffect *vfx = (VisualEffect*) calloc(1, sizeof(VisualEffect));
    if (vfx == nullptr)
        return nullptr;

    (*vfx).blur = 1.0f;
    (*vfx).material = VISUAL_EFFECT_MATERIAL_HUD;
    (*vfx).vibrant = false;
    (*vfx).active = true;
    return vfx;
}

VisualEffect *VisualEffect_1(void *nativeView) {
    VisualEffect *vfx = VisualEffect_0();
    if (vfx == nullptr)
        return nullptr;

    if (nativeView != nullptr)
        VisualEffect_attach(vfx, nativeView);

    return vfx;
}

void VisualEffect_free(VisualEffect *vfx) {
    if (vfx == nullptr)
        return;

    VisualEffect_detach(vfx);
    free(vfx);
}

bool VisualEffect_attach(VisualEffect *vfx, void *nativeView) {
    if (vfx == nullptr || nativeView == nullptr)
        return false;

    if ((*vfx).nativeHandle != nullptr)
        VisualEffect_detach(vfx);

#if defined(__APPLE__)
    (*vfx).parentView = nativeView;
    (*vfx).nativeHandle = VisualEffect_cocoaAttach(nativeView, (*vfx).material, (*vfx).blur, (*vfx).vibrant);
    return (*vfx).nativeHandle != nullptr;
#else
    (*vfx).parentView = nativeView;
    return true;
#endif
}

void VisualEffect_detach(VisualEffect *vfx) {
    if (vfx == nullptr || (*vfx).nativeHandle == nullptr)
        return;

#if defined(__APPLE__)
    VisualEffect_cocoaDetach((*vfx).nativeHandle);
#endif
    (*vfx).nativeHandle = nullptr;
    (*vfx).parentView = nullptr;
}

void VisualEffect_setBlur(VisualEffect *vfx, float blur) {
    if (vfx == nullptr)
        return;

    (*vfx).blur = blur;
#if defined(__APPLE__)
    if ((*vfx).nativeHandle != nullptr)
        VisualEffect_cocoaSetBlur((*vfx).nativeHandle, blur);
#endif
}

float VisualEffect_getBlur(const VisualEffect *vfx) {
    if (vfx == nullptr)
        return 0.0f;
    return (*vfx).blur;
}

void VisualEffect_setMaterial(VisualEffect *vfx, int material) {
    if (vfx == nullptr)
        return;

    (*vfx).material = material;
#if defined(__APPLE__)
    if ((*vfx).nativeHandle != nullptr)
        VisualEffect_cocoaSetMaterial((*vfx).nativeHandle, material);
#endif
}

int VisualEffect_getMaterial(const VisualEffect *vfx) {
    if (vfx == nullptr)
        return VISUAL_EFFECT_MATERIAL_HUD;
    return (*vfx).material;
}

void VisualEffect_setVibrancy(VisualEffect *vfx, bool vibrant) {
    if (vfx == nullptr)
        return;

    (*vfx).vibrant = vibrant;
#if defined(__APPLE__)
    if ((*vfx).nativeHandle != nullptr)
        VisualEffect_cocoaSetVibrancy((*vfx).nativeHandle, vibrant);
#endif
}

bool VisualEffect_isVibrant(const VisualEffect *vfx) {
    if (vfx == nullptr)
        return false;
    return (*vfx).vibrant;
}

void VisualEffect_setActive(VisualEffect *vfx, bool active) {
    if (vfx == nullptr)
        return;

    (*vfx).active = active;
#if defined(__APPLE__)
    if ((*vfx).nativeHandle != nullptr)
        VisualEffect_cocoaSetActive((*vfx).nativeHandle, active);
#endif
}

bool VisualEffect_isActive(const VisualEffect *vfx) {
    if (vfx == nullptr)
        return false;
    return (*vfx).active;
}

void *VisualEffect_nativeHandle(const VisualEffect *vfx) {
    if (vfx == nullptr)
        return nullptr;
    return (*vfx).nativeHandle;
}
