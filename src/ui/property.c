#include "ui/property.h"

#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Property (ui/property.c)
 * ============================================================================
 * A Property is the shared BOUND: inert placement + paint data, pooled in nio/
 * and aliased by pointer so several Elements can share one record. It owns
 * nothing and runs nothing; its one behavior is the size clamp — minW/minH and
 * maxW/maxH bound the stored w/h, and Property_width/Height expose the clamped
 * result. Because the clamp lives on the bound and not on the element, a
 * ceiling set after a size still takes effect and every aliasing element clamps
 * identically. max <= 0 means "no ceiling"; a default bound matches the element
 * defaults (opaque white body, no border/shadow, no clamps).
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Property (ui/property.c)
 * ============================================================================
 * Inert, pool-allocated bound record. No owned state, no allocation here.
 *
 * STRUCT FIELDS (mirrors ui/property.h):
 * ----------------------------------------------------------------------------
 *   float x, y, w, h;      // placement rect (native px)
 *   float minW, minH;      // size floor (0 = none)
 *   float maxW, maxH;      // size ceiling (<= 0 = unbounded)
 *   float radius;          // corner radius (also clips children when > 0)
 *   bool  clip;            // clip children to this rect
 *   Color background, border; float borderWidth;
 *   Color shadow; float shadowX, shadowY, shadowBlur; float blur;
 *
 * FUNCTION REGISTRY (exported by ui/property.h):
 * ----------------------------------------------------------------------------
 * Core:
 *   - Property_default
 * Size clamp:
 *   - Property_setMinSize, Property_setMaxSize
 * Getters:
 *   - Property_width, Property_height
 * ============================================================================
 */

// Returns a zeroed bound with an opaque white background and no size clamps.
Property Property_default(void) {
    Property p = {0};
    p.background = COLOR_WHITE;
    return p;
}

// Clamp the stored extent into [min, max]; a non-positive max is no ceiling.
// Applies the minimum and an optional positive maximum to one stored dimension.
static float property_clamp(float value, float min, float max) {
    if (max > 0.0f && value > max) value = max;
    if (value < min) value = min;
    return value;
}

// Sets nonnegative minimum dimensions; zero disables each minimum.
void Property_setMinSize(Property *property, float width, float height) {
    if (!property) return;
    (*property).minW = width > 0.0f ? width : 0.0f;
    (*property).minH = height > 0.0f ? height : 0.0f;
}

// Sets nonnegative maximum dimensions; zero means no upper bound.
void Property_setMaxSize(Property *property, float width, float height) {
    if (!property) return;
    (*property).maxW = width > 0.0f ? width : 0.0f;
    (*property).maxH = height > 0.0f ? height : 0.0f;
}

// Returns the stored width clamped to configured limits, or zero for null.
float Property_width(const Property *property) {
    return property ? property_clamp((*property).w, (*property).minW, (*property).maxW) : 0.0f;
}

// Returns the stored height clamped to configured limits, or zero for null.
float Property_height(const Property *property) {
    return property ? property_clamp((*property).h, (*property).minH, (*property).maxH) : 0.0f;
}
