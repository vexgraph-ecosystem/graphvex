#ifndef LANG_GRAPHICS_COMPONENT_H
#define LANG_GRAPHICS_COMPONENT_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/transform.h"

// lang/graphics_component.h — the graphics element metadata (anchor / origin / pivot).
//
// THE THREE DIALS that place one element inside its parent — the whole reason a
// pinned element stays glued to an edge while the window resizes:
//
//   ORIGIN — the PARENT's coordinate zero-point and axis direction (4 corners).
//            It decides which way positive x/y point.
//   ANCHOR — where on the PARENT the element tracks during resize (9-grid).
//   PIVOT  — the element's OWN reference point for placement (4 corners + center).
//
// A GraphicsComponent carries placement + size + scale + the three dials +
// constraints + margin/padding + presentation state, and RESOLVES on every
// geometry setter into TWO render currencies (GraphicsComponent_recompute):
//
//   local — a 2D affine Transform (lang/transform.h): the element's local→parent
//           matrix, T = translate(A) · scale, with
//           A = parentAnchorPoint − pivot·elementSize + dir·loc + margin.
//   abs   — the absolute AABB (local applied to the element's [0,w]×[0,h] box),
//           for hit-testing and culling, which want a rect, not a matrix.
//
// So: DIALS IN, TRANSFORM + AABB OUT. Authoring stays readable; rendering
// composes the matrix down an ElementNode tree; hit-testing reads the AABB.
//
// NAMING: this is the GRAPHVEX term (the language). darling's own interface type
// stays `Component` — same idea, different layer, no collision. A GraphicsComponent
// owns no tree, no children, no hooks: an ElementNode owns the array.
//
// Ported from the old reference (darling/component.h/.c), renamed and lifted
// into the shared language, with the matrix render currency added.

// ORIGIN — the parent's coordinate zero-point and axis direction (4 corners).
#define GRAPHICS_COMPONENT_ORIGIN_TOP_LEFT      0
#define GRAPHICS_COMPONENT_ORIGIN_TOP_RIGHT     1
#define GRAPHICS_COMPONENT_ORIGIN_BOTTOM_LEFT   2
#define GRAPHICS_COMPONENT_ORIGIN_BOTTOM_RIGHT  3

// ANCHOR — where on the parent the element tracks during resize (9-grid).
#define GRAPHICS_COMPONENT_ANCHOR_TOP_LEFT      0
#define GRAPHICS_COMPONENT_ANCHOR_TOP_CENTER    1
#define GRAPHICS_COMPONENT_ANCHOR_TOP_RIGHT     2
#define GRAPHICS_COMPONENT_ANCHOR_MIDDLE_LEFT   3
#define GRAPHICS_COMPONENT_ANCHOR_MIDDLE_CENTER 4
#define GRAPHICS_COMPONENT_ANCHOR_MIDDLE_RIGHT  5
#define GRAPHICS_COMPONENT_ANCHOR_BOTTOM_LEFT   6
#define GRAPHICS_COMPONENT_ANCHOR_BOTTOM_CENTER 7
#define GRAPHICS_COMPONENT_ANCHOR_BOTTOM_RIGHT  8

// PIVOT — the element's own reference point for placement (4 corners + center).
#define GRAPHICS_COMPONENT_PIVOT_TOP_LEFT      0
#define GRAPHICS_COMPONENT_PIVOT_TOP_RIGHT     1
#define GRAPHICS_COMPONENT_PIVOT_BOTTOM_LEFT   2
#define GRAPHICS_COMPONENT_PIVOT_BOTTOM_RIGHT  3
#define GRAPHICS_COMPONENT_PIVOT_CENTER        4

// Corner radius modes.
#define GRAPHICS_COMPONENT_CORNER_ARC          0
#define GRAPHICS_COMPONENT_CORNER_SUPERELLIPSE 1

#define GRAPHICS_COMPONENT_COLOR_CLEAR 0x00000000u

// Pure-data device mapping handed to paint paths: the point-to-native-pixel
// scale of the current present plus an abs-space translation (originX/originY):
// a sample point is (abs - origin) * scale, so shifted views (scroll/pan) reuse
// the exact same currency. No graphics pointer: mapping is pure math.
typedef struct GraphicsComponentView {
    float scaleX;
    float scaleY;
    float originX;
    float originY;
} GraphicsComponentView;

typedef struct GraphicsComponent {
    // --- Geometry (parent units; placement varies with anchor) ---
    // w/h hold the DECLARED size, which may be SIZE_AUTO (the åuto sentinel).
    // AUTO is the default; `w == SIZE_AUTO` is the check that swaps in the
    // element's AUTO EQUIVALENCE (measuredW/measuredH below).
    float x, y, w, h;
    float scaleX, scaleY;
    uint8_t origin;          // GRAPHICS_COMPONENT_ORIGIN_*
    uint8_t anchor;          // GRAPHICS_COMPONENT_ANCHOR_*
    int32_t pivot;           // GRAPHICS_COMPONENT_PIVOT_*
    float minW, minH;
    float maxW, maxH;
    float minX, minY;
    float maxX, maxY;
    // --- AUTO equivalence (owner-supplied; the class's AUTO default size) ---
    // A dumb element (base/GraphicsPanel) leaves these 0, so its AUTO resolves to 0.
    // An element with intrinsic content (Label) writes its measured size here,
    // leaving w/h at the sentinel so it re-measures on every render.
    float measuredW, measuredH;
    // --- Spacing ---
    float marginL, marginT, marginR, marginB;
    float paddingL, paddingT, paddingR, paddingB;
    // --- Presentation state ---
    float borderWidth;
    uint32_t borderColor;
    uint32_t backgroundColor;
    float cornerRadius;
    int radiusMode;
    float opacity;
    int32_t z;
    uint8_t visible;
    // --- Resolved render currencies (recomputed on every geometry setter) ---
    Transform local;         // local→parent affine (the render currency)
    float absX, absY;        // absolute AABB (hit-test / cull)
    float absW, absH;
    float parentAbsX, parentAbsY;
    float parentAbsW, parentAbsH;
} GraphicsComponent;

// --- Constructors ---
// GraphicsComponent() : heap element at origin, TOP_LEFT everything, visible, opaque.
GraphicsComponent *GraphicsComponent_0(void);
// Value initializer for embedded members: fills defaults in place.
void GraphicsComponent_init(GraphicsComponent *self);
// Free a heap GraphicsComponent (null-safe). Embedded ones need no free.
void GraphicsComponent_free(GraphicsComponent *component);

// --- Core functions ---
// Resolve the dials into `local` (Transform) + the abs AABB. Called by every
// geometry setter; call it directly after writing fields by hand.
void GraphicsComponent_recompute(GraphicsComponent *self);
void GraphicsComponent_setParentAbs(GraphicsComponent *self, float px, float py, float pw, float ph);
bool GraphicsComponent_hitTest(const GraphicsComponent *self, float pointX, float pointY);
void GraphicsComponent_getContentRect(const GraphicsComponent *self, float *outX, float *outY,
                                      float *outW, float *outH);
void GraphicsComponent_viewMap(const GraphicsComponentView *view, float ax, float ay, float aw, float ah,
                               float *outX, float *outY, float *outW, float *outH);

// --- Geometry setters (each recomputes eagerly) ---
void GraphicsComponent_setX(GraphicsComponent *self, float x);
void GraphicsComponent_setY(GraphicsComponent *self, float y);
void GraphicsComponent_setWidth(GraphicsComponent *self, float w);
void GraphicsComponent_setHeight(GraphicsComponent *self, float h);
void GraphicsComponent_setScale(GraphicsComponent *self, float sx, float sy);
void GraphicsComponent_setLocation(GraphicsComponent *self, float x, float y);
void GraphicsComponent_setSize(GraphicsComponent *self, float w, float h);
void GraphicsComponent_setMinSize(GraphicsComponent *self, float w, float h);
void GraphicsComponent_setMaxSize(GraphicsComponent *self, float w, float h);
void GraphicsComponent_setMinLocation(GraphicsComponent *self, float x, float y);
void GraphicsComponent_setMaxLocation(GraphicsComponent *self, float x, float y);
void GraphicsComponent_setOrigin(GraphicsComponent *self, int origin);
void GraphicsComponent_setAnchor(GraphicsComponent *self, int anchor);
void GraphicsComponent_setPivot(GraphicsComponent *self, int pivot);
void GraphicsComponent_setCenter(GraphicsComponent *self);
void GraphicsComponent_setMargin(GraphicsComponent *self, float l, float t, float r, float b);
void GraphicsComponent_setPadding(GraphicsComponent *self, float l, float t, float r, float b);
// The AUTO equivalence: the concrete size an AUTO dim resolves to. Owners with
// intrinsic content (Label) write their measured size here; a dumb element
// leaves it 0. Does not touch w/h (the declared sentinel survives).
void GraphicsComponent_setMeasuredSize(GraphicsComponent *self, float w, float h);

// --- Presentation setters (no recompute) ---
void GraphicsComponent_setBorderWidth(GraphicsComponent *self, float w);
void GraphicsComponent_setBorderColor(GraphicsComponent *self, uint32_t color);
void GraphicsComponent_setBackgroundColor(GraphicsComponent *self, uint32_t color);
void GraphicsComponent_setCornerRadius(GraphicsComponent *self, float r);
void GraphicsComponent_setRadiusMode(GraphicsComponent *self, int mode);
void GraphicsComponent_setOpacity(GraphicsComponent *self, float opacity);
void GraphicsComponent_setZ(GraphicsComponent *self, int z);
void GraphicsComponent_setVisible(GraphicsComponent *self, bool visible);

// --- Getters (null-safe defaults) ---
float GraphicsComponent_getX(const GraphicsComponent *self);
float GraphicsComponent_getY(const GraphicsComponent *self);
float GraphicsComponent_getWidth(const GraphicsComponent *self);
float GraphicsComponent_getHeight(const GraphicsComponent *self);
bool GraphicsComponent_isAutoWidth(const GraphicsComponent *self);
bool GraphicsComponent_isAutoHeight(const GraphicsComponent *self);
// The resolved (pre-scale) extent: the declared size, or the AUTO equivalence
// when the declared dim is the sentinel.
float GraphicsComponent_getResolvedWidth(const GraphicsComponent *self);
float GraphicsComponent_getResolvedHeight(const GraphicsComponent *self);
float GraphicsComponent_getMeasuredWidth(const GraphicsComponent *self);
float GraphicsComponent_getMeasuredHeight(const GraphicsComponent *self);
float GraphicsComponent_getScaleX(const GraphicsComponent *self);
float GraphicsComponent_getScaleY(const GraphicsComponent *self);
Transform GraphicsComponent_getLocal(const GraphicsComponent *self);
float GraphicsComponent_getAbsX(const GraphicsComponent *self);
float GraphicsComponent_getAbsY(const GraphicsComponent *self);
float GraphicsComponent_getAbsW(const GraphicsComponent *self);
float GraphicsComponent_getAbsH(const GraphicsComponent *self);
void GraphicsComponent_getAbsRect(const GraphicsComponent *self, float *outX, float *outY, float *outW, float *outH);
void GraphicsComponent_getParentAbsRect(const GraphicsComponent *self, float *outX, float *outY, float *outW, float *outH);
int GraphicsComponent_getOrigin(const GraphicsComponent *self);
int GraphicsComponent_getAnchor(const GraphicsComponent *self);
int GraphicsComponent_getPivot(const GraphicsComponent *self);
float GraphicsComponent_getMinWidth(const GraphicsComponent *self);
float GraphicsComponent_getMinHeight(const GraphicsComponent *self);
float GraphicsComponent_getMaxWidth(const GraphicsComponent *self);
float GraphicsComponent_getMaxHeight(const GraphicsComponent *self);
float GraphicsComponent_getMinX(const GraphicsComponent *self);
float GraphicsComponent_getMinY(const GraphicsComponent *self);
float GraphicsComponent_getMaxX(const GraphicsComponent *self);
float GraphicsComponent_getMaxY(const GraphicsComponent *self);
void GraphicsComponent_getMargin(const GraphicsComponent *self, float *outL, float *outT, float *outR, float *outB);
void GraphicsComponent_getPadding(const GraphicsComponent *self, float *outL, float *outT, float *outR, float *outB);
float GraphicsComponent_getBorderWidth(const GraphicsComponent *self);
uint32_t GraphicsComponent_getBorderColor(const GraphicsComponent *self);
uint32_t GraphicsComponent_getBackgroundColor(const GraphicsComponent *self);
float GraphicsComponent_getCornerRadius(const GraphicsComponent *self);
int GraphicsComponent_getRadiusMode(const GraphicsComponent *self);
float GraphicsComponent_getOpacity(const GraphicsComponent *self);
int GraphicsComponent_getZ(const GraphicsComponent *self);
bool GraphicsComponent_isVisible(const GraphicsComponent *self);

// --- toString Law (bounded, cold-path) ---
void GraphicsComponent_toString(const GraphicsComponent *self, char *dest, size_t cap, bool *outTruncated);
void GraphicsComponent_toStringStruct(const GraphicsComponent *self, char *dest, size_t cap, bool *outTruncated);

#endif // LANG_GRAPHICS_COMPONENT_H
