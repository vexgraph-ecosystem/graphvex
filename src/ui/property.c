#include "ui/property.h"

// graphvex R3 — ui/property.c
// A Property is inert data; the default matches the element defaults (an opaque
// white body, no border or shadow) so adopting one changes nothing.

Property Property_default(void) {
    Property p = {0};
    p.background = COLOR_WHITE;
    return p;
}
