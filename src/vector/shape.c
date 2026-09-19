#include "vector/shape.h"

#include <stdlib.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Shape (vector/shape.c)
 * LEVEL: L2 — Behavior (pure vector path geometry API)
 * ============================================================================
 * Pure vector paths without raster: builds and manipulates 2D Bézier paths
 * represented as dynamic arrays of verbs and coordinate points. Supports
 * moveTo, lineTo, cubicTo, close, procedural primitives (rect, circle),
 * and simple SVG path string parsing ("M x y L x y Z"). The struct rides
 * the vexspoke arena via Memory_alloc(TYPE_SHAPE_SINGLETON) with a calloc
 * fallback for standalone builds; coordinate and verb arrays are owned
 * system memory.
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
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   ShapeVerb (per-verb path command enum — Shape's slot record, no own API):
 *     typedef enum ShapeVerb {
 *         SHAPE_VERB_MOVE = 0,
 *         SHAPE_VERB_LINE = 1,
 *         SHAPE_VERB_CUBIC = 2,
 *         SHAPE_VERB_CLOSE = 3
 *     } ShapeVerb;
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Shape()                                       : Shape_0()
 *   - Shape(x, y, w, h)                             : Shape_4(x, y, w, h)
 *
 * Core Functions:
 *   - Shape_free(self)
 *   - Shape_rect(dest, x, y, w, h)
 *   - Shape_circle(dest, cx, cy, r)
 *   - Shape_moveTo(self, x, y)
 *   - Shape_lineTo(self, x, y)
 *   - Shape_cubicTo(self, x1, y1, x2, y2, x3, y3)
 *   - Shape_close(self)
 *   - Shape_reset(self)
 *   - Shape_fromSvg(pathStr, dest)
 *
 * Setters:
 *   - Shape_setClosed(self, closed)
 *
 * Getters:
 *   - Shape_isClosed(self)
 *   - Shape_getTypeId(self)
 *   - Shape_getPointCount(self)
 *   - Shape_getVerbCount(self)
 *   - Shape_getBounds(self, outBounds4)
 * ============================================================================
 */

// vector/shape.c — Pure vector path geometry implementation.

// Verb codes live in vector/shape.h (SHAPE_VERB_*); consumers such as
// DirectGraphics flatten paths by verb, so the header owns them.

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

// CONSTRUCTORS

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

// CORE FUNCTIONS

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

// SETTERS

void Shape_setClosed(Shape *self, bool closed) {
    if (!self)
        return;
    (*self).closed = closed;
}

// GETTERS

bool Shape_isClosed(const Shape *self) {
    return self ? (*self).closed : false;
}

uint64_t Shape_getTypeId(const Shape *self) {
    return self ? (*self).typeId : 0;
}

size_t Shape_getPointCount(const Shape *self) {
    return self ? (*self).pointCount : 0;
}

size_t Shape_getVerbCount(const Shape *self) {
    return self ? (*self).verbCount : 0;
}

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
