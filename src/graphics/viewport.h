#ifndef GRAPHICS_VIEWPORT_H
#define GRAPHICS_VIEWPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "c23/constructor.h"

#include "graphics/graphics.h"

// graphvex R3 — graphics/viewport.h
//
// The drawable window viewport. NOT a virtual canvas: a desktop UI works in
// LOGICAL POINTS at 1x and converts to NATIVE PIXELS only at the draw boundary.
// Retina: 1 point = backingScale pixels. There is deliberately NO FIT/STRETCH /
// virtual-resolution here — that is a *scene* concern (a separate SceneCanvas),
// never the UI path (the Absolute Size and Location Law).
//
// Forward Rendering & Bounded Surface Law: the viewport's scissor is the only
// bound — nothing outside it allocates or draws.

typedef struct Viewport {
    float width;    // framebuffer width  in native px
    float height;   // framebuffer height in native px
    float scale;    // backingScale: points → px (2.0 retina, else 1.0)
    Rect  scissor;  // visible rect in native px (usually the full framebuffer)
} Viewport;
typedef struct ViewportPoint { float x; float y; } ViewportPoint;
typedef struct ViewportSize { float width; float height; } ViewportSize;

Viewport Viewport_0(void);   // 0x0, scale 1
#define Viewport(...) CONSTRUCTOR_DISPATCH(Viewport, __VA_ARGS__)

void Viewport_resize(Viewport *v, float framebufferWidthPx, float framebufferHeightPx,
                     float backingScale);

// logical points (top-left, Y-down) → native px
float Viewport_x(const Viewport *v, float pointX);
float Viewport_y(const Viewport *v, float pointY);
float Viewport_w(const Viewport *v, float pointW);
float Viewport_h(const Viewport *v, float pointH);
Rect  Viewport_rect(const Viewport *v, float xPoints, float yPoints, float wPoints, float hPoints);

// native px → logical points
ViewportPoint Viewport_toPoints(const Viewport *v, float px, float py);

// the framebuffer's logical size, in points
ViewportSize Viewport_logicalSize(const Viewport *v);

bool Viewport_isEmpty(const Viewport *v);

#endif // GRAPHICS_VIEWPORT_H
