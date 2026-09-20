#include "vector/shape.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"
#include "nio/mem.h"
#include "oop/type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Shape
 * ============================================================================
 * Pure 2D vector path geometry representation modeling scalable vector graphics
 * without raster hardware coupling. Manages dynamic, contiguous arrays of Bézier
 * command verbs (move, line, cubic curve, close) paired with interleaved (x, y)
 * coordinate floating-point sequences. Computes exact axis-aligned bounding boxes
 * dynamically as path primitives are appended. Supports SVG path mini-language
 * parsing ("M", "L", "H", "V", "C", "Z") and geometric primitives such as
 * rectangles and cubic Bézier circles. Instances reside in unified heap memory
 * tagged TYPE_SHAPE_SINGLETON while coordinate and verb buffers scale dynamically
 * through standard realloc.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Shape (vector/shape.c)
 * LEVEL: L2 — Behavior (pure vector path geometry API)
 * ============================================================================
 * Pure vector paths without raster: builds and manipulates 2D Bézier paths
 * represented as dynamic arrays of verbs and coordinate points. Supports
 * moveTo, lineTo, cubicTo, close, procedural primitives (rect, circle),
 * and simple SVG path string parsing ("M x y L x y Z").
 *
 * STRUCT FIELDS (Mirroring vector/shape.h):
 * ----------------------------------------------------------------------------
 *   Shape {
 *     float *points;        // dynamic array of (x, y) coordinates
 *     uint8_t *verbs;       // dynamic array of ShapeVerb
 *     size_t pointCount;    // number of (x, y) coordinate points
 *     size_t pointCapacity; // allocated capacity in (x, y) points
 *     size_t verbCount;     // number of verbs
 *     size_t verbCapacity;  // allocated capacity in verbs
 *     float bounds[4];      // bounding box: [minX, minY, maxX, maxY]
 *     bool closed;          // true if path was closed with SHAPE_VERB_CLOSE
 *     uint64_t typeId;      // block-header type id (TYPE_SHAPE_SINGLETON)
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Shape_0(void)                                        : Allocate default vector shape
 *   - Shape_4(x, y, w, h)                                  : Allocate vector rectangle
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Shape_free(self)                                     : Release shape heap storage
 *   - Shape_rect(dest, x, y, w, h)                         : Generate rectangle path
 *   - Shape_circle(dest, cx, cy, r)                        : Generate circle path
 *   - Shape_moveTo(self, x, y)                             : Append move verb
 *   - Shape_lineTo(self, x, y)                             : Append line verb
 *   - Shape_cubicTo(self, x1, y1, x2, y2, x3, y3)          : Append cubic Bézier verb
 *   - Shape_close(self)                                    : Close current path contour
 *   - Shape_reset(self)                                    : Clear verbs and points
 *   - Shape_fromSvg(pathStr, dest)                         : Parse SVG path commands into shape
 *
 * Private Core Functions: (.c static)
 *   - shapeFreeStorage(self)                               : Release heap storage
 *   - shapeEnsurePointCapacity(self, needed)               : Grow points buffer capacity
 *   - shapeEnsureVerbCapacity(self, needed)                : Grow verbs buffer capacity
 *   - shapeExpandBounds(self, x, y, isFirst)               : Expand bounding box
 *   - shapeSkipSvgSpaces(p)                                : Skip SVG whitespace delimiters
 *   - shapeParseSvgFloat(pp, outVal)                       : Parse float from SVG text stream
 *
 * Public Setters: (.h)
 *   - Shape_setClosed(self, closed)                        : Set path closed state flag
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Shape_isClosed(self)                                 : Query path closed status
 *   - Shape_getTypeId(self)                                : Query block type ID
 *   - Shape_getPointCount(self)                            : Query point count
 *   - Shape_getVerbCount(self)                             : Query verb count
 *   - Shape_getBounds(self, outBounds4)                    : Retrieve axis-aligned bounding box
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

Shape *Shape_0(void) {
    Shape *self = (Shape*) Memory_alloc(TYPE_SHAPE_SINGLETON, sizeof(Shape));
    if (!self)
        self = (Shape*) calloc(1, sizeof(Shape));
    if (!self)
        return nullptr;
    (*self).points = nullptr;
    (*self).verbs = nullptr;
    (*self).pointCount = 0;
    (*self).pointCapacity = 0;
    (*self).verbCount = 0;
    (*self).verbCapacity = 0;
    (*self).bounds[0] = 0.0f;
    (*self).bounds[1] = 0.0f;
    (*self).bounds[2] = 0.0f;
    (*self).bounds[3] = 0.0f;
    (*self).closed = false;
    (*self).typeId = TYPE_SHAPE_SINGLETON;
    return self;
}

Shape *Shape_4(float x, float y, float w, float h) {
    Shape *self = Shape_0();
    if (!self)
        return nullptr;
    Shape_rect(self, x, y, w, h);
    return self;
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

static void shapeFreeStorage(Shape *self) {
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

static bool shapeEnsurePointCapacity(Shape *self, size_t needed) {
    if ((*self).pointCount + needed <= (*self).pointCapacity)
        return true;
    size_t newCap = (*self).pointCapacity == 0 ? 16 : (*self).pointCapacity * 2;
    while (newCap < (*self).pointCount + needed)
        newCap *= 2;
    float *newPoints = (float*) realloc((*self).points, newCap * 2 * sizeof(float));
    if (!newPoints)
        return false;
    (*self).points = newPoints;
    (*self).pointCapacity = newCap;
    return true;
}

static bool shapeEnsureVerbCapacity(Shape *self, size_t needed) {
    if ((*self).verbCount + needed <= (*self).verbCapacity)
        return true;
    size_t newCap = (*self).verbCapacity == 0 ? 16 : (*self).verbCapacity * 2;
    while (newCap < (*self).verbCount + needed)
        newCap *= 2;
    uint8_t *newVerbs = (uint8_t*) realloc((*self).verbs, newCap * sizeof(uint8_t));
    if (!newVerbs)
        return false;
    (*self).verbs = newVerbs;
    (*self).verbCapacity = newCap;
    return true;
}

static void shapeExpandBounds(Shape *self, float x, float y, bool isFirst) {
    if (isFirst) {
        (*self).bounds[0] = x;
        (*self).bounds[1] = y;
        (*self).bounds[2] = x;
        (*self).bounds[3] = y;
        return;
    }
    if (x < (*self).bounds[0])
        (*self).bounds[0] = x;
    if (y < (*self).bounds[1])
        (*self).bounds[1] = y;
    if (x > (*self).bounds[2])
        (*self).bounds[2] = x;
    if (y > (*self).bounds[3])
        (*self).bounds[3] = y;
}

static const char *shapeSkipSvgSpaces(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ','))
        p++;
    return p;
}

static bool shapeParseSvgFloat(const char **pp, float *outVal) {
    const char *p = shapeSkipSvgSpaces(*pp);
    if (!*p)
        return false;
    char *end = nullptr;
    float val = strtof(p, &end);
    if (end == p)
        return false;
    *outVal = val;
    *pp = end;
    return true;
}

void Shape_free(Shape *self) {
    if (!self)
        return;
    free((*self).points);
    (*self).points = nullptr;
    free((*self).verbs);
    (*self).verbs = nullptr;
    shapeFreeStorage(self);
}

void Shape_rect(Shape *dest, float x, float y, float w, float h) {
    if (!dest)
        return;
    Shape_reset(dest);
    Shape_moveTo(dest, x, y);
    Shape_lineTo(dest, x + w, y);
    Shape_lineTo(dest, x + w, y + h);
    Shape_lineTo(dest, x, y + h);
    Shape_close(dest);
}

void Shape_circle(Shape *dest, float cx, float cy, float r) {
    if (!dest)
        return;
    if (r < 0.0f)
        r = -r;
    Shape_reset(dest);
    float k = r * 0.55228475f;
    Shape_moveTo(dest, cx + r, cy);
    Shape_cubicTo(dest, cx + r, cy + k, cx + k, cy + r, cx, cy + r);
    Shape_cubicTo(dest, cx - k, cy + r, cx - r, cy + k, cx - r, cy);
    Shape_cubicTo(dest, cx - r, cy - k, cx - k, cy - r, cx, cy - r);
    Shape_cubicTo(dest, cx + k, cy - r, cx + r, cy - k, cx + r, cy);
    Shape_close(dest);
}

void Shape_moveTo(Shape *self, float x, float y) {
    if (!self)
        return;
    if (!shapeEnsurePointCapacity(self, 1) || !shapeEnsureVerbCapacity(self, 1))
        return;
    bool first = ((*self).pointCount == 0);
    size_t idx = (*self).pointCount * 2;
    (*self).points[idx] = x;
    (*self).points[idx + 1] = y;
    (*self).pointCount++;
    (*self).verbs[(*self).verbCount] = (uint8_t) SHAPE_VERB_MOVE;
    (*self).verbCount++;
    shapeExpandBounds(self, x, y, first);
}

void Shape_lineTo(Shape *self, float x, float y) {
    if (!self)
        return;
    if ((*self).pointCount == 0) {
        Shape_moveTo(self, 0.0f, 0.0f);
        if ((*self).pointCount == 0)
            return;
    }
    if (!shapeEnsurePointCapacity(self, 1) || !shapeEnsureVerbCapacity(self, 1))
        return;
    bool first = ((*self).pointCount == 0);
    size_t idx = (*self).pointCount * 2;
    (*self).points[idx] = x;
    (*self).points[idx + 1] = y;
    (*self).pointCount++;
    (*self).verbs[(*self).verbCount] = (uint8_t) SHAPE_VERB_LINE;
    (*self).verbCount++;
    shapeExpandBounds(self, x, y, first);
}

void Shape_cubicTo(Shape *self, float x1, float y1, float x2, float y2, float x3, float y3) {
    if (!self)
        return;
    if ((*self).pointCount == 0) {
        Shape_moveTo(self, 0.0f, 0.0f);
        if ((*self).pointCount == 0)
            return;
    }
    if (!shapeEnsurePointCapacity(self, 3) || !shapeEnsureVerbCapacity(self, 1))
        return;
    bool first = ((*self).pointCount == 0);
    size_t idx = (*self).pointCount * 2;
    (*self).points[idx] = x1;
    (*self).points[idx + 1] = y1;
    (*self).points[idx + 2] = x2;
    (*self).points[idx + 3] = y2;
    (*self).points[idx + 4] = x3;
    (*self).points[idx + 5] = y3;
    (*self).pointCount += 3;
    (*self).verbs[(*self).verbCount] = (uint8_t) SHAPE_VERB_CUBIC;
    (*self).verbCount++;
    shapeExpandBounds(self, x1, y1, first);
    shapeExpandBounds(self, x2, y2, false);
    shapeExpandBounds(self, x3, y3, false);
}

void Shape_close(Shape *self) {
    if (!self)
        return;
    if (!shapeEnsureVerbCapacity(self, 1))
        return;
    (*self).verbs[(*self).verbCount] = (uint8_t) SHAPE_VERB_CLOSE;
    (*self).verbCount++;
    (*self).closed = true;
}

void Shape_reset(Shape *self) {
    if (!self)
        return;
    (*self).pointCount = 0;
    (*self).verbCount = 0;
    (*self).bounds[0] = 0.0f;
    (*self).bounds[1] = 0.0f;
    (*self).bounds[2] = 0.0f;
    (*self).bounds[3] = 0.0f;
    (*self).closed = false;
}

bool Shape_fromSvg(const char *pathStr, Shape *dest) {
    if (!pathStr || !dest)
        return false;
    Shape_reset(dest);
    const char *p = pathStr;
    float curX = 0.0f;
    float curY = 0.0f;
    float startX = 0.0f;
    float startY = 0.0f;
    char cmd = '\0';

    while (*p) {
        p = shapeSkipSvgSpaces(p);
        if (!*p)
            break;

        char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            cmd = c;
            p++;
        }

        if (cmd == 'M' || cmd == 'm') {
            float x = 0.0f;
            float y = 0.0f;
            if (!shapeParseSvgFloat(&p, &x) || !shapeParseSvgFloat(&p, &y))
                return false;
            if (cmd == 'm') {
                curX += x;
                curY += y;
            } else {
                curX = x;
                curY = y;
            }
            startX = curX;
            startY = curY;
            Shape_moveTo(dest, curX, curY);
            cmd = (cmd == 'm') ? 'l' : 'L';
        } else if (cmd == 'L' || cmd == 'l') {
            float x = 0.0f;
            float y = 0.0f;
            if (!shapeParseSvgFloat(&p, &x) || !shapeParseSvgFloat(&p, &y))
                return false;
            if (cmd == 'l') {
                curX += x;
                curY += y;
            } else {
                curX = x;
                curY = y;
            }
            Shape_lineTo(dest, curX, curY);
        } else if (cmd == 'H' || cmd == 'h') {
            float x = 0.0f;
            if (!shapeParseSvgFloat(&p, &x))
                return false;
            if (cmd == 'h')
                curX += x;
            else
                curX = x;
            Shape_lineTo(dest, curX, curY);
        } else if (cmd == 'V' || cmd == 'v') {
            float y = 0.0f;
            if (!shapeParseSvgFloat(&p, &y))
                return false;
            if (cmd == 'v')
                curY += y;
            else
                curY = y;
            Shape_lineTo(dest, curX, curY);
        } else if (cmd == 'C' || cmd == 'c') {
            float x1 = 0.0f;
            float y1 = 0.0f;
            float x2 = 0.0f;
            float y2 = 0.0f;
            float x = 0.0f;
            float y = 0.0f;
            if (!shapeParseSvgFloat(&p, &x1) || !shapeParseSvgFloat(&p, &y1) ||
                !shapeParseSvgFloat(&p, &x2) || !shapeParseSvgFloat(&p, &y2) ||
                !shapeParseSvgFloat(&p, &x) || !shapeParseSvgFloat(&p, &y))
                return false;
            if (cmd == 'c') {
                Shape_cubicTo(dest, curX + x1, curY + y1, curX + x2, curY + y2, curX + x, curY + y);
                curX += x;
                curY += y;
            } else {
                Shape_cubicTo(dest, x1, y1, x2, y2, x, y);
                curX = x;
                curY = y;
            }
        } else if (cmd == 'Z' || cmd == 'z') {
            Shape_close(dest);
            curX = startX;
            curY = startY;
            cmd = '\0';
        } else {
            return false;
        }
    }
    return (*dest).verbCount > 0;
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void Shape_setClosed(Shape *self, bool closed) {
    if (!self)
        return;
    (*self).closed = closed;
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
bool Shape_isClosed(const Shape *self) {
    return self ? (*self).closed : false;
}

;;GETTER
uint64_t Shape_getTypeId(const Shape *self) {
    return self ? (*self).typeId : 0;
}

;;GETTER
size_t Shape_getPointCount(const Shape *self) {
    return self ? (*self).pointCount : 0;
}

;;GETTER
size_t Shape_getVerbCount(const Shape *self) {
    return self ? (*self).verbCount : 0;
}

;;GETTER
void Shape_getBounds(const Shape *self, float *outBounds4) {
    if (!outBounds4)
        return;
    if (!self) {
        outBounds4[0] = 0.0f;
        outBounds4[1] = 0.0f;
        outBounds4[2] = 0.0f;
        outBounds4[3] = 0.0f;
        return;
    }
    outBounds4[0] = (*self).bounds[0];
    outBounds4[1] = (*self).bounds[1];
    outBounds4[2] = (*self).bounds[2];
    outBounds4[3] = (*self).bounds[3];
}
