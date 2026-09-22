#include "lang/element_painter.h"

#include "lang/brush.h"
#include "lang/graphics.h"
#include "lang/label.h"
#include "lang/rect/rectangle.h"
#include "lang/stroke.h"
#include "annotation/definition.h"
#include "annotation/overview.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ElementPainter
 * ============================================================================
 * The UI paint pass: it walks a Component subtree and draws each graphics part's
 * presentation at its resolved absolute rect, through the unified Graphics table.
 * Placement became pixels once Component_layout resolved the dials; the painter
 * draws WHAT each part looks like (background fill + border).
 *
 * It is a procedural module (no struct): it reads the tree, builds stack-local
 * Brush/Stroke records (zero heap), and calls the Graphics forwarders. The caller
 * owns the target binding and the layout. Rounded corners are deferred until the
 * Graphics table grows a rounded-rect verb.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: ElementPainter (element/element_painter.c)
 * LEVEL: L2 — Behavior (UI paint pass over a Component subtree)
 * ============================================================================
 * SUMMARY:
 *   Paints each graphics part of a component (background + border), then recurses
 *   into its children. No struct, no allocation.
 *
 * STRUCT FIELDS: none — procedural paint pass.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Core Functions: (.h)
 *   - ElementPainter_paint(component)
 *
 * Private Core Functions: (.c static)
 *   - paintComponent(component) : draw one component's parts, then recurse
 * ============================================================================
 */

static void paintComponent(const Component *component) {
    uint32_t parts = Component_graphicsCount(component);
    for (uint32_t i = 0; i < parts; i++) {
        const GraphicsComponent *gc = Component_graphics((Component*) component, i);
        if (gc == nullptr || !GraphicsComponent_isVisible(gc))
            continue;
        float w = (*gc).absW;
        float h = (*gc).absH;
        if (w <= 0.0f || h <= 0.0f)
            continue;
        Rectangle rect = { (*gc).absX, (*gc).absY, w, h };

        // Background fill (skip a fully transparent color).
        if (((*gc).backgroundColor & 0xFFu) != 0u) {
            Brush brush = { (*gc).backgroundColor, (*gc).opacity };
            Graphics_fillRect(&rect, &brush);
        }
        // Border (skip a zero width).
        if ((*gc).borderWidth > 0.0f) {
            Stroke stroke = { (*gc).borderWidth, 0.0f, STROKE_CAP_BUTT, STROKE_JOIN_MITER,
                              (*gc).borderColor };
            Graphics_drawRect(&rect, &stroke);
        }

        // A Label draws its text (type-dispatched; the base has no text).
        // Render first: the label PULLS its bound reactives on this (owner)
        // thread, so the paint pass is where a cross-thread write becomes
        // visible — the text cache is refreshed here, never on a writer thread.
        if (Component_getType(component) == LABEL_TYPE) {
            Label *label = (Label*) component;
            Label_render(label);
            Brush text = { (*label).textColor, 1.0f };
            Graphics_drawText(&rect, (*label).text, &text);
        }
    }

    // Recurse into the children (back-to-front by child order).
    const ElementNode *node = &(*component).children;
    uint32_t count = ElementNode_count(node);
    for (uint32_t i = 0; i < count; i++)
        paintComponent(ElementNode_getChildren(node, i));
}

bool ElementPainter_paint(const Component *component) {
    if (component == nullptr)
        return false;
    const Graphics *g = Graphics_getCurrent();
    if (g == nullptr || (*g).backendId == LANG_BACKEND_NONE)
        return false;
    paintComponent(component);
    return true;
}
