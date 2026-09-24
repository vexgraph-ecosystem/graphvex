#include "lang/scroll_panel_graphics.h"
#include "lang/str.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "lang/brush.h"
#include "lang/graphics.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ScrollPanelGraphics
 * ============================================================================
 * The ScrollPanel graphics holder (R3): the scroll chrome — a viewport
 * background fill plus the two owned scroll bars (horizontal docked bottom,
 * vertical docked right). It wires the two ScrollBarGraphics holders
 * together. The R4 darling ScrollPanel owns the tree, offsets, and behavior,
 * then hands this holder the rect to paint.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ScrollPanelGraphics (ui/scroll/scroll_panel_graphics.c)
 * LEVEL: L2 — Behavior (the ScrollPanel chrome: background + two bars)
 * ============================================================================
 * SUMMARY:
 *   Fills the viewport background (when its alpha is set) then paints the
 *   two ScrollBarGraphics bars docked inside the rect. Pure composition.
 *
 * STRUCT FIELDS (Mirroring lang/scroll_panel_graphics.h):
 * ----------------------------------------------------------------------------
 *   ScrollBarGraphics hBar;    // Horizontal bar graphics (bottom-docked)
 *   ScrollBarGraphics vBar;    // Vertical bar graphics (right-docked)
 *   uint32_t backgroundColor;  // Viewport fill (alpha 0 = skip)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - ScrollPanelGraphics_0 / ScrollPanelGraphics_init / ScrollPanelGraphics_free
 *
 * Public Core Functions: (.h)
 *   - ScrollPanelGraphics_paint
 *
 * Public toString: (.h)
 *   - ScrollPanelGraphics_toString / ScrollPanelGraphics_toStringStruct
 * ============================================================================
 */

// CONSTRUCTORS (PUBLIC & PRIVATE)

void ScrollPanelGraphics_init(ScrollPanelGraphics *g) {
    if (!g)
        return;
    ScrollBarGraphics_init(&(*g).hBar);
    (*g).hBar.orientation = SCROLL_GRAPHICS_HORIZONTAL;
    ScrollBarGraphics_init(&(*g).vBar);
    (*g).vBar.orientation = SCROLL_GRAPHICS_VERTICAL;
    (*g).backgroundColor = 0x00000000u;
}

ScrollPanelGraphics *ScrollPanelGraphics_0(void) {
    ScrollPanelGraphics *g = (ScrollPanelGraphics*) calloc(1, sizeof(ScrollPanelGraphics));
    if (!g)
        return nullptr;
    ScrollPanelGraphics_init(g);
    return g;
}

void ScrollPanelGraphics_free(ScrollPanelGraphics *g) {
    free(g);
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

bool ScrollPanelGraphics_paint(const ScrollPanelGraphics *g, const Rectangle *panelRect) {
    if (!g || !panelRect)
        return false;
    if ((*panelRect).width <= 0.0f || (*panelRect).height <= 0.0f)
        return false;
    bool drew = false;
    if (((*g).backgroundColor & 0xFFu) != 0u) {
        Brush bg = { (*g).backgroundColor, 1.0f };
        drew = Graphics_fillRect(panelRect, &bg) || drew;
    }
    drew = ScrollBarGraphics_paint(&(*g).hBar, panelRect) || drew;
    drew = ScrollBarGraphics_paint(&(*g).vBar, panelRect) || drew;
    return drew;
}

// TOSTRING (PUBLIC)

void ScrollPanelGraphics_toString(const ScrollPanelGraphics *self, char *dest, size_t cap, bool *outTruncated) {
    Str s;
    Str_init(&s, dest, cap);
    if (self == nullptr) {
        Str_put(&s, "nullptr");
        if (outTruncated) *outTruncated = false;
        return;
    }
    Str_printf(&s, "ScrollPanelGraphics[bg=0x%08X hVisible=%s vVisible=%s]",
               (*self).backgroundColor,
               (*self).hBar.visible ? "true" : "false",
               (*self).vBar.visible ? "true" : "false");
    if (outTruncated) *outTruncated = Str_isTruncated(&s);
}

void ScrollPanelGraphics_toStringStruct(const ScrollPanelGraphics *self, char *dest, size_t cap, bool *outTruncated) {
    Str s;
    Str_init(&s, dest, cap);
    if (self == nullptr) {
        Str_put(&s, "nullptr");
        if (outTruncated) *outTruncated = false;
        return;
    }
    // ONE layer: the nested bars render via their own toString.
    char hbar[192], vbar[192];
    bool hTrunc = false, vTrunc = false;
    ScrollBarGraphics_toString(&(*self).hBar, hbar, sizeof(hbar), &hTrunc);
    ScrollBarGraphics_toString(&(*self).vBar, vbar, sizeof(vbar), &vTrunc);
    Str_put(&s, "ScrollPanelGraphics { ");
    Str_printf(&s, "backgroundColor: 0x%08X, hBar: ", (*self).backgroundColor);
    Str_put(&s, hbar);
    Str_put(&s, ", vBar: ");
    Str_put(&s, vbar);
    Str_put(&s, " }");
    if (outTruncated) *outTruncated = Str_isTruncated(&s) || hTrunc || vTrunc;
}
