#ifndef LANG_TRANSFORM_H
#define LANG_TRANSFORM_H

#include <stdbool.h>
#include <stddef.h>

// lang/transform.h — the 2D affine render currency.
//
// A Transform is a 2×3 affine matrix:
//
//   | m00  m01  m02 |     x' = m00*x + m01*y + m02
//   | m10  m11  m12 |     y' = m10*x + m11*y + m12
//
// It is what a GraphicsComponent RESOLVES to: the anchor/pivot/origin dials and
// the scale compose into ONE transform per element, and transforms COMPOSE down
// an ElementNode tree (parent × child). Rendering then pushes one transform per
// element instead of re-deriving bare placement at paint time — and it extends
// to rotation/skew without touching callers.
//
// The absolute AABB (the transform applied to the element's [0,w]×[0,h] box)
// stays available for hit-testing and culling, which want a rect, not a matrix.

typedef struct Transform {
    float m00, m01, m02;   // row 0: x' = m00*x + m01*y + m02
    float m10, m11, m12;   // row 1: y' = m10*x + m11*y + m12
} Transform;

// --- Constructors ---
Transform Transform_identity(void);
Transform Transform_translate(float tx, float ty);
Transform Transform_scale(float sx, float sy);

// --- Core functions ---
// dest = parent * child (apply child first, then parent). Dest-last: dest may
// alias neither parent nor child (callers pass a fresh Transform).
void Transform_multiply(const Transform *parent, const Transform *child, Transform *dest);
// Map a point through the transform (dest-last outputs).
void Transform_applyPoint(const Transform *self, float x, float y, float *outX, float *outY);

// --- toString Law (bounded, cold-path) ---
void Transform_toString(const Transform *self, char *dest, size_t cap, bool *outTruncated);
void Transform_toStringStruct(const Transform *self, char *dest, size_t cap, bool *outTruncated);

#endif // LANG_TRANSFORM_H
