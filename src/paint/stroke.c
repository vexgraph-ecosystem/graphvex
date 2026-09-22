#include "lang/stroke.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Stroke
 * ============================================================================
 * The outline stroke style: width, dash, line cap, line join, and a packed
 * 0xRRGGBBAA color. Plain-data value record, embed-first, no GPU handles.
 * Ported from the old reference (paint/stroke.c) and reduced to the language
 * contract.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Stroke (paint/stroke.c)
 * LEVEL: L2 — Behavior (outline style value record)
 * ============================================================================
 * SUMMARY:
 *   Width + dash + cap + join + packed color. Symmetric accessors.
 *
 * STRUCT FIELDS (Mirroring lang/stroke.h):
 * ----------------------------------------------------------------------------
 *   float width;      // stroke width in pixels (>= 0)
 *   float dash;       // dash segment length (0 = solid)
 *   uint32_t cap;     // STROKE_CAP_*
 *   uint32_t join;    // STROKE_JOIN_*
 *   uint32_t color;   // packed 0xRRGGBBAA
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Stroke_0(void) / Stroke_2(width, color)
 *
 * Public Core Functions: (.h)
 *   - Stroke_free(stroke)
 *
 * Public Setters: (.h)
 *   - Stroke_setWidth/Dash/Cap/Join/Color(stroke, ...)
 *
 * Public Getters: (.h)
 *   - Stroke_getWidth/Dash/Cap/Join/Color(stroke)
 * ============================================================================
 */

Stroke *Stroke_0(void) {
    return Stroke_2(1.0f, 0x000000FFu);
}

Stroke *Stroke_2(float width, uint32_t color) {
    Stroke *stroke = (Stroke*) calloc(1, sizeof(Stroke));
    if (stroke == nullptr)
        return nullptr;
    (*stroke).width = width < 0.0f ? 0.0f : width;
    (*stroke).dash = 0.0f;
    (*stroke).cap = STROKE_CAP_BUTT;
    (*stroke).join = STROKE_JOIN_MITER;
    (*stroke).color = color;
    return stroke;
}

void Stroke_free(Stroke *stroke) {
    free(stroke);
}

;;SETTER
void Stroke_setWidth(Stroke *stroke, float width) {
    if (stroke == nullptr)
        return;
    (*stroke).width = width < 0.0f ? 0.0f : width;
}

;;SETTER
void Stroke_setDash(Stroke *stroke, float dash) {
    if (stroke == nullptr)
        return;
    (*stroke).dash = dash < 0.0f ? 0.0f : dash;
}

;;SETTER
void Stroke_setCap(Stroke *stroke, uint32_t cap) {
    if (stroke == nullptr)
        return;
    (*stroke).cap = cap;
}

;;SETTER
void Stroke_setJoin(Stroke *stroke, uint32_t join) {
    if (stroke == nullptr)
        return;
    (*stroke).join = join;
}

;;SETTER
void Stroke_setColor(Stroke *stroke, uint32_t color) {
    if (stroke == nullptr)
        return;
    (*stroke).color = color;
}

;;GETTER
float Stroke_getWidth(const Stroke *stroke) {
    return stroke ? (*stroke).width : 0.0f;
}

;;GETTER
float Stroke_getDash(const Stroke *stroke) {
    return stroke ? (*stroke).dash : 0.0f;
}

;;GETTER
uint32_t Stroke_getCap(const Stroke *stroke) {
    return stroke ? (*stroke).cap : 0u;
}

;;GETTER
uint32_t Stroke_getJoin(const Stroke *stroke) {
    return stroke ? (*stroke).join : 0u;
}

;;GETTER
uint32_t Stroke_getColor(const Stroke *stroke) {
    return stroke ? (*stroke).color : 0u;
}
