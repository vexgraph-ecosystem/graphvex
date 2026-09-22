#include "lang/brush.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Brush
 * ============================================================================
 * The fill brush: a packed 0xRRGGBBAA color and a master alpha multiplier. It
 * is what every fill verb paints with — a plain-data value record, embed-first,
 * no GPU handles. Ported from the old reference (paint/brush.c) and reduced to
 * the language contract.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Brush (paint/brush.c)
 * LEVEL: L2 — Behavior (fill style value record)
 * ============================================================================
 * SUMMARY:
 *   Packed color + opacity, heap or embedded. Symmetric accessors.
 *
 * STRUCT FIELDS (Mirroring lang/brush.h):
 * ----------------------------------------------------------------------------
 *   uint32_t color;   // packed 0xRRGGBBAA (alpha low byte)
 *   float opacity;    // master alpha multiplier [0..1]
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Brush_0(void) / Brush_2(color, opacity)
 *
 * Public Core Functions: (.h)
 *   - Brush_free(brush)
 *
 * Public Setters: (.h)
 *   - Brush_setColor(brush, color) / Brush_setOpacity(brush, opacity)
 *
 * Public Getters: (.h)
 *   - Brush_getColor(brush) / Brush_getOpacity(brush)
 * ============================================================================
 */

Brush *Brush_0(void) {
    return Brush_2(0x000000FFu, 1.0f);
}

Brush *Brush_2(uint32_t color, float opacity) {
    Brush *brush = (Brush*) calloc(1, sizeof(Brush));
    if (brush == nullptr)
        return nullptr;
    (*brush).color = color;
    (*brush).opacity = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
    return brush;
}

void Brush_free(Brush *brush) {
    free(brush);
}

;;SETTER
void Brush_setColor(Brush *brush, uint32_t color) {
    if (brush == nullptr)
        return;
    (*brush).color = color;
}

;;SETTER
void Brush_setOpacity(Brush *brush, float opacity) {
    if (brush == nullptr)
        return;
    (*brush).opacity = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
}

;;GETTER
uint32_t Brush_getColor(const Brush *brush) {
    return brush ? (*brush).color : 0u;
}

;;GETTER
float Brush_getOpacity(const Brush *brush) {
    return brush ? (*brush).opacity : 0.0f;
}
