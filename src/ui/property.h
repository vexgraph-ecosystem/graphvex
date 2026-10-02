#ifndef GRAPHICS_UI_PROPERTY_H
#define GRAPHICS_UI_PROPERTY_H

#include "graphics/graphics.h"   // Color

// graphvex R3 — ui/property.h
//
// THE BOUND. Every element carries a Property: the rectangle (x,y,w,h) plus its
// shape and paint (radius, background, border, shadow, blur). It is placement
// data, pooled in nio/ and shared BY POINTER — one record, many referrers, so a
// bind is just aliasing the address and `revalidate` reflects it everywhere.
//
// radius > 0 is a clip: children are masked to the rounded shape (the corner
// radius actually means what it says).
typedef struct Property {
    float x, y, w, h;
    float radius;
    Color background, border;
    float borderWidth;
    Color shadow;
    float shadowX, shadowY, shadowBlur;
    float blur;
} Property;

Property Property_default(void);

#endif // GRAPHICS_UI_PROPERTY_H
