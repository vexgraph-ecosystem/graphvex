#include "lang/graphics_panel.h"
#include "lang/str.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: GraphicsPanel
 * ============================================================================
 * The element GraphicsPanel — a Component (identity + tree + graphics + children)
 * wrapped with a forwarding surface so a call site reads GraphicsPanel_setCornerRadius
 * instead of reaching through the component. Every forwarding verb targets the
 * panel's PRIMARY GraphicsComponent (graphics[0]).
 *
 * This is the AbsoluteLayout section: a GraphicsPanel places its children with the
 * origin/anchor/pivot dials. Flex/scroll/list panels are other layouts (other
 * element types), added later.
 *
 * Arity surface (the Arity and Constructive Convenience Law): GraphicsPanel_0/1/2 + the
 * GraphicsPanel(...) chooser, GraphicsPanel_zero(), and GraphicsPanel_add() as the additive verb.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: GraphicsPanel (ui/panel/graphics_panel.c)
 * LEVEL: L2 — Behavior (the element GraphicsPanel: a Component + forwarding surface)
 * ============================================================================
 * SUMMARY:
 *   Wraps one embedded Component (with a primary GraphicsComponent); every
 *   forwarding setter/getter targets graphics[0]. GraphicsPanel_add adds any element.
 *
 * STRUCT FIELDS (Mirroring lang/graphics_panel.h):
 * ----------------------------------------------------------------------------
 *   Component component;   // the element base (identity + tree + graphics + children)
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   primary(panel) : the panel's primary GraphicsComponent (graphics[0])
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - GraphicsPanel_0/1/2 + the GraphicsPanel(...) chooser / GraphicsPanel_zero / GraphicsPanel_free
 *
 * Public Core Functions: (.h)
 *   - GraphicsPanel_component(panel) / GraphicsPanel_add(panel, child)
 *
 * Public Setters: (.h)
 *   - GraphicsPanel_setColor/Border/CornerRadius/Size/Location/Anchor/Pivot/Origin/Opacity/Visible/Z
 *
 * Public Getters: (.h)
 *   - GraphicsPanel_getColor/CornerRadius/AbsX/AbsY/AbsW/AbsH / GraphicsPanel_isVisible / GraphicsPanel_isValid
 * ============================================================================
 */

// The panel's primary graphics part (its own placement + style).
static GraphicsComponent *primary(GraphicsPanel *panel) {
    return Component_graphics(&(*panel).component, 0);
}

// CONSTRUCTORS (PUBLIC & PRIVATE)

GraphicsPanel *GraphicsPanel_0(void) {
    GraphicsPanel *panel = (GraphicsPanel*) calloc(1, sizeof(GraphicsPanel));
    if (panel == nullptr)
        return nullptr;
    Component_init(&(*panel).component);
    GraphicsComponent gc;
    GraphicsComponent_init(&gc);
    Component_addGraphics(&(*panel).component, &gc);
    return panel;
}

GraphicsPanel *GraphicsPanel_1(uint32_t color) {
    GraphicsPanel *panel = GraphicsPanel_0();
    if (panel != nullptr)
        GraphicsPanel_setColor(panel, color);
    return panel;
}

GraphicsPanel *GraphicsPanel_2(const char *name, uint32_t color) {
    GraphicsPanel *panel = GraphicsPanel_1(color);
    if (panel != nullptr)
        Component_setName(&(*panel).component, name);
    return panel;
}

GraphicsPanel *GraphicsPanel_zero(void) {
    return GraphicsPanel_0();
}

void GraphicsPanel_free(GraphicsPanel *panel) {
    if (panel == nullptr)
        return;
    Component_destroy(&(*panel).component);
    free(panel);
}

Component *GraphicsPanel_component(GraphicsPanel *panel) {
    return panel ? &(*panel).component : nullptr;
}

bool GraphicsPanel_add(GraphicsPanel *panel, Component *child) {
    if (panel == nullptr)
        return false;
    return Element_add(&(*panel).component, child);
}

// SETTERS (PUBLIC & PRIVATE) — forwarded to the primary GraphicsComponent.

;;SETTER
void GraphicsPanel_setColor(GraphicsPanel *panel, uint32_t color) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        (*gc).backgroundColor = color;
}

;;SETTER
void GraphicsPanel_setBorder(GraphicsPanel *panel, uint32_t color, float width) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc == nullptr)
        return;
    (*gc).borderColor = color;
    (*gc).borderWidth = width < 0.0f ? 0.0f : width;
}

;;SETTER
void GraphicsPanel_setCornerRadius(GraphicsPanel *panel, float cornerRadius) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setCornerRadius(gc, cornerRadius);
}

;;SETTER
void GraphicsPanel_setSize(GraphicsPanel *panel, float w, float h) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setSize(gc, w, h);
}

;;SETTER
void GraphicsPanel_setLocation(GraphicsPanel *panel, float x, float y) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setLocation(gc, x, y);
}

;;SETTER
void GraphicsPanel_setAnchor(GraphicsPanel *panel, int anchor) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setAnchor(gc, anchor);
}

;;SETTER
void GraphicsPanel_setPivot(GraphicsPanel *panel, int pivot) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setPivot(gc, pivot);
}

;;SETTER
void GraphicsPanel_setOrigin(GraphicsPanel *panel, int origin) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setOrigin(gc, origin);
}

;;SETTER
void GraphicsPanel_setOpacity(GraphicsPanel *panel, float opacity) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setOpacity(gc, opacity);
}

;;SETTER
void GraphicsPanel_setVisible(GraphicsPanel *panel, bool visible) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setVisible(gc, visible);
}

;;SETTER
void GraphicsPanel_setZ(GraphicsPanel *panel, int z) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setZ(gc, z);
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
uint32_t GraphicsPanel_getColor(const GraphicsPanel *panel) {
    if (panel == nullptr)
        return GRAPHICS_COMPONENT_COLOR_CLEAR;
    GraphicsComponent *gc = Component_graphics((Component*) &(*panel).component, 0);
    return gc ? (*gc).backgroundColor : GRAPHICS_COMPONENT_COLOR_CLEAR;
}

;;GETTER
float GraphicsPanel_getCornerRadius(const GraphicsPanel *panel) {
    if (panel == nullptr)
        return 0.0f;
    GraphicsComponent *gc = Component_graphics((Component*) &(*panel).component, 0);
    return gc ? (*gc).cornerRadius : 0.0f;
}

;;GETTER
float GraphicsPanel_getAbsX(const GraphicsPanel *panel) {
    if (panel == nullptr)
        return 0.0f;
    GraphicsComponent *gc = Component_graphics((Component*) &(*panel).component, 0);
    return gc ? (*gc).absX : 0.0f;
}

;;GETTER
float GraphicsPanel_getAbsY(const GraphicsPanel *panel) {
    if (panel == nullptr)
        return 0.0f;
    GraphicsComponent *gc = Component_graphics((Component*) &(*panel).component, 0);
    return gc ? (*gc).absY : 0.0f;
}

;;GETTER
float GraphicsPanel_getAbsW(const GraphicsPanel *panel) {
    if (panel == nullptr)
        return 0.0f;
    GraphicsComponent *gc = Component_graphics((Component*) &(*panel).component, 0);
    return gc ? (*gc).absW : 0.0f;
}

;;GETTER
float GraphicsPanel_getAbsH(const GraphicsPanel *panel) {
    if (panel == nullptr)
        return 0.0f;
    GraphicsComponent *gc = Component_graphics((Component*) &(*panel).component, 0);
    return gc ? (*gc).absH : 0.0f;
}

;;GETTER
bool GraphicsPanel_isVisible(const GraphicsPanel *panel) {
    if (panel == nullptr)
        return false;
    GraphicsComponent *gc = Component_graphics((Component*) &(*panel).component, 0);
    return gc ? GraphicsComponent_isVisible(gc) : false;
}

;;GETTER
bool GraphicsPanel_isValid(const GraphicsPanel *panel) {
    return panel != nullptr;
}

// --- toString Law (bounded, cold-path) ---

void GraphicsPanel_toString(const GraphicsPanel *self, char *dest, size_t cap, bool *outTruncated) {
    Str s;
    Str_init(&s, dest, cap);
    if (self == nullptr) {
        Str_put(&s, "nullptr");
        if (outTruncated) *outTruncated = false;
        return;
    }
    Str_printf(&s, "GraphicsPanel(\"%s\", 0x%08X)", (*self).component.name, GraphicsPanel_getColor(self));
    if (outTruncated) *outTruncated = Str_isTruncated(&s);
}

void GraphicsPanel_toStringStruct(const GraphicsPanel *self, char *dest, size_t cap, bool *outTruncated) {
    // A GraphicsPanel IS its Component: the structural dump is the component's.
    Component_toStringStruct(self ? &(*self).component : nullptr, dest, cap, outTruncated);
}
