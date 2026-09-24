#ifndef LANG_SCROLL_PANEL_GRAPHICS_H
#define LANG_SCROLL_PANEL_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/rect/rectangle.h"
#include "lang/scroll_bar_graphics.h"

// lang/scroll_panel_graphics.h — the ScrollPanel graphics holder (R3).
//
// The scroll chrome: a viewport background fill plus the two owned scroll
// bars (horizontal docked bottom, vertical docked right). It wires the two
// ScrollBarGraphics holders together — the R4 darling ScrollPanel owns the
// tree, offsets, and behavior, then hands this holder the rect to paint.

typedef struct ScrollPanelGraphics {
    ScrollBarGraphics hBar;      // Horizontal bar graphics (bottom-docked)
    ScrollBarGraphics vBar;      // Vertical bar graphics (right-docked)
    uint32_t backgroundColor;    // Viewport fill (0xRRGGBBAA; alpha 0 = skip)
} ScrollPanelGraphics;

// Constructors: ScrollPanelGraphics() / ScrollPanelGraphics_zero().
ScrollPanelGraphics *ScrollPanelGraphics_0(void);
void ScrollPanelGraphics_init(ScrollPanelGraphics *g);
void ScrollPanelGraphics_free(ScrollPanelGraphics *g);

// Paint the viewport background, then both bars, into panelRect. False when
// nothing drew (no background and neither bar visible).
bool ScrollPanelGraphics_paint(const ScrollPanelGraphics *g, const Rectangle *panelRect);

// --- toString Law (bounded, cold-path) ---
void ScrollPanelGraphics_toString(const ScrollPanelGraphics *self, char *dest, size_t cap, bool *outTruncated);
void ScrollPanelGraphics_toStringStruct(const ScrollPanelGraphics *self, char *dest, size_t cap, bool *outTruncated);

#endif // LANG_SCROLL_PANEL_GRAPHICS_H
