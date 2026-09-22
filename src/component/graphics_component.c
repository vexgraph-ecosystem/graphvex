#include "lang/graphics_component.h"
#include "lang/str.h"

#include <math.h>
#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: GraphicsComponent
 * ============================================================================
 * The element metadata of the graphics language: placement, size, scale, the
 * three dials (origin/anchor/pivot), constraints, margin/padding, and
 * presentation state — resolved eagerly on every geometry setter into TWO render
 * currencies: `local` (a 2D affine Transform, the local->parent matrix) and the
 * absolute AABB (for hit-test/cull).
 *
 * An ElementNode owns an array of GraphicsComponents and feeds each one its
 * parent box via GraphicsComponent_setParentAbs once per layout. A component
 * owns no tree, no children, no hooks. Setters never lay out beyond this
 * component's own local transform — cascading is the ElementNode's job.
 *
 * Ported from the old reference (darling/component.c), renamed to the graphvex
 * term and lifted into the shared language, with the matrix render currency
 * added.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: GraphicsComponent (component/graphics_component.c)
 * LEVEL: L2 — Behavior (element metadata + resolved transform)
 * ============================================================================
 * SUMMARY:
 *   Pure metadata for one element plus its resolved local Transform + abs AABB.
 *   No tree, no hooks. Owned in arrays by ElementNode.
 *
 * STRUCT FIELDS (Mirroring lang/graphics_component.h):
 * ----------------------------------------------------------------------------
 *   float x, y, w, h;        // Placement + size in parent units
 *   float scaleX, scaleY;    // Axis scale multipliers (1 = unscaled)
 *   uint8_t origin;          // GRAPHICS_COMPONENT_ORIGIN_* 0..3
 *   uint8_t anchor;          // GRAPHICS_COMPONENT_ANCHOR_* 0..8
 *   int32_t pivot;           // GRAPHICS_COMPONENT_PIVOT_* 0..4
 *   float minW, minH; float maxW, maxH;   // Size constraints (0 = unset)
 *   float minX, minY; float maxX, maxY;   // Location constraints
 *   float marginL/T/R/B;     // Additive placement offsets
 *   float paddingL/T/R/B;    // Inward content insets
 *   float borderWidth; uint32_t borderColor; uint32_t backgroundColor;
 *   float cornerRadius; int radiusMode; float opacity; int32_t z; uint8_t visible;
 *   Transform local;         // RESOLVED local->parent affine (render currency)
 *   float absX, absY, absW, absH;               // RESOLVED absolute AABB
 *   float parentAbsX, parentAbsY, parentAbsW, parentAbsH; // Parent box at last recompute
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   contentBox(self, outX, outY, outW, outH) : abs rect inset by padding
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - GraphicsComponent_0(void) / GraphicsComponent_init(self) / GraphicsComponent_free(component)
 *
 * Public Core Functions: (.h)
 *   - GraphicsComponent_recompute(self)
 *   - GraphicsComponent_setParentAbs(self, px, py, pw, ph)
 *   - GraphicsComponent_hitTest(self, pointX, pointY)
 *   - GraphicsComponent_getContentRect(self, outX, outY, outW, outH)
 *   - GraphicsComponent_viewMap(view, ax, ay, aw, ah, outX, outY, outW, outH)
 *
 * Public Setters: (.h)
 *   - GraphicsComponent_set<Geometry/Presentation>(...)
 *
 * Public Getters: (.h)
 *   - GraphicsComponent_get... / GraphicsComponent_is...(...)
 * ============================================================================
 */

// FILE-LOCAL HELPERS

static void contentBox(const GraphicsComponent *self, float *outX, float *outY,
                       float *outW, float *outH) {
    float cx = (*self).absX + (*self).paddingL;
    float cy = (*self).absY + (*self).paddingT;
    float cw = (*self).absW - (*self).paddingL - (*self).paddingR;
    float ch = (*self).absH - (*self).paddingT - (*self).paddingB;
    if (cw < 0.0f)
        cw = 0.0f;
    if (ch < 0.0f)
        ch = 0.0f;
    if (outX) *outX = cx;
    if (outY) *outY = cy;
    if (outW) *outW = cw;
    if (outH) *outH = ch;
}

// CONSTRUCTORS (PUBLIC & PRIVATE)

void GraphicsComponent_init(GraphicsComponent *self) {
    if (!self)
        return;
    (*self).x = 0.0f;
    (*self).y = 0.0f;
    (*self).w = 0.0f;
    (*self).h = 0.0f;
    (*self).scaleX = 1.0f;
    (*self).scaleY = 1.0f;
    (*self).origin = GRAPHICS_COMPONENT_ORIGIN_TOP_LEFT;
    (*self).anchor = GRAPHICS_COMPONENT_ANCHOR_TOP_LEFT;
    (*self).pivot = GRAPHICS_COMPONENT_PIVOT_TOP_LEFT;
    (*self).minW = 0.0f;
    (*self).minH = 0.0f;
    (*self).maxW = 0.0f;
    (*self).maxH = 0.0f;
    (*self).minX = 0.0f;
    (*self).minY = 0.0f;
    (*self).maxX = 0.0f;
    (*self).maxY = 0.0f;
    (*self).marginL = 0.0f;
    (*self).marginT = 0.0f;
    (*self).marginR = 0.0f;
    (*self).marginB = 0.0f;
    (*self).paddingL = 0.0f;
    (*self).paddingT = 0.0f;
    (*self).paddingR = 0.0f;
    (*self).paddingB = 0.0f;
    (*self).borderWidth = 0.0f;
    (*self).borderColor = GRAPHICS_COMPONENT_COLOR_CLEAR;
    (*self).backgroundColor = GRAPHICS_COMPONENT_COLOR_CLEAR;
    (*self).cornerRadius = 0.0f;
    (*self).radiusMode = GRAPHICS_COMPONENT_CORNER_ARC;
    (*self).opacity = 1.0f;
    (*self).z = 0;
    (*self).visible = 1;
    (*self).local = Transform_identity();
    (*self).absX = 0.0f;
    (*self).absY = 0.0f;
    (*self).absW = 0.0f;
    (*self).absH = 0.0f;
    (*self).parentAbsX = 0.0f;
    (*self).parentAbsY = 0.0f;
    (*self).parentAbsW = 0.0f;
    (*self).parentAbsH = 0.0f;
}

GraphicsComponent *GraphicsComponent_0(void) {
    GraphicsComponent *self = (GraphicsComponent*) calloc(1, sizeof(GraphicsComponent));
    if (!self)
        return nullptr;
    GraphicsComponent_init(self);
    return self;
}

void GraphicsComponent_free(GraphicsComponent *component) {
    free(component);
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

void GraphicsComponent_recompute(GraphicsComponent *self) {
    if (!self)
        return;
    float sw = (*self).w * (*self).scaleX;
    float sh = (*self).h * (*self).scaleY;
    float locX = (*self).x;
    float locY = (*self).y;
    float parentW = (*self).parentAbsW;
    float parentH = (*self).parentAbsH;
    float Ua = 0.0f, Va = 0.0f;
    switch ((*self).anchor) {
        case GRAPHICS_COMPONENT_ANCHOR_TOP_LEFT: Ua = 0.0f; Va = 0.0f; break;
        case GRAPHICS_COMPONENT_ANCHOR_TOP_CENTER: Ua = 0.5f; Va = 0.0f; break;
        case GRAPHICS_COMPONENT_ANCHOR_TOP_RIGHT: Ua = 1.0f; Va = 0.0f; break;
        case GRAPHICS_COMPONENT_ANCHOR_MIDDLE_LEFT: Ua = 0.0f; Va = 0.5f; break;
        case GRAPHICS_COMPONENT_ANCHOR_MIDDLE_CENTER: Ua = 0.5f; Va = 0.5f; break;
        case GRAPHICS_COMPONENT_ANCHOR_MIDDLE_RIGHT: Ua = 1.0f; Va = 0.5f; break;
        case GRAPHICS_COMPONENT_ANCHOR_BOTTOM_LEFT: Ua = 0.0f; Va = 1.0f; break;
        case GRAPHICS_COMPONENT_ANCHOR_BOTTOM_CENTER: Ua = 0.5f; Va = 1.0f; break;
        case GRAPHICS_COMPONENT_ANCHOR_BOTTOM_RIGHT: Ua = 1.0f; Va = 1.0f; break;
        default: break;
    }
    float anchorX = (*self).parentAbsX + Ua * parentW;
    float anchorY = (*self).parentAbsY + Va * parentH;
    float Up = 0.0f, Vp = 0.0f;
    switch ((*self).pivot) {
        case GRAPHICS_COMPONENT_PIVOT_TOP_LEFT: Up = 0.0f; Vp = 0.0f; break;
        case GRAPHICS_COMPONENT_PIVOT_TOP_RIGHT: Up = 1.0f; Vp = 0.0f; break;
        case GRAPHICS_COMPONENT_PIVOT_BOTTOM_LEFT: Up = 0.0f; Vp = 1.0f; break;
        case GRAPHICS_COMPONENT_PIVOT_BOTTOM_RIGHT: Up = 1.0f; Vp = 1.0f; break;
        case GRAPHICS_COMPONENT_PIVOT_CENTER: Up = 0.5f; Vp = 0.5f; break;
        default: break;
    }
    float pivotX = Up * sw;
    float pivotY = Vp * sh;
    float dirX = 1.0f;
    float dirY = 1.0f;
    switch ((*self).origin) {
        case GRAPHICS_COMPONENT_ORIGIN_TOP_RIGHT:    dirX = -1.0f; dirY = 1.0f; break;
        case GRAPHICS_COMPONENT_ORIGIN_BOTTOM_LEFT:  dirX = 1.0f; dirY = -1.0f; break;
        case GRAPHICS_COMPONENT_ORIGIN_BOTTOM_RIGHT: dirX = -1.0f; dirY = -1.0f; break;
        case GRAPHICS_COMPONENT_ORIGIN_TOP_LEFT:
        default:                                     dirX = 1.0f; dirY = 1.0f; break;
    }
    float ax = anchorX - pivotX + (dirX * locX) + (*self).marginL;
    float ay = anchorY - pivotY + (dirY * locY) + (*self).marginT;

    // RESOLVE: local = translate(A) * scale; abs AABB = local applied to [0,w]x[0,h].
    Transform scale = Transform_scale((*self).scaleX, (*self).scaleY);
    Transform trans = Transform_translate(ax, ay);
    Transform_multiply(&trans, &scale, &(*self).local);
    (*self).absX = (*self).local.m02;
    (*self).absY = (*self).local.m12;
    (*self).absW = (*self).local.m00 * (*self).w;
    (*self).absH = (*self).local.m11 * (*self).h;
}

void GraphicsComponent_setParentAbs(GraphicsComponent *self, float px, float py, float pw, float ph) {
    if (!self)
        return;
    (*self).parentAbsX = px;
    (*self).parentAbsY = py;
    (*self).parentAbsW = pw;
    (*self).parentAbsH = ph;
    GraphicsComponent_recompute(self);
}

bool GraphicsComponent_hitTest(const GraphicsComponent *self, float pointX, float pointY) {
    if (!self || !(*self).visible)
        return false;
    return pointX >= (*self).absX && pointX < (*self).absX + (*self).absW
        && pointY >= (*self).absY && pointY < (*self).absY + (*self).absH;
}

void GraphicsComponent_getContentRect(const GraphicsComponent *self, float *outX, float *outY,
                                      float *outW, float *outH) {
    if (!self) {
        if (outX) *outX = 0.0f;
        if (outY) *outY = 0.0f;
        if (outW) *outW = 0.0f;
        if (outH) *outH = 0.0f;
        return;
    }
    contentBox(self, outX, outY, outW, outH);
}

void GraphicsComponent_viewMap(const GraphicsComponentView *view, float ax, float ay, float aw, float ah,
                               float *outX, float *outY, float *outW, float *outH) {
    float x0 = ax;
    float y0 = ay;
    float w = aw;
    float h = ah;
    if (view) {
        x0 = floorf((ax - (*view).originX) * (*view).scaleX);
        y0 = floorf((ay - (*view).originY) * (*view).scaleY);
        float x1 = ceilf((ax + aw - (*view).originX) * (*view).scaleX);
        float y1 = ceilf((ay + ah - (*view).originY) * (*view).scaleY);
        w = x1 - x0;
        h = y1 - y0;
        if (w < 0.0f)
            w = 0.0f;
        if (h < 0.0f)
            h = 0.0f;
    }
    if (outX) *outX = x0;
    if (outY) *outY = y0;
    if (outW) *outW = w;
    if (outH) *outH = h;
}

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void GraphicsComponent_setX(GraphicsComponent *self, float x) {
    if (!self)
        return;
    if ((*self).minX != 0.0f && x < (*self).minX)
        x = (*self).minX;
    if ((*self).maxX != 0.0f && x > (*self).maxX)
        x = (*self).maxX;
    (*self).x = x;
    GraphicsComponent_recompute(self);
}

;;SETTER
void GraphicsComponent_setY(GraphicsComponent *self, float y) {
    if (!self)
        return;
    if ((*self).minY != 0.0f && y < (*self).minY)
        y = (*self).minY;
    if ((*self).maxY != 0.0f && y > (*self).maxY)
        y = (*self).maxY;
    (*self).y = y;
    GraphicsComponent_recompute(self);
}

;;SETTER
void GraphicsComponent_setWidth(GraphicsComponent *self, float w) {
    if (!self)
        return;
    (*self).w = w;
    GraphicsComponent_recompute(self);
}

;;SETTER
void GraphicsComponent_setHeight(GraphicsComponent *self, float h) {
    if (!self)
        return;
    (*self).h = h;
    GraphicsComponent_recompute(self);
}

;;SETTER
void GraphicsComponent_setLocation(GraphicsComponent *self, float x, float y) {
    if (!self)
        return;
    GraphicsComponent_setX(self, x);
    GraphicsComponent_setY(self, y);
}

;;SETTER
void GraphicsComponent_setSize(GraphicsComponent *self, float w, float h) {
    if (!self)
        return;
    if (w < (*self).minW)
        w = (*self).minW;
    if (h < (*self).minH)
        h = (*self).minH;
    if ((*self).maxW > 0.0f && w > (*self).maxW)
        w = (*self).maxW;
    if ((*self).maxH > 0.0f && h > (*self).maxH)
        h = (*self).maxH;
    GraphicsComponent_setWidth(self, w);
    GraphicsComponent_setHeight(self, h);
}

;;SETTER
void GraphicsComponent_setMinSize(GraphicsComponent *self, float w, float h) {
    if (!self)
        return;
    (*self).minW = w;
    (*self).minH = h;
    float cw = (*self).w;
    float ch = (*self).h;
    if (cw < w) cw = w;
    if (ch < h) ch = h;
    if ((*self).maxW > 0.0f && cw > (*self).maxW) cw = (*self).maxW;
    if ((*self).maxH > 0.0f && ch > (*self).maxH) ch = (*self).maxH;
    GraphicsComponent_setWidth(self, cw);
    GraphicsComponent_setHeight(self, ch);
}

;;SETTER
void GraphicsComponent_setMaxSize(GraphicsComponent *self, float w, float h) {
    if (!self)
        return;
    (*self).maxW = w;
    (*self).maxH = h;
    float cw = (*self).w;
    float ch = (*self).h;
    if (w > 0.0f && cw > w) cw = w;
    if (h > 0.0f && ch > h) ch = h;
    if (cw < (*self).minW) cw = (*self).minW;
    if (ch < (*self).minH) ch = (*self).minH;
    GraphicsComponent_setWidth(self, cw);
    GraphicsComponent_setHeight(self, ch);
}

;;SETTER
void GraphicsComponent_setScale(GraphicsComponent *self, float sx, float sy) {
    if (!self)
        return;
    (*self).scaleX = sx;
    (*self).scaleY = sy;
    GraphicsComponent_recompute(self);
}

;;SETTER
void GraphicsComponent_setMinLocation(GraphicsComponent *self, float x, float y) {
    if (!self)
        return;
    (*self).minX = x;
    (*self).minY = y;
    float cx = (*self).x;
    float cy = (*self).y;
    if ((*self).minX != 0.0f && cx < x) cx = x;
    if ((*self).minY != 0.0f && cy < y) cy = y;
    if ((*self).maxX != 0.0f && cx > (*self).maxX) cx = (*self).maxX;
    if ((*self).maxY != 0.0f && cy > (*self).maxY) cy = (*self).maxY;
    GraphicsComponent_setX(self, cx);
    GraphicsComponent_setY(self, cy);
}

;;SETTER
void GraphicsComponent_setMaxLocation(GraphicsComponent *self, float x, float y) {
    if (!self)
        return;
    (*self).maxX = x;
    (*self).maxY = y;
    float cx = (*self).x;
    float cy = (*self).y;
    if (x != 0.0f && cx > x) cx = x;
    if (y != 0.0f && cy > y) cy = y;
    if ((*self).minX != 0.0f && cx < (*self).minX) cx = (*self).minX;
    if ((*self).minY != 0.0f && cy < (*self).minY) cy = (*self).minY;
    GraphicsComponent_setX(self, cx);
    GraphicsComponent_setY(self, cy);
}

;;SETTER
void GraphicsComponent_setOrigin(GraphicsComponent *self, int origin) {
    if (!self || origin < GRAPHICS_COMPONENT_ORIGIN_TOP_LEFT || origin > GRAPHICS_COMPONENT_ORIGIN_BOTTOM_RIGHT)
        return;
    (*self).origin = (uint8_t) origin;
    GraphicsComponent_recompute(self);
}

;;SETTER
void GraphicsComponent_setAnchor(GraphicsComponent *self, int anchor) {
    if (!self || anchor < GRAPHICS_COMPONENT_ANCHOR_TOP_LEFT || anchor > GRAPHICS_COMPONENT_ANCHOR_BOTTOM_RIGHT)
        return;
    (*self).anchor = (uint8_t) anchor;
    GraphicsComponent_recompute(self);
}

;;SETTER
void GraphicsComponent_setPivot(GraphicsComponent *self, int pivot) {
    if (!self || pivot < GRAPHICS_COMPONENT_PIVOT_TOP_LEFT || pivot > GRAPHICS_COMPONENT_PIVOT_CENTER)
        return;
    (*self).pivot = pivot;
    GraphicsComponent_recompute(self);
}

;;SETTER
void GraphicsComponent_setCenter(GraphicsComponent *self) {
    if (!self)
        return;
    GraphicsComponent_setPivot(self, GRAPHICS_COMPONENT_PIVOT_CENTER);
}

;;SETTER
void GraphicsComponent_setMargin(GraphicsComponent *self, float l, float t, float r, float b) {
    if (!self)
        return;
    (*self).marginL = l;
    (*self).marginT = t;
    (*self).marginR = r;
    (*self).marginB = b;
    GraphicsComponent_recompute(self);
}

;;SETTER
void GraphicsComponent_setPadding(GraphicsComponent *self, float l, float t, float r, float b) {
    if (!self)
        return;
    (*self).paddingL = l < 0.0f ? 0.0f : l;
    (*self).paddingT = t < 0.0f ? 0.0f : t;
    (*self).paddingR = r < 0.0f ? 0.0f : r;
    (*self).paddingB = b < 0.0f ? 0.0f : b;
    GraphicsComponent_recompute(self);
}

;;SETTER
void GraphicsComponent_setBorderWidth(GraphicsComponent *self, float w) {
    if (!self)
        return;
    (*self).borderWidth = w < 0.0f ? 0.0f : w;
}

;;SETTER
void GraphicsComponent_setBorderColor(GraphicsComponent *self, uint32_t color) {
    if (!self)
        return;
    (*self).borderColor = color;
}

;;SETTER
void GraphicsComponent_setBackgroundColor(GraphicsComponent *self, uint32_t color) {
    if (!self)
        return;
    (*self).backgroundColor = color;
}

;;SETTER
void GraphicsComponent_setCornerRadius(GraphicsComponent *self, float r) {
    if (!self)
        return;
    (*self).cornerRadius = r < 0.0f ? 0.0f : r;
}

;;SETTER
void GraphicsComponent_setRadiusMode(GraphicsComponent *self, int mode) {
    if (!self)
        return;
    if (mode != GRAPHICS_COMPONENT_CORNER_ARC && mode != GRAPHICS_COMPONENT_CORNER_SUPERELLIPSE)
        return;
    (*self).radiusMode = mode;
}

;;SETTER
void GraphicsComponent_setOpacity(GraphicsComponent *self, float opacity) {
    if (!self)
        return;
    (*self).opacity = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
}

;;SETTER
void GraphicsComponent_setZ(GraphicsComponent *self, int z) {
    if (!self)
        return;
    (*self).z = z;
}

;;SETTER
void GraphicsComponent_setVisible(GraphicsComponent *self, bool visible) {
    if (!self)
        return;
    (*self).visible = visible ? 1 : 0;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
float GraphicsComponent_getX(const GraphicsComponent *self) { return self ? (*self).x : 0.0f; }
;;GETTER
float GraphicsComponent_getY(const GraphicsComponent *self) { return self ? (*self).y : 0.0f; }
;;GETTER
float GraphicsComponent_getWidth(const GraphicsComponent *self) { return self ? (*self).w : 0.0f; }
;;GETTER
float GraphicsComponent_getHeight(const GraphicsComponent *self) { return self ? (*self).h : 0.0f; }
;;GETTER
float GraphicsComponent_getScaleX(const GraphicsComponent *self) { return self ? (*self).scaleX : 1.0f; }
;;GETTER
float GraphicsComponent_getScaleY(const GraphicsComponent *self) { return self ? (*self).scaleY : 1.0f; }

;;GETTER
Transform GraphicsComponent_getLocal(const GraphicsComponent *self) {
    return self ? (*self).local : Transform_identity();
}

;;GETTER
float GraphicsComponent_getAbsX(const GraphicsComponent *self) { return self ? (*self).absX : 0.0f; }
;;GETTER
float GraphicsComponent_getAbsY(const GraphicsComponent *self) { return self ? (*self).absY : 0.0f; }
;;GETTER
float GraphicsComponent_getAbsW(const GraphicsComponent *self) { return self ? (*self).absW : 0.0f; }
;;GETTER
float GraphicsComponent_getAbsH(const GraphicsComponent *self) { return self ? (*self).absH : 0.0f; }

;;GETTER
void GraphicsComponent_getAbsRect(const GraphicsComponent *self, float *outX, float *outY, float *outW, float *outH) {
    if (outX) *outX = self ? (*self).absX : 0.0f;
    if (outY) *outY = self ? (*self).absY : 0.0f;
    if (outW) *outW = self ? (*self).absW : 0.0f;
    if (outH) *outH = self ? (*self).absH : 0.0f;
}

;;GETTER
void GraphicsComponent_getParentAbsRect(const GraphicsComponent *self, float *outX, float *outY, float *outW, float *outH) {
    if (outX) *outX = self ? (*self).parentAbsX : 0.0f;
    if (outY) *outY = self ? (*self).parentAbsY : 0.0f;
    if (outW) *outW = self ? (*self).parentAbsW : 0.0f;
    if (outH) *outH = self ? (*self).parentAbsH : 0.0f;
}

;;GETTER
int GraphicsComponent_getOrigin(const GraphicsComponent *self) {
    return self ? (*self).origin : GRAPHICS_COMPONENT_ORIGIN_TOP_LEFT;
}

;;GETTER
int GraphicsComponent_getAnchor(const GraphicsComponent *self) {
    return self ? (*self).anchor : GRAPHICS_COMPONENT_ANCHOR_TOP_LEFT;
}

;;GETTER
int GraphicsComponent_getPivot(const GraphicsComponent *self) {
    return self ? (*self).pivot : GRAPHICS_COMPONENT_PIVOT_TOP_LEFT;
}

;;GETTER
float GraphicsComponent_getMinWidth(const GraphicsComponent *self) { return self ? (*self).minW : 0.0f; }
;;GETTER
float GraphicsComponent_getMinHeight(const GraphicsComponent *self) { return self ? (*self).minH : 0.0f; }
;;GETTER
float GraphicsComponent_getMaxWidth(const GraphicsComponent *self) { return self ? (*self).maxW : 0.0f; }
;;GETTER
float GraphicsComponent_getMaxHeight(const GraphicsComponent *self) { return self ? (*self).maxH : 0.0f; }
;;GETTER
float GraphicsComponent_getMinX(const GraphicsComponent *self) { return self ? (*self).minX : 0.0f; }
;;GETTER
float GraphicsComponent_getMinY(const GraphicsComponent *self) { return self ? (*self).minY : 0.0f; }
;;GETTER
float GraphicsComponent_getMaxX(const GraphicsComponent *self) { return self ? (*self).maxX : 0.0f; }
;;GETTER
float GraphicsComponent_getMaxY(const GraphicsComponent *self) { return self ? (*self).maxY : 0.0f; }

;;GETTER
void GraphicsComponent_getMargin(const GraphicsComponent *self, float *outL, float *outT, float *outR, float *outB) {
    if (outL) *outL = self ? (*self).marginL : 0.0f;
    if (outT) *outT = self ? (*self).marginT : 0.0f;
    if (outR) *outR = self ? (*self).marginR : 0.0f;
    if (outB) *outB = self ? (*self).marginB : 0.0f;
}

;;GETTER
void GraphicsComponent_getPadding(const GraphicsComponent *self, float *outL, float *outT, float *outR, float *outB) {
    if (outL) *outL = self ? (*self).paddingL : 0.0f;
    if (outT) *outT = self ? (*self).paddingT : 0.0f;
    if (outR) *outR = self ? (*self).paddingR : 0.0f;
    if (outB) *outB = self ? (*self).paddingB : 0.0f;
}

;;GETTER
float GraphicsComponent_getBorderWidth(const GraphicsComponent *self) { return self ? (*self).borderWidth : 0.0f; }
;;GETTER
uint32_t GraphicsComponent_getBorderColor(const GraphicsComponent *self) { return self ? (*self).borderColor : GRAPHICS_COMPONENT_COLOR_CLEAR; }
;;GETTER
uint32_t GraphicsComponent_getBackgroundColor(const GraphicsComponent *self) { return self ? (*self).backgroundColor : GRAPHICS_COMPONENT_COLOR_CLEAR; }
;;GETTER
float GraphicsComponent_getCornerRadius(const GraphicsComponent *self) { return self ? (*self).cornerRadius : 0.0f; }
;;GETTER
int GraphicsComponent_getRadiusMode(const GraphicsComponent *self) { return self ? (*self).radiusMode : GRAPHICS_COMPONENT_CORNER_ARC; }
;;GETTER
float GraphicsComponent_getOpacity(const GraphicsComponent *self) { return self ? (*self).opacity : 1.0f; }
;;GETTER
int GraphicsComponent_getZ(const GraphicsComponent *self) { return self ? (*self).z : 0; }
;;GETTER
bool GraphicsComponent_isVisible(const GraphicsComponent *self) { return self && (*self).visible != 0; }

// --- toString Law (bounded, cold-path) ---

void GraphicsComponent_toString(const GraphicsComponent *self, char *dest, size_t cap, bool *outTruncated) {
    Str s;
    Str_init(&s, dest, cap);
    if (self == nullptr) {
        Str_put(&s, "nullptr");
        if (outTruncated) *outTruncated = false;
        return;
    }
    Str_printf(&s, "GraphicsComponent[(%.1f, %.1f) %.1fx%.1f abs(%.1f, %.1f, %.1f, %.1f) bg=0x%08X]",
               (*self).x, (*self).y, (*self).w, (*self).h,
               (*self).absX, (*self).absY, (*self).absW, (*self).absH,
               (*self).backgroundColor);
    if (outTruncated) *outTruncated = Str_isTruncated(&s);
}

void GraphicsComponent_toStringStruct(const GraphicsComponent *self, char *dest, size_t cap, bool *outTruncated) {
    Str s;
    Str_init(&s, dest, cap);
    if (self == nullptr) {
        Str_put(&s, "nullptr");
        if (outTruncated) *outTruncated = false;
        return;
    }
    Str_put(&s, "GraphicsComponent { ");
    Str_printf(&s, "x: %.1f, y: %.1f, w: %.1f, h: %.1f, ", (*self).x, (*self).y, (*self).w, (*self).h);
    Str_printf(&s, "scaleX: %.2f, scaleY: %.2f, ", (*self).scaleX, (*self).scaleY);
    Str_printf(&s, "origin: %u, anchor: %u, pivot: %d, ", (unsigned) (*self).origin, (unsigned) (*self).anchor, (*self).pivot);
    Str_printf(&s, "margin: (%.1f, %.1f, %.1f, %.1f), ", (*self).marginL, (*self).marginT, (*self).marginR, (*self).marginB);
    Str_printf(&s, "padding: (%.1f, %.1f, %.1f, %.1f), ", (*self).paddingL, (*self).paddingT, (*self).paddingR, (*self).paddingB);
    Str_printf(&s, "borderWidth: %.1f, borderColor: 0x%08X, ", (*self).borderWidth, (*self).borderColor);
    Str_printf(&s, "backgroundColor: 0x%08X, cornerRadius: %.1f, opacity: %.2f, z: %d, visible: %s, ",
               (*self).backgroundColor, (*self).cornerRadius, (*self).opacity, (*self).z,
               (*self).visible ? "true" : "false");
    Str_printf(&s, "abs: (%.1f, %.1f, %.1f, %.1f) }", (*self).absX, (*self).absY, (*self).absW, (*self).absH);
    if (outTruncated) *outTruncated = Str_isTruncated(&s);
}
