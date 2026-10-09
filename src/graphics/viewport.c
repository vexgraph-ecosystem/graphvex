#include "graphics/viewport.h"

// graphvex R3 — graphics/viewport.c
// Points <-> native pixels. Pure math; no virtual canvas, no scene modes.

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
void Viewport_toPoints(const Viewport *v, float px, float py, float *outX, float *outY) {
    float sc = s(v);
    if (outX) *outX = px / sc;
    if (outY) *outY = py / sc;
}

// Converts the stored native-pixel extent to logical dimensions for supplied outputs.
void Viewport_logicalSize(const Viewport *v, float *outW, float *outH) {
    float sc = s(v);
    if (outW) *outW = (v ? (*v).width : 0.0f) / sc;
    if (outH) *outH = (v ? (*v).height : 0.0f) / sc;
}

// Reports true for a null viewport or an extent with a non-positive dimension.
bool Viewport_isEmpty(const Viewport *v) { return !v || (*v).width <= 0.0f || (*v).height <= 0.0f; }
