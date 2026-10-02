#include "graphics/viewport.h"

// graphvex R3 — graphics/viewport.c
// Points <-> native pixels. Pure math; no virtual canvas, no scene modes.

Viewport Viewport_0(void) {
    Viewport v = {0.0f, 0.0f, 1.0f, {0, 0, 0, 0}};
    return v;
}

void Viewport_resize(Viewport *v, float framebufferWidthPx, float framebufferHeightPx,
                     float backingScale) {
    if (!v) return;
    (*v).width = framebufferWidthPx;
    (*v).height = framebufferHeightPx;
    (*v).scale = backingScale > 0.0f ? backingScale : 1.0f;
    (*v).scissor = (Rect){0, 0, framebufferWidthPx, framebufferHeightPx};
}

static float s(const Viewport *v) { return (v && (*v).scale > 0.0f) ? (*v).scale : 1.0f; }

float Viewport_x(const Viewport *v, float pointX) { return pointX * s(v); }
float Viewport_y(const Viewport *v, float pointY) { return pointY * s(v); }
float Viewport_w(const Viewport *v, float pointW) { return pointW * s(v); }
float Viewport_h(const Viewport *v, float pointH) { return pointH * s(v); }

Rect Viewport_rect(const Viewport *v, float xPoints, float yPoints, float wPoints, float hPoints) {
    return (Rect){xPoints * s(v), yPoints * s(v), wPoints * s(v), hPoints * s(v)};
}

void Viewport_toPoints(const Viewport *v, float px, float py, float *outX, float *outY) {
    float sc = s(v);
    if (outX) *outX = px / sc;
    if (outY) *outY = py / sc;
}

void Viewport_logicalSize(const Viewport *v, float *outW, float *outH) {
    float sc = s(v);
    if (outW) *outW = (v ? (*v).width : 0.0f) / sc;
    if (outH) *outH = (v ? (*v).height : 0.0f) / sc;
}

bool Viewport_isEmpty(const Viewport *v) { return !v || (*v).width <= 0.0f || (*v).height <= 0.0f; }
