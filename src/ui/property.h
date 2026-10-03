#ifndef GRAPHICS_UI_PROPERTY_H
#define GRAPHICS_UI_PROPERTY_H

#include <stdbool.h>

#include "graphics/graphics.h"   // Color

// graphvex R3 — ui/property.h
//
// THE BOUND. Every element carries a Property: the rectangle (x,y,w,h) plus its
// shape and paint (radius, background, border, shadow, blur). It is placement
// data, pooled in nio/ and shared BY POINTER — one record, many referrers, so a
// bind is just aliasing the address and `revalidate` reflects it everywhere.
//
// Size is MODULATED by the bound's own min/max: Property_width/Height return the
// stored w/h clamped to [minW, maxW], and every layout read goes through them.
// So a min/max set after a size still takes effect, and two elements aliasing
// one bound clamp identically. maxW/maxH <= 0 means "no ceiling".
//
// radius > 0 is a clip: children are masked to the rounded shape (the corner
// radius actually means what it says).
typedef struct Property {
    float x, y, w, h;
    float minW, minH;      // size floor (0 = none)
    float maxW, maxH;      // size ceiling (<= 0 = unbounded)
    float radius;
    bool clip;             // clip children to this rect (a scroll viewport)
    Color background, border;
    float borderWidth;
    Color shadow;
    float shadowX, shadowY, shadowBlur;
    float blur;
} Property;

Property Property_default(void);

// Lower/upper size clamps. Setting max <= 0 clears the ceiling.
void Property_setMinSize(Property *property, float width, float height);
void Property_setMaxSize(Property *property, float width, float height);

// The EFFECTIVE size: stored w/h clamped to the min/max above. Every layout,
// hit-test and query read uses these, never the raw fields.
float Property_width(const Property *property);
float Property_height(const Property *property);

#endif // GRAPHICS_UI_PROPERTY_H
