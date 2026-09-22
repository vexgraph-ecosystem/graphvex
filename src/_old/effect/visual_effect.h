#ifndef EFFECT_VISUAL_EFFECT_H
#define EFFECT_VISUAL_EFFECT_H

#include <stdbool.h>
#include <stdint.h>
#include "c23/constructor.h"
#include "../graphics/type.h"

// effect/visual_effect.h — Platform-abstracted visual effect & blur surface.
//
// Single Class Per File Law: VisualEffect.
//
// Encapsulates hardware-accelerated frosted glass, material blur, and
// vibrancy (NSVisualEffectView on macOS). Decoupled strictly from Window
// (the Window Decoupling Law) and owned by graphvex at R3.
//
// A VisualEffect attaches to a native window or view (typically placed
// immediately behind the primary presentation Surface layer).
//
// Materials:
//   VISUAL_EFFECT_MATERIAL_HUD        0 (HUD window / dark frosted glass)
//   VISUAL_EFFECT_MATERIAL_SIDEBAR    1 (macOS sidebar material)
//   VISUAL_EFFECT_MATERIAL_HEADER     2 (titlebar/header material)
//   VISUAL_EFFECT_MATERIAL_FULLSCREEN 3 (fullscreen sheet / overlay)

#define VISUAL_EFFECT_MATERIAL_HUD        0
#define VISUAL_EFFECT_MATERIAL_SIDEBAR    1
#define VISUAL_EFFECT_MATERIAL_HEADER     2
#define VISUAL_EFFECT_MATERIAL_FULLSCREEN 3

typedef struct VisualEffect {
    void *nativeHandle;     // OS view pointer (NSVisualEffectView on macOS)
    void *parentView;       // Parent NSView / container
    float blur;             // Alpha / blur strength [0.0f .. 1.0f]
    int material;           // Active material preset
    bool vibrant;           // Vibrancy toggle
    bool active;            // Active state toggle
} VisualEffect;

// Constructors
VisualEffect *VisualEffect_0(void);
VisualEffect *VisualEffect_1(void *nativeView);

#define VisualEffect(...) CONSTRUCTOR_DISPATCH(VisualEffect, __VA_ARGS__)

// Lifecycle
void VisualEffect_free(VisualEffect *vfx);
bool VisualEffect_attach(VisualEffect *vfx, void *nativeView);
void VisualEffect_detach(VisualEffect *vfx);

// Configuration
void  VisualEffect_setBlur(VisualEffect *vfx, float blur);
float VisualEffect_getBlur(const VisualEffect *vfx);

void VisualEffect_setMaterial(VisualEffect *vfx, int material);
int  VisualEffect_getMaterial(const VisualEffect *vfx);

void VisualEffect_setVibrancy(VisualEffect *vfx, bool vibrant);
bool VisualEffect_isVibrant(const VisualEffect *vfx);

void VisualEffect_setActive(VisualEffect *vfx, bool active);
bool VisualEffect_isActive(const VisualEffect *vfx);

void *VisualEffect_nativeHandle(const VisualEffect *vfx);

#endif
