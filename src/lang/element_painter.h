#ifndef LANG_ELEMENT_PAINTER_H
#define LANG_ELEMENT_PAINTER_H

#include <stdbool.h>

#include "lang/component.h"

// lang/element_painter.h — paint a Component subtree (the UI paint pass).
//
// Walks a Component: for every graphics part it draws the part's presentation at
// its RESOLVED absolute rect through the unified Graphics table (background fill
// + border), then RECURSES into the component's children — so a whole panel tree
// paints from one call at the root.
//
// CONTRACT: the caller binds the target (Graphics_resize / RasterGraphics_
// setFramebuffer) and lays the tree out (Component_layout) so every graphics
// part's abs rect is resolved, then calls ElementPainter_paint. Paint order is
// the child order (back-to-front by convention); hidden parts are skipped.

bool ElementPainter_paint(const Component *component);

#endif // LANG_ELEMENT_PAINTER_H
