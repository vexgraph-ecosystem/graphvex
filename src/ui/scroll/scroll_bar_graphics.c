#include "lang/scroll_bar_graphics.h"
#include "lang/str.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "lang/brush.h"
#include "lang/graphics.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ScrollBarGraphics
 * ============================================================================
 * The ScrollBar graphics holder (R3): a dumb track + thumb primitive. Given a
 * viewport rect and a 0..1 value fraction, it docks a track (right edge for
 * vertical, bottom edge for horizontal), derives the thumb from the
 * viewport/content ratio floored by the grippable minimum, and draws two
 * quads through the active Graphics row. No widget state, no input, no tree —
 * the R4 darling ScrollBar owns behavior and feeds this holder.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ScrollBarGraphics (ui/scroll/scroll_bar_graphics.c)
 * LEVEL: L2 — Behavior (the ScrollBar track + thumb graphics primitive)
 * ============================================================================
 * SUMMARY:
 *   Docks a track inside a viewport and draws track + thumb quads. Pure
 *   geometry + two Graphics_fillRect calls; no state beyond its own fields.
 *
 * STRUCT FIELDS (Mirroring lang/scroll_bar_graphics.h):
 * ----------------------------------------------------------------------------
 *   int orientation;      // SCROLL_GRAPHICS_VERTICAL / _HORIZONTAL
 *   float thickness;      // Cross-axis extent (bar width / bar height)
 *   float inset;          // Edge inset from the docked corner
 *   float fraction;       // Thumb position along the track (0..1)
 *   float viewLen;        // Viewport length along the bar
 *   float contentLen;     // Content length along the bar
 *   float thumbMin;       // Minimum thumb extent in px
 *   float shortLimit;     // Minimum thumb share of the track (0..1)
 *   float opacity;        // 0..1
 *   bool visible;         // False = paint nothing
 *   uint32_t trackColor;  // 0xRRGGBBAA
 *   uint32_t thumbColor;  // 0xRRGGBBAA
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   pin01(t) : clamp to 0..1
 *   thumbDims(trackLen, g, outPos, outLen) : fraction + floored thumb length
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - ScrollBarGraphics_0 / ScrollBarGraphics_init / ScrollBarGraphics_free
 *
 * Public Core Functions: (.h)
 *   - ScrollBarGraphics_trackRect / ScrollBarGraphics_thumbRect / ScrollBarGraphics_paint
 *
 * Public toString: (.h)
 *   - ScrollBarGraphics_toString / ScrollBarGraphics_toStringStruct
 * ============================================================================
 */

// PRIVATE HELPERS

static float pin01(float t) {
    if (t < 0.0f)
        return 0.0f;
    if (t > 1.0f)
        return 1.0f;
    return t;
}

// Thumb position (px from the track start) + length (px): the fraction along
// the value span, and the viewport/content ratio floored by the grippable
// minimum (px thumbMin vs the shortLimit share of the track).
static void thumbDims(float trackLen, const ScrollBarGraphics *g, float *outPos, float *outLen) {
    float frac = pin01((*g).fraction);
    float ratio = (*g).contentLen > 0.0f ? (*g).viewLen / (*g).contentLen : 1.0f;
    ratio = pin01(ratio);
    float floor = (*g).shortLimit * trackLen;
    float minLen = (*g).thumbMin > floor ? (*g).thumbMin : floor;
    float len = ratio * trackLen;
    if (len < minLen)
        len = minLen;
    if (len > trackLen)
        len = trackLen;
    if (outPos)
        *outPos = frac * (trackLen - len);
    if (outLen)
        *outLen = len;
}

// CONSTRUCTORS (PUBLIC & PRIVATE)

void ScrollBarGraphics_init(ScrollBarGraphics *g) {
    if (!g)
        return;
    (*g).orientation = SCROLL_GRAPHICS_VERTICAL;
    (*g).thickness = 12.0f;
    (*g).inset = 2.0f;
    (*g).fraction = 0.0f;
    (*g).viewLen = 0.0f;
    (*g).contentLen = 0.0f;
    (*g).thumbMin = 24.0f;
    (*g).shortLimit = 0.0f;
    (*g).opacity = 1.0f;
    (*g).visible = true;
    (*g).trackColor = SCROLL_GRAPHICS_TRACK_COLOR;
    (*g).thumbColor = SCROLL_GRAPHICS_THUMB_COLOR;
}

ScrollBarGraphics *ScrollBarGraphics_0(void) {
    ScrollBarGraphics *g = (ScrollBarGraphics*) calloc(1, sizeof(ScrollBarGraphics));
    if (!g)
        return nullptr;
    ScrollBarGraphics_init(g);
    return g;
}

void ScrollBarGraphics_free(ScrollBarGraphics *g) {
    free(g);
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

void ScrollBarGraphics_trackRect(const ScrollBarGraphics *g, const Rectangle *viewportRect, Rectangle *dest) {
    if (!dest)
        return;
    (*dest).x = 0.0f;
    (*dest).y = 0.0f;
    (*dest).width = 0.0f;
    (*dest).height = 0.0f;
    if (!g || !viewportRect)
        return;
    float rx = (*viewportRect).x;
    float ry = (*viewportRect).y;
    float rw = (*viewportRect).width;
    float rh = (*viewportRect).height;
    float t = (*g).thickness;
    float inset = (*g).inset;
    if ((*g).orientation == SCROLL_GRAPHICS_HORIZONTAL) {
        (*dest).x = rx + inset;
        (*dest).y = ry + rh - inset - t;
        (*dest).width = rw - 2.0f * inset;
        (*dest).height = t;
    } else {
        (*dest).x = rx + rw - inset - t;
        (*dest).y = ry + inset;
        (*dest).width = t;
        (*dest).height = rh - 2.0f * inset;
    }
}

void ScrollBarGraphics_thumbRect(const ScrollBarGraphics *g, const Rectangle *viewportRect, Rectangle *dest) {
    if (!dest)
        return;
    (*dest).x = 0.0f;
    (*dest).y = 0.0f;
    (*dest).width = 0.0f;
    (*dest).height = 0.0f;
    if (!g || !viewportRect)
        return;
    Rectangle track;
    ScrollBarGraphics_trackRect(g, viewportRect, &track);
    float tx = track.x;
    float ty = track.y;
    float tw = track.width;
    float th = track.height;
    bool horizontal = (*g).orientation == SCROLL_GRAPHICS_HORIZONTAL;
    float pos = 0.0f, len = 0.0f;
    if (horizontal) {
        thumbDims(tw, g, &pos, &len);
        (*dest).x = tx + pos;
        (*dest).y = ty;
        (*dest).width = len;
        (*dest).height = th;
    } else {
        thumbDims(th, g, &pos, &len);
        (*dest).x = tx;
        (*dest).y = ty + pos;
        (*dest).width = tw;
        (*dest).height = len;
    }
}

bool ScrollBarGraphics_paint(const ScrollBarGraphics *g, const Rectangle *viewportRect) {
    if (!g || !viewportRect)
        return false;
    if ((*viewportRect).width <= 0.0f || (*viewportRect).height <= 0.0f)
        return false;
    if (!(*g).visible)
        return false;
    if ((*g).opacity <= 0.0f)
        return false;
    if ((*g).contentLen <= (*g).viewLen)
        return false;
    Rectangle track;
    ScrollBarGraphics_trackRect(g, viewportRect, &track);
    if (track.width <= 0.0f || track.height <= 0.0f)
        return false;
    Brush trackBrush = { (*g).trackColor, (*g).opacity };
    if (!Graphics_fillRect(&track, &trackBrush))
        return false;
    Rectangle thumb;
    ScrollBarGraphics_thumbRect(g, viewportRect, &thumb);
    Brush thumbBrush = { (*g).thumbColor, (*g).opacity };
    return Graphics_fillRect(&thumb, &thumbBrush);
}

// TOSTRING (PUBLIC)

void ScrollBarGraphics_toString(const ScrollBarGraphics *self, char *dest, size_t cap, bool *outTruncated) {
    Str s;
    Str_init(&s, dest, cap);
    if (self == nullptr) {
        Str_put(&s, "nullptr");
        if (outTruncated) *outTruncated = false;
        return;
    }
    Str_printf(&s, "ScrollBarGraphics[%s fraction=%.2f view=%.0f content=%.0f thickness=%.1f opacity=%.2f]",
               (*self).orientation == SCROLL_GRAPHICS_HORIZONTAL ? "horizontal" : "vertical",
               (*self).fraction, (*self).viewLen, (*self).contentLen,
               (*self).thickness, (*self).opacity);
    if (outTruncated) *outTruncated = Str_isTruncated(&s);
}

void ScrollBarGraphics_toStringStruct(const ScrollBarGraphics *self, char *dest, size_t cap, bool *outTruncated) {
    Str s;
    Str_init(&s, dest, cap);
    if (self == nullptr) {
        Str_put(&s, "nullptr");
        if (outTruncated) *outTruncated = false;
        return;
    }
    Str_put(&s, "ScrollBarGraphics { ");
    Str_printf(&s, "orientation: %d, thickness: %.1f, inset: %.1f, fraction: %.2f, ",
               (*self).orientation, (*self).thickness, (*self).inset, (*self).fraction);
    Str_printf(&s, "viewLen: %.0f, contentLen: %.0f, thumbMin: %.1f, shortLimit: %.2f, ",
               (*self).viewLen, (*self).contentLen, (*self).thumbMin, (*self).shortLimit);
    Str_printf(&s, "opacity: %.2f, visible: %s }",
               (*self).opacity, (*self).visible ? "true" : "false");
    if (outTruncated) *outTruncated = Str_isTruncated(&s);
}
