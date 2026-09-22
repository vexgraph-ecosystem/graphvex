#include "lang/panel.h"
#include "lang/str.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Panel
 * ============================================================================
 * The element Panel — a Component (identity + tree + graphics + children)
 * wrapped with a forwarding surface so a call site reads Panel_setCornerRadius
 * instead of reaching through the component. Every forwarding verb targets the
 * panel's PRIMARY GraphicsComponent (graphics[0]).
 *
 * This is the AbsoluteLayout section: a Panel places its children with the
 * origin/anchor/pivot dials. Flex/scroll/list panels are other layouts (other
 * element types), added later.
 *
 * Arity surface (the Arity and Constructive Convenience Law): Panel_0/1/2 + the
 * Panel(...) chooser, Panel_zero(), and Panel_add() as the additive verb.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Panel (panel/panel.c)
 * LEVEL: L2 — Behavior (the element Panel: a Component + forwarding surface)
 * ============================================================================
 * SUMMARY:
 *   Wraps one embedded Component (with a primary GraphicsComponent); every
 *   forwarding setter/getter targets graphics[0]. Panel_add adds any element.
 *
 * STRUCT FIELDS (Mirroring lang/panel.h):
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
 *   - Panel_0/1/2 + the Panel(...) chooser / Panel_zero / Panel_free
 *
 * Public Core Functions: (.h)
 *   - Panel_component(panel) / Panel_add(panel, child)
 *
 * Public Setters: (.h)
 *   - Panel_setColor/Border/CornerRadius/Size/Location/Anchor/Pivot/Origin/Opacity/Visible/Z
 *
 * Public Getters: (.h)
 *   - Panel_getColor/CornerRadius/AbsX/AbsY/AbsW/AbsH / Panel_isVisible / Panel_isValid
 * ============================================================================
 */

// The panel's primary graphics part (its own placement + style).
static GraphicsComponent *primary(Panel *panel) {
    return Component_graphics(&(*panel).component, 0);
}

// CONSTRUCTORS (PUBLIC & PRIVATE)

Panel *Panel_0(void) {
    Panel *panel = (Panel*) calloc(1, sizeof(Panel));
    if (panel == nullptr)
        return nullptr;
    Component_init(&(*panel).component);
    GraphicsComponent gc;
    GraphicsComponent_init(&gc);
    Component_addGraphics(&(*panel).component, &gc);
    return panel;
}

Panel *Panel_1(uint32_t color) {
    Panel *panel = Panel_0();
    if (panel != nullptr)
        Panel_setColor(panel, color);
    return panel;
}

Panel *Panel_2(const char *name, uint32_t color) {
    Panel *panel = Panel_1(color);
    if (panel != nullptr)
        Component_setName(&(*panel).component, name);
    return panel;
}

Panel *Panel_zero(void) {
    return Panel_0();
}

void Panel_free(Panel *panel) {
    if (panel == nullptr)
        return;
    Component_destroy(&(*panel).component);
    free(panel);
}

Component *Panel_component(Panel *panel) {
    return panel ? &(*panel).component : nullptr;
}

bool Panel_add(Panel *panel, Component *child) {
    if (panel == nullptr)
        return false;
    return Element_add(&(*panel).component, child);
}

// SETTERS (PUBLIC & PRIVATE) — forwarded to the primary GraphicsComponent.

;;SETTER
void Panel_setColor(Panel *panel, uint32_t color) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        (*gc).backgroundColor = color;
}

;;SETTER
void Panel_setBorder(Panel *panel, uint32_t color, float width) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc == nullptr)
        return;
    (*gc).borderColor = color;
    (*gc).borderWidth = width < 0.0f ? 0.0f : width;
}

;;SETTER
void Panel_setCornerRadius(Panel *panel, float cornerRadius) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setCornerRadius(gc, cornerRadius);
}

;;SETTER
void Panel_setSize(Panel *panel, float w, float h) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setSize(gc, w, h);
}

;;SETTER
void Panel_setLocation(Panel *panel, float x, float y) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setLocation(gc, x, y);
}

;;SETTER
void Panel_setAnchor(Panel *panel, int anchor) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setAnchor(gc, anchor);
}

;;SETTER
void Panel_setPivot(Panel *panel, int pivot) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setPivot(gc, pivot);
}

;;SETTER
void Panel_setOrigin(Panel *panel, int origin) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setOrigin(gc, origin);
}

;;SETTER
void Panel_setOpacity(Panel *panel, float opacity) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setOpacity(gc, opacity);
}

;;SETTER
void Panel_setVisible(Panel *panel, bool visible) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setVisible(gc, visible);
}

;;SETTER
void Panel_setZ(Panel *panel, int z) {
    if (panel == nullptr)
        return;
    GraphicsComponent *gc = primary(panel);
    if (gc != nullptr)
        GraphicsComponent_setZ(gc, z);
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
uint32_t Panel_getColor(const Panel *panel) {
    if (panel == nullptr)
        return GRAPHICS_COMPONENT_COLOR_CLEAR;
    GraphicsComponent *gc = Component_graphics((Component*) &(*panel).component, 0);
    return gc ? (*gc).backgroundColor : GRAPHICS_COMPONENT_COLOR_CLEAR;
}

;;GETTER
float Panel_getCornerRadius(const Panel *panel) {
    if (panel == nullptr)
        return 0.0f;
    GraphicsComponent *gc = Component_graphics((Component*) &(*panel).component, 0);
    return gc ? (*gc).cornerRadius : 0.0f;
}

;;GETTER
float Panel_getAbsX(const Panel *panel) {
    if (panel == nullptr)
        return 0.0f;
    GraphicsComponent *gc = Component_graphics((Component*) &(*panel).component, 0);
    return gc ? (*gc).absX : 0.0f;
}

;;GETTER
float Panel_getAbsY(const Panel *panel) {
    if (panel == nullptr)
        return 0.0f;
    GraphicsComponent *gc = Component_graphics((Component*) &(*panel).component, 0);
    return gc ? (*gc).absY : 0.0f;
}

;;GETTER
float Panel_getAbsW(const Panel *panel) {
    if (panel == nullptr)
        return 0.0f;
    GraphicsComponent *gc = Component_graphics((Component*) &(*panel).component, 0);
    return gc ? (*gc).absW : 0.0f;
}

;;GETTER
float Panel_getAbsH(const Panel *panel) {
    if (panel == nullptr)
        return 0.0f;
    GraphicsComponent *gc = Component_graphics((Component*) &(*panel).component, 0);
    return gc ? (*gc).absH : 0.0f;
}

;;GETTER
bool Panel_isVisible(const Panel *panel) {
    if (panel == nullptr)
        return false;
    GraphicsComponent *gc = Component_graphics((Component*) &(*panel).component, 0);
    return gc ? GraphicsComponent_isVisible(gc) : false;
}

;;GETTER
bool Panel_isValid(const Panel *panel) {
    return panel != nullptr;
}

// --- toString Law (bounded, cold-path) ---

void Panel_toString(const Panel *self, char *dest, size_t cap, bool *outTruncated) {
    Str s;
    Str_init(&s, dest, cap);
    if (self == nullptr) {
        Str_put(&s, "nullptr");
        if (outTruncated) *outTruncated = false;
        return;
    }
    Str_printf(&s, "Panel(\"%s\", 0x%08X)", (*self).component.name, Panel_getColor(self));
    if (outTruncated) *outTruncated = Str_isTruncated(&s);
}

void Panel_toStringStruct(const Panel *self, char *dest, size_t cap, bool *outTruncated) {
    // A Panel IS its Component: the structural dump is the component's.
    Component_toStringStruct(self ? &(*self).component : nullptr, dest, cap, outTruncated);
}
