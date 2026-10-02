#ifndef GRAPHICS_ELEMENT_H
#define GRAPHICS_ELEMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "graphics/graphics.h"
#include "ui/property.h"

// graphvex R3 — element.h
//
// THE UI NODE. Everything visible is an Element: a Frame's content, a Panel, a
// container. Placement is the anchor/pivot model; the rectangle + style live in
// a `Property` the Element carries as a POINTER (pooled in nio/, stable, and
// shareable — two Elements may point at one bound, so a write through either is
// seen by both).
//
//   Element *e = Element(&(ElementDesc){ .width = 200, .height = 80 });
//   Element_add(parent, e);
//
// Revalidation is explicit: a change marks a node dirty (walking up), and
// Element_revalidate(root) reflects only the dirty branches. A parent with
// radius > 0 clips its children to the rounded shape.

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

typedef struct Element Element;

typedef struct ElementDesc {
    float width, height;      // native px
    float offsetX, offsetY;   // added to the anchor point
    int   anchor;             // PART_* on the PARENT (default TOP_LEFT)
    int   pivot;              // PART_* on the ELEMENT (default TOP_LEFT)
    float radius;             // corner radius (also clips children when > 0)
    Color background;
    Color border;
    float borderWidth;
    Color shadow;             // alpha 0 = no shadow
    float shadowX, shadowY;   // shadow offset (px)
    float shadowBlur;         // shadow softness (px; 0 = hard)
    float blur;               // the element's OWN soft edge (px; 0 = sharp)
    const char *tag;          // optional name for Element_find
} ElementDesc;

Element *Element_0(void);
Element *Element_1(const ElementDesc *desc);
// Public construction is `Element(...)` — the arity chooser picks _0/_1 by
// argument count. The `_N` spellings are implementation, not call sites.
#define ELEMENT_CHOOSER(_0, _1, NAME, ...) NAME
#define Element(...) ELEMENT_CHOOSER(dummy __VA_OPT__(,) __VA_ARGS__, Element_1, Element_0)(__VA_ARGS__)

void Element_destroy(Element *element);      // frees the subtree

// ── the bound (a pooled, shareable Property) ────────────────────────────────
Property *Element_property(const Element *element);         // borrowed view
Element  *Element_setProperty(Element *element, Property *property);  // bind: borrow (no ownership)
Element  *Element_ownProperty(Element *element);            // re-own: allocate a private copy

// ── revalidation (dirty subtree) ────────────────────────────────────────────
void Element_markDirty(Element *element);    // this node + its ancestors
void Element_revalidate(Element *root);      // reflect the dirty branches; prunes clean ones
bool Element_isDirty(const Element *element);

// ── tree ────────────────────────────────────────────────────────────────────
Element *Element_add(Element *parent, Element *child);
Element *Element_addAt(Element *parent, Element *child, int index);
bool Element_remove(Element *child);
int Element_count(const Element *element);
Element *Element_child(const Element *element, int index);
Element *Element_parent(const Element *element);
Element *Element_root(Element *element);

// ── find / hit ──────────────────────────────────────────────────────────────
Element *Element_find(Element *root, const char *tag);
Element *Element_hit(Element *root, float x, float y);   // top-most, deepest

// ── geometry ────────────────────────────────────────────────────────────────
Rect Element_resolve(const Element *element, Rect parent);
Element *Element_setSize(Element *element, float width, float height);
Element *Element_setOffset(Element *element, float x, float y);
Element *Element_setAnchor(Element *element, int anchor);
Element *Element_setPivot(Element *element, int pivot);
Element *Element_setTag(Element *element, const char *tag);

// ── visual ──────────────────────────────────────────────────────────────────
Element *Element_setRadius(Element *element, float radius);
Element *Element_setBackground(Element *element, Color color);
Element *Element_setBorder(Element *element, Color color, float width);
// A shadow is just offset + blur (a real soft shadow, not a faux spread slab).
Element *Element_setShadow(Element *element, float offsetX, float offsetY, float blur);
Element *Element_setShadowColor(Element *element, Color color);
// Blur the element ITSELF (soft edges). The layout/hit rect is unchanged.
Element *Element_setBlur(Element *element, float blur);
Element *Element_setClip(Element *element, bool clip);   // clip children to the bound
Element *Element_setPressed(Element *element, bool pressed);
Element *Element_setVisible(Element *element, bool visible);

// ── boxes ───────────────────────────────────────────────────────────────────
// Element_resolve = the LAYOUT/HIT rect (what placement, clicks and anchoring
// use) — untouched by shadows/blur. Element_bounds = the PAINT bounds (the
// layout rect grown to fit the filters); the reference location never moves.
Rect Element_bounds(const Element *element, Rect parent);

// ── queries ─────────────────────────────────────────────────────────────────
float Element_width(const Element *element);
float Element_height(const Element *element);
float Element_radius(const Element *element);
int   Element_anchor(const Element *element);
int   Element_pivot(const Element *element);
bool  Element_isValid(const Element *element);
bool  Element_isVisible(const Element *element);
bool  Element_isPressed(const Element *element);
const char *Element_tag(const Element *element);

// Paint this element at an ABSOLUTE rect, then its children (resolved against
// that rect). A parent with radius > 0 clips its children to the rounded shape.
// A fully transparent element paints nothing but still walks.
void Element_paint(const Element *element, Rect absolute, DisplayList *dl);

#endif // GRAPHICS_ELEMENT_H
