#include "lang/transform.h"
#include "lang/str.h"

#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Transform
 * ============================================================================
 * The 2D affine render currency: a 2×3 matrix mapping element-local points to
 * parent points. A GraphicsComponent resolves its anchor/pivot/origin/scale
 * dials into one Transform; ElementNode composes them down the tree. Rendering
 * pushes one transform per element; hit-testing reads the derived absolute AABB.
 *
 * Pure value type — no allocation, no state, every verb returns by value or
 * writes a dest-last out. Ported from nothing (new for the renovation): the old
 * reference resolved bare abs values; this is the matrix form.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Transform (transform/transform.c)
 * LEVEL: L1 — File Metadata (pure 2D affine value type)
 * ============================================================================
 * SUMMARY:
 *   A 2×3 affine matrix and its algebra (identity/translate/scale builders,
 *   multiply, point map). No allocation, no state.
 *
 * STRUCT FIELDS (Mirroring lang/transform.h):
 * ----------------------------------------------------------------------------
 *   float m00, m01, m02;   // row 0: x' = m00*x + m01*y + m02
 *   float m10, m11, m12;   // row 1: y' = m10*x + m11*y + m12
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   (none)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Transform_identity(void) / Transform_translate(tx,ty) / Transform_scale(sx,sy)
 *
 * Public Core Functions: (.h)
 *   - Transform_multiply(parent, child, dest)
 *   - Transform_applyPoint(self, x, y, outX, outY)
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters / Getters: (.h)
 *   - (none — value type; fields are read directly)
 * ============================================================================
 */

Transform Transform_identity(void) {
    Transform t = { .m00 = 1.0f, .m01 = 0.0f, .m02 = 0.0f,
                    .m10 = 0.0f, .m11 = 1.0f, .m12 = 0.0f };
    return t;
}

Transform Transform_translate(float tx, float ty) {
    Transform t = { .m00 = 1.0f, .m01 = 0.0f, .m02 = tx,
                    .m10 = 0.0f, .m11 = 1.0f, .m12 = ty };
    return t;
}

Transform Transform_scale(float sx, float sy) {
    Transform t = { .m00 = sx, .m01 = 0.0f, .m02 = 0.0f,
                    .m10 = 0.0f, .m11 = sy, .m12 = 0.0f };
    return t;
}

void Transform_multiply(const Transform *parent, const Transform *child, Transform *dest) {
    if (parent == nullptr || child == nullptr || dest == nullptr)
        return;
    float m00 = (*parent).m00 * (*child).m00 + (*parent).m01 * (*child).m10;
    float m01 = (*parent).m00 * (*child).m01 + (*parent).m01 * (*child).m11;
    float m02 = (*parent).m00 * (*child).m02 + (*parent).m01 * (*child).m12 + (*parent).m02;
    float m10 = (*parent).m10 * (*child).m00 + (*parent).m11 * (*child).m10;
    float m11 = (*parent).m10 * (*child).m01 + (*parent).m11 * (*child).m11;
    float m12 = (*parent).m10 * (*child).m02 + (*parent).m11 * (*child).m12 + (*parent).m12;
    (*dest).m00 = m00;
    (*dest).m01 = m01;
    (*dest).m02 = m02;
    (*dest).m10 = m10;
    (*dest).m11 = m11;
    (*dest).m12 = m12;
}

void Transform_applyPoint(const Transform *self, float x, float y, float *outX, float *outY) {
    if (self == nullptr) {
        if (outX) *outX = x;
        if (outY) *outY = y;
        return;
    }
    float px = (*self).m00 * x + (*self).m01 * y + (*self).m02;
    float py = (*self).m10 * x + (*self).m11 * y + (*self).m12;
    if (outX) *outX = px;
    if (outY) *outY = py;
}

// --- toString Law (bounded, cold-path) ---

void Transform_toString(const Transform *self, char *dest, size_t cap, bool *outTruncated) {
    Str s;
    Str_init(&s, dest, cap);
    if (self == nullptr) {
        Str_put(&s, "nullptr");
        if (outTruncated) *outTruncated = false;
        return;
    }
    Str_printf(&s, "Transform[%.3f %.3f %.3f; %.3f %.3f %.3f]",
               (*self).m00, (*self).m01, (*self).m02,
               (*self).m10, (*self).m11, (*self).m12);
    if (outTruncated) *outTruncated = Str_isTruncated(&s);
}

void Transform_toStringStruct(const Transform *self, char *dest, size_t cap, bool *outTruncated) {
    Str s;
    Str_init(&s, dest, cap);
    if (self == nullptr) {
        Str_put(&s, "nullptr");
        if (outTruncated) *outTruncated = false;
        return;
    }
    Str_printf(&s, "Transform { m00: %.3f, m01: %.3f, m02: %.3f, m10: %.3f, m11: %.3f, m12: %.3f }",
               (*self).m00, (*self).m01, (*self).m02,
               (*self).m10, (*self).m11, (*self).m12);
    if (outTruncated) *outTruncated = Str_isTruncated(&s);
}
