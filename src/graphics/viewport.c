#include "graphics/viewport.h"
#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/** Viewport stores a framebuffer's native-pixel extent and converts UI geometry
 * between logical points and physical pixels using the admitted backing scale.
 * It is pure geometry: no virtual canvas, allocation, device or scene mode. */
;;OVERVIEW
/** ========================================================================
 * CLASS: Viewport (graphics/viewport.c)
 * STRUCT FIELDS (Mirroring graphics/viewport.h)
 * float width                   framebuffer width in native pixels
 * float height                  framebuffer height in native pixels
 * float scale                   logical-point to native-pixel scale
 * Rect scissor                  visible native-pixel rectangle
 * FUNCTION REGISTRY
 * Constructors: Viewport Viewport_0(void)
 * Setters: void Viewport_resize(Viewport *v, float framebufferWidthPx,
 *   float framebufferHeightPx, float backingScale)
 * Projections: Viewport_x/y/w/h(const Viewport *v, float pointValue)
 *   Rect Viewport_rect(const Viewport *v, float xPoints, float yPoints,
 *   float wPoints, float hPoints)
 *   void Viewport_toPoints(const Viewport *v, float px, float py,
 *   float *outX, float *outY)
 *   void Viewport_logicalSize(const Viewport *v, float *outW, float *outH)
 *   bool Viewport_isEmpty(const Viewport *v)
 * Private helper: s returns the positive backing scale, defaulting to 1.
 * Null/default contract: zero construction uses scale 1; resize ignores null
 * and sanitizes nonpositive scale to 1. Optional output pointers are independent.
 * ======================================================================== */

// Returns a zero-sized viewport with unit backing scale.
Viewport Viewport_0(void) {
    Viewport v = {0.0f, 0.0f, 1.0f, {0, 0, 0, 0}};
    return v;
}

// Updates native-pixel extent, sanitizes the scale to a positive value, and resets the scissor.
void Viewport_resize(Viewport *v, float framebufferWidthPx, float framebufferHeightPx,
                     float backingScale) {
    if (!v) return;
    (*v).width = framebufferWidthPx;
    (*v).height = framebufferHeightPx;
    (*v).scale = backingScale > 0.0f ? backingScale : 1.0f;
    (*v).scissor = (Rect){0, 0, framebufferWidthPx, framebufferHeightPx};
}

// Uses the configured positive backing scale, defaulting to one for absent or invalid viewports.
static float s(const Viewport *v) { return (v && (*v).scale > 0.0f) ? (*v).scale : 1.0f; }

// Converts a logical x coordinate to native pixels.
float Viewport_x(const Viewport *v, float pointX) { return pointX * s(v); }
// Converts a logical y coordinate to native pixels.
float Viewport_y(const Viewport *v, float pointY) { return pointY * s(v); }
// Converts a logical width to native pixels.
float Viewport_w(const Viewport *v, float pointW) { return pointW * s(v); }
// Converts a logical height to native pixels.
float Viewport_h(const Viewport *v, float pointH) { return pointH * s(v); }

// Scales all rectangle components from logical points to native pixels.
Rect Viewport_rect(const Viewport *v, float xPoints, float yPoints, float wPoints, float hPoints) {
    return (Rect){xPoints * s(v), yPoints * s(v), wPoints * s(v), hPoints * s(v)};
}

// Converts native-pixel coordinates to logical points, writing only non-null outputs.
ViewportPoint Viewport_toPoints(const Viewport *v, float px, float py) {
    float sc = s(v);
    return (ViewportPoint) { px / sc, py / sc };
}

// Converts the stored native-pixel extent to logical dimensions for supplied outputs.
ViewportSize Viewport_logicalSize(const Viewport *v) {
    float sc = s(v);
    return (ViewportSize) { (v ? (*v).width : 0.0f) / sc, (v ? (*v).height : 0.0f) / sc };
}

// Reports true for a null viewport or an extent with a non-positive dimension.
bool Viewport_isEmpty(const Viewport *v) { return !v || (*v).width <= 0.0f || (*v).height <= 0.0f; }
