#ifndef LANG_SCROLL_BAR_GRAPHICS_H
#define LANG_SCROLL_BAR_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/rect/rectangle.h"

// lang/scroll_bar_graphics.h — the ScrollBar graphics holder (R3).
//
// The dumb track + thumb primitive: given a viewport rect and a 0..1 value
// fraction, it docks a track (right edge for vertical, bottom edge for
// horizontal), derives the thumb from the viewport/content ratio floored by
// the grippable minimum, and draws two quads through the active Graphics
// row. No widget state, no input, no layout tree — the R4 darling ScrollBar
// owns behavior (value, mode, friction, drag) and feeds this holder. Same
// layer split as Label (lang/label.h + ui/label) and Panel.

#define SCROLL_GRAPHICS_VERTICAL    0
#define SCROLL_GRAPHICS_HORIZONTAL  1

#define SCROLL_GRAPHICS_TRACK_COLOR 0xFFFFFF2Eu
#define SCROLL_GRAPHICS_THUMB_COLOR 0xFFFFFFB3u

typedef struct ScrollBarGraphics {
    int orientation;      // SCROLL_GRAPHICS_VERTICAL / _HORIZONTAL
    float thickness;      // Cross-axis extent (bar width / bar height)
    float inset;          // Edge inset from the docked corner
    float fraction;       // Thumb position along the track (0..1)
    float viewLen;        // Viewport length along the bar
    float contentLen;     // Content length along the bar
    float thumbMin;       // Minimum thumb extent in px
    float shortLimit;     // Minimum thumb share of the track (0..1)
    float opacity;        // 0..1
    bool visible;         // False = paint nothing
    uint32_t trackColor;  // 0xRRGGBBAA
    uint32_t thumbColor;  // 0xRRGGBBAA
} ScrollBarGraphics;

// Constructors: ScrollBarGraphics() / ScrollBarGraphics_zero().
ScrollBarGraphics *ScrollBarGraphics_0(void);
void ScrollBarGraphics_init(ScrollBarGraphics *g);
void ScrollBarGraphics_free(ScrollBarGraphics *g);

// Geometry (dest-last): the docked track rect and the thumb rect, both
// inside the viewport rect.
void ScrollBarGraphics_trackRect(const ScrollBarGraphics *g, const Rectangle *viewportRect, Rectangle *dest);
void ScrollBarGraphics_thumbRect(const ScrollBarGraphics *g, const Rectangle *viewportRect, Rectangle *dest);

// Paint track + thumb into the viewport. False when hidden, transparent,
// nothing to scroll (content fits the viewport), or a hostile rect.
bool ScrollBarGraphics_paint(const ScrollBarGraphics *g, const Rectangle *viewportRect);

// --- toString Law (bounded, cold-path) ---
void ScrollBarGraphics_toString(const ScrollBarGraphics *self, char *dest, size_t cap, bool *outTruncated);
void ScrollBarGraphics_toStringStruct(const ScrollBarGraphics *self, char *dest, size_t cap, bool *outTruncated);

#endif // LANG_SCROLL_BAR_GRAPHICS_H
