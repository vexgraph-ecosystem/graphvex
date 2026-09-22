#ifndef LANG_PANEL_H
#define LANG_PANEL_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/component.h"

// lang/panel.h — the element Panel (a Component with a forwarding surface).
//
// A Panel is a Component (its identity + tree + graphics + children) wrapped so
// the call site reads Panel_setCornerRadius(p, 30) instead of reaching through
// Component_graphics(&p.component, 0). Every forwarding setter/getter is a thin
// wrapper over the panel's PRIMARY GraphicsComponent (graphics[0]).
//
// The AbsoluteLayout section of the suite: a Panel places its children with the
// origin/anchor/pivot dials (the absolute layout); flex/scroll/list panels are
// other layouts (other types), added later.
//
// Arity surface (the Arity and Constructive Convenience Law):
//   Panel()            -> Panel_0()   — empty panel
//   Panel(color)       -> Panel_1()   — colored
//   Panel(name, color) -> Panel_2()   — named + colored
//   Panel_zero()       -> the empty panel
//   Panel_add(p, child)-> the additive verb (forwards to Element_add)

typedef struct Panel {
    Component component;   // the element base (identity + tree + graphics + children)
} Panel;

// --- Constructors (arity) ---
Panel *Panel_0(void);
Panel *Panel_1(uint32_t color);
Panel *Panel_2(const char *name, uint32_t color);

#define PANEL_CHOOSER(_0, _1, _2, NAME, ...) NAME
#define Panel(...) PANEL_CHOOSER( \
    dummy __VA_OPT__(,) __VA_ARGS__, \
    Panel_2, Panel_1, Panel_0 \
)(__VA_ARGS__)

Panel *Panel_zero(void);
void Panel_free(Panel *panel);

// The panel's element base (the Component) — the universal handle.
Component *Panel_component(Panel *panel);

// The additive verb: adds any element to this panel (the ONE mechanism).
bool Panel_add(Panel *panel, Component *child);

// --- Forwarding setters (to the panel's primary GraphicsComponent) ---
void Panel_setColor(Panel *panel, uint32_t color);
void Panel_setBorder(Panel *panel, uint32_t color, float width);
void Panel_setCornerRadius(Panel *panel, float cornerRadius);
void Panel_setSize(Panel *panel, float w, float h);
void Panel_setLocation(Panel *panel, float x, float y);
void Panel_setAnchor(Panel *panel, int anchor);
void Panel_setPivot(Panel *panel, int pivot);
void Panel_setOrigin(Panel *panel, int origin);
void Panel_setOpacity(Panel *panel, float opacity);
void Panel_setVisible(Panel *panel, bool visible);
void Panel_setZ(Panel *panel, int z);

// --- Forwarding getters (the Symmetric Getter/Setter Completeness Law) ---
uint32_t Panel_getColor(const Panel *panel);
float Panel_getCornerRadius(const Panel *panel);
float Panel_getAbsX(const Panel *panel);
float Panel_getAbsY(const Panel *panel);
float Panel_getAbsW(const Panel *panel);
float Panel_getAbsH(const Panel *panel);
bool Panel_isVisible(const Panel *panel);
bool Panel_isValid(const Panel *panel);

// --- toString Law (bounded, cold-path) ---
void Panel_toString(const Panel *self, char *dest, size_t cap, bool *outTruncated);
void Panel_toStringStruct(const Panel *self, char *dest, size_t cap, bool *outTruncated);

#endif // LANG_PANEL_H
