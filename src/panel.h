#ifndef GRAPHICS_PANEL_H
#define GRAPHICS_PANEL_H

#include <stdbool.h>
#include <stdint.h>

#include "graphics/graphics.h"

// graphvex R3 — panel.h
//
// THE UI COMPONENT. Everything a user sees is a Panel: a rectangle with a corner
// radius, a background, an optional border, and an optional shadow. "Different
// types of panels" are just different PanelDesc values (radius, border, shadow,
// colours) — no subclass zoo.
//
// Placement is pure geometry, fully testable headless:
//   * ANCHOR is the point on the PARENT the panel attaches to   (9 parts)
//   * PIVOT  is the point on the PANEL that lands on the anchor (9 parts)
//   * offset (x,y) is added to that anchor point
//
//   resolve:  pivot(panel) is placed at  anchor(parent) + offset
//
// So anchor=TOP_LEFT, pivot=TOP_LEFT puts the panel's top-left at the parent's
// top-left; anchor=CENTER, pivot=CENTER centres it; anchor=BOTTOM_RIGHT,
// pivot=TOP_LEFT hangs it off the parent's bottom-right corner.

// The 9 parts of a rectangle, row-major.
enum {
    PART_TOP_LEFT = 0, PART_TOP_CENTER, PART_TOP_RIGHT,
    PART_MIDDLE_LEFT, PART_CENTER, PART_MIDDLE_RIGHT,
    PART_BOTTOM_LEFT, PART_BOTTOM_CENTER, PART_BOTTOM_RIGHT,
    PART_COUNT
};

typedef struct Point { float x, y; } Point;

// The point `part` names, in the rect's own space (null-safe; clamps).
Point Part_point(Rect rect, int part);

typedef struct Panel Panel;

typedef struct PanelDesc {
    float width, height;      // native px
    float offsetX, offsetY;   // added to the anchor point
    int   anchor;             // PART_* on the PARENT (default TOP_LEFT)
    int   pivot;              // PART_* on the PANEL  (default TOP_LEFT)
    float radius;             // corner radius
    Color background;
    Color border;
    float borderWidth;
    Color shadow;             // alpha 0 = no shadow
    float shadowX, shadowY, shadowSpread;
} PanelDesc;

Panel *Panel_0(void);
Panel *Panel_new(const PanelDesc *desc);
void   Panel_destroy(Panel *panel);

// setters (return the panel for chaining)
Panel *Panel_setSize(Panel *p, float width, float height);
Panel *Panel_setOffset(Panel *p, float x, float y);
Panel *Panel_setAnchor(Panel *p, int anchor);
Panel *Panel_setPivot(Panel *p, int pivot);
Panel *Panel_setRadius(Panel *p, float radius);
Panel *Panel_setBackground(Panel *p, Color color);
Panel *Panel_setBorder(Panel *p, Color color, float width);
Panel *Panel_setShadow(Panel *p, Color color, float dx, float dy, float spread);

int   Panel_anchor(const Panel *p);
int   Panel_pivot(const Panel *p);
float Panel_width(const Panel *p);
float Panel_height(const Panel *p);
float Panel_radius(const Panel *p);
bool  Panel_isValid(const Panel *p);

// Resolve this panel's absolute rect against its parent's absolute rect.
Rect  Panel_resolve(const Panel *p, Rect parent);

// Append this panel's paint (shadow, then background+border) to a display list.
void  Panel_paint(const Panel *p, Rect absolute, DisplayList *dl);

#endif // GRAPHICS_PANEL_H
