#ifndef VECTOR_SHAPE_H
#define VECTOR_SHAPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "../graphics/type.h"

// vector/shape.h — Pure vector path geometry API.
//
// Builds and manipulates 2D vector paths using move, line, and cubic Bézier
// segments. Computes bounding boxes, supports SVG path parsing, and procedural
// primitives (rect, circle) without raster dependencies.

// Path verb codes (stored in the verbs array; mirror the enum inside
// shape.c — consumers must use these, never re-declare them).
#define SHAPE_VERB_MOVE  0u
#define SHAPE_VERB_LINE  1u
#define SHAPE_VERB_CUBIC 2u
#define SHAPE_VERB_CLOSE 3u

typedef struct Shape {
    float *points;        // dynamic array of (x, y) coordinates
    uint8_t *verbs;       // dynamic array of ShapeVerb
    size_t pointCount;
    size_t pointCapacity;
    size_t verbCount;
    size_t verbCapacity;
    float bounds[4];      // minX, minY, maxX, maxY
    bool closed;
    uint64_t typeId;
} Shape;

// Constructors
Shape *Shape_0(void);
Shape *Shape_4(float x, float y, float w, float h);

// Core functions
void Shape_free(Shape *self);
void Shape_rect(Shape *dest, float x, float y, float w, float h);
void Shape_circle(Shape *dest, float cx, float cy, float r);
void Shape_moveTo(Shape *self, float x, float y);
void Shape_lineTo(Shape *self, float x, float y);
void Shape_cubicTo(Shape *self, float x1, float y1, float x2, float y2, float x3, float y3);
void Shape_close(Shape *self);
void Shape_reset(Shape *self);
bool Shape_fromSvg(const char *pathStr, Shape *dest);

// Symmetric Setters
void Shape_setClosed(Shape *self, bool closed);

// Symmetric Getters
bool Shape_isClosed(const Shape *self);
uint64_t Shape_getTypeId(const Shape *self);
size_t Shape_getPointCount(const Shape *self);
size_t Shape_getVerbCount(const Shape *self);
void Shape_getBounds(const Shape *self, float *outBounds4);

#define Shape(...) CONSTRUCTOR_DISPATCH(Shape, __VA_ARGS__)

#endif
