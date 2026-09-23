#ifndef LANG_PANEL_H
#define LANG_PANEL_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/component.h"

// lang/graphics_panel.h — the element GraphicsPanel (a Component with a forwarding surface).
//
// A GraphicsPanel is a Component (its identity + tree + graphics + children) wrapped so
// the call site reads GraphicsPanel_setCornerRadius(p, 30) instead of reaching through
// Component_graphics(&p.component, 0). Every forwarding setter/getter is a thin
// wrapper over the panel's PRIMARY GraphicsComponent (graphics[0]).
//
// The AbsoluteLayout section of the suite: a GraphicsPanel places its children with the
// origin/anchor/pivot dials (the absolute layout); flex/scroll/list panels are
// other layouts (other types), added later.
//
// Arity surface (the Arity and Constructive Convenience Law):
//   GraphicsPanel()            -> GraphicsPanel_0()   — empty panel
//   GraphicsPanel(color)       -> GraphicsPanel_1()   — colored
//   GraphicsPanel(name, color) -> GraphicsPanel_2()   — named + colored
//   GraphicsPanel_zero()       -> the empty panel
//   GraphicsPanel_add(p, child)-> the additive verb (forwards to Element_add)

typedef struct GraphicsPanel {
    Component component;   // the element base (identity + tree + graphics + children)
} GraphicsPanel;

// --- Constructors (arity) ---
GraphicsPanel *GraphicsPanel_0(void);
GraphicsPanel *GraphicsPanel_1(uint32_t color);
GraphicsPanel *GraphicsPanel_2(const char *name, uint32_t color);

#define GRAPHICS_PANEL_CHOOSER(_0, _1, _2, NAME, ...) NAME
#define GraphicsPanel(...) GRAPHICS_PANEL_CHOOSER( \
    dummy __VA_OPT__(,) __VA_ARGS__, \
    GraphicsPanel_2, GraphicsPanel_1, GraphicsPanel_0 \
)(__VA_ARGS__)

GraphicsPanel *GraphicsPanel_zero(void);
void GraphicsPanel_free(GraphicsPanel *panel);

// The panel's element base (the Component) — the universal handle.
Component *GraphicsPanel_component(GraphicsPanel *panel);

// The additive verb: adds any element to this panel (the ONE mechanism).
bool GraphicsPanel_add(GraphicsPanel *panel, Component *child);

// --- Forwarding setters (to the panel's primary GraphicsComponent) ---
void GraphicsPanel_setColor(GraphicsPanel *panel, uint32_t color);
void GraphicsPanel_setBorder(GraphicsPanel *panel, uint32_t color, float width);
void GraphicsPanel_setCornerRadius(GraphicsPanel *panel, float cornerRadius);
void GraphicsPanel_setSize(GraphicsPanel *panel, float w, float h);
void GraphicsPanel_setLocation(GraphicsPanel *panel, float x, float y);
void GraphicsPanel_setAnchor(GraphicsPanel *panel, int anchor);
void GraphicsPanel_setPivot(GraphicsPanel *panel, int pivot);
void GraphicsPanel_setOrigin(GraphicsPanel *panel, int origin);
void GraphicsPanel_setOpacity(GraphicsPanel *panel, float opacity);
void GraphicsPanel_setVisible(GraphicsPanel *panel, bool visible);
void GraphicsPanel_setZ(GraphicsPanel *panel, int z);

// --- Forwarding getters (the Symmetric Getter/Setter Completeness Law) ---
uint32_t GraphicsPanel_getColor(const GraphicsPanel *panel);
float GraphicsPanel_getCornerRadius(const GraphicsPanel *panel);
float GraphicsPanel_getAbsX(const GraphicsPanel *panel);
float GraphicsPanel_getAbsY(const GraphicsPanel *panel);
float GraphicsPanel_getAbsW(const GraphicsPanel *panel);
float GraphicsPanel_getAbsH(const GraphicsPanel *panel);
bool GraphicsPanel_isVisible(const GraphicsPanel *panel);
bool GraphicsPanel_isValid(const GraphicsPanel *panel);

// --- toString Law (bounded, cold-path) ---
void GraphicsPanel_toString(const GraphicsPanel *self, char *dest, size_t cap, bool *outTruncated);
void GraphicsPanel_toStringStruct(const GraphicsPanel *self, char *dest, size_t cap, bool *outTruncated);

#endif // LANG_PANEL_H
