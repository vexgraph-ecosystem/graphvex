#include "panel.h"

#include <stdlib.h>

// graphvex R3 — panel.c
// The UI component + the anchor/pivot placement model. Pure geometry; the
// paint path just appends rounded rects to a display list.

Point Part_point(Rect r, int part) {
    if (part < 0 || part >= PART_COUNT) part = PART_TOP_LEFT;
    int col = part % 3;   // 0 left, 1 centre, 2 right
    int row = part / 3;   // 0 top,  1 middle, 2 bottom
    Point p = {r.x + (float)col * 0.5f * r.w, r.y + (float)row * 0.5f * r.h};
    return p;
}

struct Panel {
    float width, height;
    float offsetX, offsetY;
    int anchor;
    int pivot;
    float radius;
    Color background;
    Color border;
    float borderWidth;
    Color shadow;
    float shadowX, shadowY, shadowSpread;
};

Panel *Panel_new(const PanelDesc *desc) {
    Panel *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->width = desc ? desc->width : 0.0f;
    p->height = desc ? desc->height : 0.0f;
    p->offsetX = desc ? desc->offsetX : 0.0f;
    p->offsetY = desc ? desc->offsetY : 0.0f;
    p->anchor = desc ? desc->anchor : PART_TOP_LEFT;
    p->pivot = desc ? desc->pivot : PART_TOP_LEFT;
    p->radius = desc ? desc->radius : 0.0f;
    p->background = desc ? desc->background : COLOR_WHITE;
    p->border = desc ? desc->border : COLOR_CLEAR;
    p->borderWidth = desc ? desc->borderWidth : 0.0f;
    p->shadow = desc ? desc->shadow : COLOR_CLEAR;
    p->shadowX = desc ? desc->shadowX : 0.0f;
    p->shadowY = desc ? desc->shadowY : 0.0f;
    p->shadowSpread = desc ? desc->shadowSpread : 0.0f;
    return p;
}

Panel *Panel_0(void) { return Panel_new(NULL); }

void Panel_destroy(Panel *panel) { free(panel); }

Panel *Panel_setSize(Panel *p, float width, float height) {
    if (p) { p->width = width; p->height = height; }
    return p;
}
Panel *Panel_setOffset(Panel *p, float x, float y) {
    if (p) { p->offsetX = x; p->offsetY = y; }
    return p;
}
Panel *Panel_setAnchor(Panel *p, int anchor) {
    if (p && anchor >= 0 && anchor < PART_COUNT) p->anchor = anchor;
    return p;
}
Panel *Panel_setPivot(Panel *p, int pivot) {
    if (p && pivot >= 0 && pivot < PART_COUNT) p->pivot = pivot;
    return p;
}
Panel *Panel_setRadius(Panel *p, float radius) {
    if (p) p->radius = radius < 0.0f ? 0.0f : radius;
    return p;
}
Panel *Panel_setBackground(Panel *p, Color color) {
    if (p) p->background = color;
    return p;
}
Panel *Panel_setBorder(Panel *p, Color color, float width) {
    if (p) { p->border = color; p->borderWidth = width < 0.0f ? 0.0f : width; }
    return p;
}
Panel *Panel_setShadow(Panel *p, Color color, float dx, float dy, float spread) {
    if (p) { p->shadow = color; p->shadowX = dx; p->shadowY = dy; p->shadowSpread = spread; }
    return p;
}

int   Panel_anchor(const Panel *p) { return p ? p->anchor : PART_TOP_LEFT; }
int   Panel_pivot(const Panel *p) { return p ? p->pivot : PART_TOP_LEFT; }
float Panel_width(const Panel *p) { return p ? p->width : 0.0f; }
float Panel_height(const Panel *p) { return p ? p->height : 0.0f; }
float Panel_radius(const Panel *p) { return p ? p->radius : 0.0f; }
bool  Panel_isValid(const Panel *p) { return p && p->width >= 0.0f && p->height >= 0.0f; }

Rect Panel_resolve(const Panel *p, Rect parent) {
    if (!p) return (Rect){0, 0, 0, 0};
    Point anchor = Part_point(parent, p->anchor);
    Rect self = {0.0f, 0.0f, p->width, p->height};
    Point pivot = Part_point(self, p->pivot);
    return (Rect){anchor.x + p->offsetX - pivot.x,
                  anchor.y + p->offsetY - pivot.y,
                  p->width, p->height};
}

void Panel_paint(const Panel *p, Rect absolute, DisplayList *dl) {
    if (!p || !dl) return;
    if (Color_alpha(p->shadow) != 0u) {
        Rect sr = {absolute.x + p->shadowX - p->shadowSpread,
                   absolute.y + p->shadowY - p->shadowSpread,
                   absolute.w + 2.0f * p->shadowSpread,
                   absolute.h + 2.0f * p->shadowSpread};
        Brush shadow = {p->shadow, p->radius + p->shadowSpread, 0u, 0.0f};
        DisplayList_rect(dl, sr, &shadow);
    }
    Brush body = {p->background, p->radius, p->border, p->borderWidth};
    DisplayList_rect(dl, absolute, &body);
}
