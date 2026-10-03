#include "ui/element.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "nio/property_pool.h"

// graphvex R3 — element.c
// The one UI node. Its rectangle + style live in a pooled Property* (shareable);
// identity (offset/anchor/pivot/tag/state) and the tree stay on the Element.
// Pure geometry + display-list paint; revalidation is explicit and pruned.
// Hit traversal respects the same rectangular/rounded ancestor masks as paint.

struct Element {
    Property *property;      // owned (from the default pool) or borrowed
    bool ownsProperty;
    bool dirty;

    float offsetX, offsetY;
    int anchor, pivot;
    const char *tag;
    bool pressed;
    bool visible;

    struct Element *parent;
    struct Element **children;
    int count, cap;
};

// ── construction ────────────────────────────────────────────────────────────
Point Part_point(Rect r, int part) {
    if (part < 0 || part >= PART_COUNT) part = PART_TOP_LEFT;
    int col = part % 3;   // 0 left, 1 centre, 2 right
    int row = part / 3;   // 0 top,  1 middle, 2 bottom
    return (Point){r.x + (float)col * 0.5f * r.w, r.y + (float)row * 0.5f * r.h};
}

Element *Element_1(const ElementDesc *d) {
    Element *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    Property *p = PropertyPool_alloc(PropertyPool_default(), NULL);
    if (!p) { free(e); return NULL; }
    (*e).property = p;
    (*e).ownsProperty = true;
    (*p).w = d ? (*d).width : 0.0f;
    (*p).h = d ? (*d).height : 0.0f;
    (*p).radius = d ? (*d).radius : 0.0f;
    (*p).background = d ? (*d).background : COLOR_WHITE;
    (*p).border = d ? (*d).border : COLOR_CLEAR;
    (*p).borderWidth = d ? (*d).borderWidth : 0.0f;
    (*p).shadow = d ? (*d).shadow : COLOR_CLEAR;
    (*p).shadowX = d ? (*d).shadowX : 0.0f;
    (*p).shadowY = d ? (*d).shadowY : 0.0f;
    (*p).shadowBlur = d ? (*d).shadowBlur : 0.0f;
    (*p).blur = d ? (*d).blur : 0.0f;
    (*e).offsetX = d ? (*d).offsetX : 0.0f;
    (*e).offsetY = d ? (*d).offsetY : 0.0f;
    (*e).anchor = d ? (*d).anchor : PART_TOP_LEFT;
    (*e).pivot = d ? (*d).pivot : PART_TOP_LEFT;
    (*e).tag = d ? (*d).tag : NULL;
    (*e).visible = true;
    return e;
}

Element *Element_0(void) { return Element_1(NULL); }

void Element_destroy(Element *e) {
    if (!e) return;
    for (int i = 0; i < (*e).count; i++) Element_destroy((*e).children[i]);
    free((*e).children);
    if ((*e).ownsProperty && (*e).property)
        PropertyPool_release(PropertyPool_default(), (*e).property);
    free(e);
}

// ── the bound ───────────────────────────────────────────────────────────────
Property *Element_property(const Element *e) { return e ? (*e).property : NULL; }

Element *Element_setProperty(Element *e, Property *property) {
    if (!e) return e;
    if ((*e).ownsProperty && (*e).property)
        PropertyPool_release(PropertyPool_default(), (*e).property);
    (*e).property = property;
    (*e).ownsProperty = false;
    Element_markDirty(e);
    return e;
}

Element *Element_ownProperty(Element *e) {
    if (!e) return e;
    Property *copy = PropertyPool_alloc(PropertyPool_default(), (*e).property);
    if (!copy) return e;
    if ((*e).ownsProperty && (*e).property)
        PropertyPool_release(PropertyPool_default(), (*e).property);
    (*e).property = copy;
    (*e).ownsProperty = true;
    Element_markDirty(e);
    return e;
}

// ── revalidation ────────────────────────────────────────────────────────────
void Element_markDirty(Element *e) {
    for (Element *n = e; n; n = (*n).parent) {
        if ((*n).dirty) break;
        (*n).dirty = true;
    }
}

void Element_revalidate(Element *root) {
    if (!root || !(*root).dirty) return;    // prune clean subtrees
    for (int i = 0; i < (*root).count; i++) {
        Element *c = (*root).children[i];
        if ((*c).dirty) Element_revalidate(c);
    }
    (*root).dirty = false;
}

bool Element_isDirty(const Element *e) { return e && (*e).dirty; }

// ── tree ────────────────────────────────────────────────────────────────────
static void child_insert(Element *parent, Element *child, int index) {
    if ((*parent).count == (*parent).cap) {
        (*parent).cap = (*parent).cap ? (*parent).cap * 2 : 8;
        Element **grown = realloc((*parent).children, (size_t)((*parent).cap) * sizeof *grown);
        if (!grown) return;
        (*parent).children = grown;
    }
    if (index < 0 || index > (*parent).count) index = (*parent).count;
    memmove(&(*parent).children[index + 1], &(*parent).children[index],
            (size_t)((*parent).count - index) * sizeof *(*parent).children);
    (*parent).children[index] = child;
    (*parent).count++;
    (*child).parent = parent;
    Element_markDirty(parent);
}

Element *Element_add(Element *parent, Element *child) {
    if (!parent || !child) return child;
    child_insert(parent, child, (*parent).count);
    return child;
}

Element *Element_addAt(Element *parent, Element *child, int index) {
    if (!parent || !child) return child;
    child_insert(parent, child, index);
    return child;
}

bool Element_remove(Element *child) {
    if (!child || !(*child).parent) return false;
    Element *p = (*child).parent;
    for (int i = 0; i < (*p).count; i++) {
        if ((*p).children[i] != child) continue;
        memmove(&(*p).children[i], &(*p).children[i + 1], (size_t)((*p).count - i - 1) * sizeof *(*p).children);
        (*p).count--;
        (*child).parent = NULL;
        Element_markDirty(p);
        return true;
    }
    return false;
}

int Element_count(const Element *e) { return e ? (*e).count : 0; }

Element *Element_child(const Element *e, int index) {
    if (!e || index < 0 || index >= (*e).count) return NULL;
    return (*e).children[index];
}

Element *Element_parent(const Element *e) { return e ? (*e).parent : NULL; }

Element *Element_root(Element *e) {
    if (!e) return NULL;
    while ((*e).parent) e = (*e).parent;
    return e;
}

Element *Element_find(Element *root, const char *tag) {
    if (!root || !tag) return NULL;
    if ((*root).tag && !strcmp((*root).tag, tag)) return root;
    for (int i = 0; i < (*root).count; i++) {
        Element *hit = Element_find((*root).children[i], tag);
        if (hit) return hit;
    }
    return NULL;
}

// ── hit-test (deepest top-most element under the point) ─────────────────────
static Element *hit_rec(Element *e, Rect absolute, float x, float y) {
    if (!e || !(*e).visible || !(*e).property) return NULL;
    const Property *property = (*e).property;
    // Descendants cannot receive input where an ancestor masks their paint.
    if ((*property).clip || (*property).radius > 0.0f) {
        if (!Rect_contains(absolute, x, y)) return NULL;
        float radius = fminf((*property).radius, fminf(absolute.w, absolute.h) * 0.5f);
        if (radius > 0.0f) {
            float qx = fmaxf(fabsf(x - absolute.x - absolute.w * 0.5f) - (absolute.w * 0.5f - radius), 0.0f);
            float qy = fmaxf(fabsf(y - absolute.y - absolute.h * 0.5f) - (absolute.h * 0.5f - radius), 0.0f);
            if (qx * qx + qy * qy > radius * radius) return NULL;
        }
    }
    for (int i = (*e).count - 1; i >= 0; i--) {
        Element *c = (*e).children[i];
        Rect cr = Element_resolve(c, absolute);
        Element *hit = hit_rec(c, cr, x, y);
        if (hit) return hit;
    }
    if (x >= absolute.x && x < absolute.x + absolute.w &&
        y >= absolute.y && y < absolute.y + absolute.h)
        return e;
    return NULL;
}

Element *Element_hit(Element *root, float x, float y) {
    if (!root) return NULL;
    Rect self = {0, 0, Property_width((*root).property), Property_height((*root).property)};
    return hit_rec(root, self, x, y);
}

// ── geometry ────────────────────────────────────────────────────────────────
Rect Element_resolve(const Element *e, Rect parent) {
    if (!e || !(*e).property) return (Rect){0, 0, 0, 0};
    float w = Property_width((*e).property);
    float h = Property_height((*e).property);
    Point anchor = Part_point(parent, (*e).anchor);
    Rect self = {0.0f, 0.0f, w, h};
    Point pivot = Part_point(self, (*e).pivot);
    return (Rect){anchor.x + (*e).offsetX - pivot.x,
                  anchor.y + (*e).offsetY - pivot.y,
                  w, h};
}

Element *Element_setSize(Element *e, float w, float h) {
    if (e && (*e).property) { (*e).property->w = w; (*e).property->h = h; Element_markDirty(e); }
    return e;
}
Element *Element_setMinimumSize(Element *e, float w, float h) {
    if (e && (*e).property) { Property_setMinSize((*e).property, w, h); Element_markDirty(e); }
    return e;
}
Element *Element_setMaximumSize(Element *e, float w, float h) {
    if (e && (*e).property) { Property_setMaxSize((*e).property, w, h); Element_markDirty(e); }
    return e;
}
Element *Element_setOffset(Element *e, float x, float y) {
    if (e) { (*e).offsetX = x; (*e).offsetY = y; Element_markDirty(e); }
    return e;
}
Element *Element_setAnchor(Element *e, int anchor) {
    if (e && anchor >= 0 && anchor < PART_COUNT) { (*e).anchor = anchor; Element_markDirty(e); }
    return e;
}
Element *Element_setPivot(Element *e, int pivot) {
    if (e && pivot >= 0 && pivot < PART_COUNT) { (*e).pivot = pivot; Element_markDirty(e); }
    return e;
}
Element *Element_setTag(Element *e, const char *tag) {
    if (e) (*e).tag = tag;
    return e;
}

// ── visual ──────────────────────────────────────────────────────────────────
Element *Element_setRadius(Element *e, float radius) {
    if (e && (*e).property) { (*e).property->radius = radius < 0.0f ? 0.0f : radius; Element_markDirty(e); }
    return e;
}
Element *Element_setBackground(Element *e, Color color) {
    if (e && (*e).property) { (*e).property->background = color; Element_markDirty(e); }
    return e;
}
Element *Element_setBorder(Element *e, Color color, float width) {
    if (e && (*e).property) {
        (*e).property->border = color;
        (*e).property->borderWidth = width < 0.0f ? 0.0f : width;
        Element_markDirty(e);
    }
    return e;
}
Element *Element_setShadow(Element *e, float offsetX, float offsetY, float blur) {
    if (!e || !(*e).property) return e;
    (*e).property->shadowX = offsetX;
    (*e).property->shadowY = offsetY;
    (*e).property->shadowBlur = blur < 0.0f ? 0.0f : blur;
    // a lone setShadow() should *do* something: default the colour if unset
    if (Color_alpha((*e).property->shadow) == 0u)
        (*e).property->shadow = COLOR_RGBA(0, 0, 0, 128);
    Element_markDirty(e);
    return e;
}
Element *Element_setShadowColor(Element *e, Color color) {
    if (e && (*e).property) { (*e).property->shadow = color; Element_markDirty(e); }
    return e;
}
Element *Element_setBlur(Element *e, float blur) {
    if (e && (*e).property) { (*e).property->blur = blur < 0.0f ? 0.0f : blur; Element_markDirty(e); }
    return e;
}
Element *Element_setClip(Element *e, bool clip) {
    if (e && (*e).property) { (*e).property->clip = clip; Element_markDirty(e); }
    return e;
}
Element *Element_setPressed(Element *e, bool pressed) {
    if (e) (*e).pressed = pressed;
    return e;
}
Element *Element_setVisible(Element *e, bool visible) {
    if (e) (*e).visible = visible;
    return e;
}

// ── queries ─────────────────────────────────────────────────────────────────
float Element_width(const Element *e) { return (e && (*e).property) ? Property_width((*e).property) : 0.0f; }
float Element_height(const Element *e) { return (e && (*e).property) ? Property_height((*e).property) : 0.0f; }
float Element_radius(const Element *e) { return (e && (*e).property) ? (*e).property->radius : 0.0f; }
int   Element_anchor(const Element *e) { return e ? (*e).anchor : PART_TOP_LEFT; }
int   Element_pivot(const Element *e) { return e ? (*e).pivot : PART_TOP_LEFT; }
bool  Element_isValid(const Element *e) { return e && (*e).property && (*e).property->w >= 0.0f && (*e).property->h >= 0.0f; }
bool  Element_isVisible(const Element *e) { return e && (*e).visible; }
bool  Element_isPressed(const Element *e) { return e && (*e).pressed; }
const char *Element_tag(const Element *e) { return e ? (*e).tag : NULL; }

// ── paint ───────────────────────────────────────────────────────────────────
void Element_paint(const Element *e, Rect absolute, DisplayList *dl) {
    if (!e || !dl || !(*e).visible || !(*e).property) return;
    const Property *p = (*e).property;

    float eb = (*p).blur > 0.0f ? (*p).blur : 0.0f;   // the element's own soft edge
    bool paints = Color_alpha((*p).background) != 0u ||
                  (Color_alpha((*p).border) != 0u && (*p).borderWidth > 0.0f) ||
                  Color_alpha((*p).shadow) != 0u;
    if (paints) {
        if (Color_alpha((*p).shadow) != 0u) {
            // shadow = the shape, offset, with a blur margin to fit the falloff
            float sb = (*p).shadowBlur > 0.0f ? (*p).shadowBlur : 0.0f;
            Rect sr = {absolute.x + (*p).shadowX - sb, absolute.y + (*p).shadowY - sb,
                       absolute.w + 2.0f * sb, absolute.h + 2.0f * sb};
            Brush shadow = {(*p).shadow, (*p).radius, 0u, 0.0f, sb};
            DisplayList_rect(dl, sr, &shadow);
        }
        // the body grows by the blur margin too; the layout rect never moves
        Rect body = {absolute.x - eb, absolute.y - eb,
                     absolute.w + 2.0f * eb, absolute.h + 2.0f * eb};
        Brush b = {(*p).background, (*p).radius, (*p).border, (*p).borderWidth, eb};
        DisplayList_rect(dl, body, &b);
        if ((*e).pressed) {
            Brush dim = {COLOR_RGBA(0, 0, 0, 48), (*p).radius, 0u, 0.0f, eb};
            DisplayList_rect(dl, body, &dim);
        }
    }

    // A clipping / rounded parent MASKS its children: the radius means what it
    // says, and a scroll viewport clips to its rect.
    bool clips = ((*p).clip || (*p).radius > 0.0f) && (*e).count > 0;
    if (clips) DisplayList_clipRounded(dl, absolute, (*p).radius);
    for (int i = 0; i < (*e).count; i++) {
        Element *c = (*e).children[i];
        Element_paint(c, Element_resolve(c, absolute), dl);
    }
    if (clips) DisplayList_unclip(dl);
}

Rect Element_bounds(const Element *e, Rect parent) {
    Rect base = Element_resolve(e, parent);
    if (!e || !(*e).property) return base;
    float eb = (*e).property->blur > 0.0f ? (*e).property->blur : 0.0f;
    float x0 = base.x - eb, y0 = base.y - eb;
    float x1 = base.x + base.w + eb, y1 = base.y + base.h + eb;
    if (Color_alpha((*e).property->shadow) != 0u) {
        float sb = (*e).property->shadowBlur > 0.0f ? (*e).property->shadowBlur : 0.0f;
        float sx0 = base.x + (*e).property->shadowX - sb, sy0 = base.y + (*e).property->shadowY - sb;
        float sx1 = base.x + base.w + (*e).property->shadowX + sb, sy1 = base.y + base.h + (*e).property->shadowY + sb;
        if (sx0 < x0) x0 = sx0;
        if (sy0 < y0) y0 = sy0;
        if (sx1 > x1) x1 = sx1;
        if (sy1 > y1) y1 = sy1;
    }
    return (Rect){x0, y0, x1 - x0, y1 - y0};
}
