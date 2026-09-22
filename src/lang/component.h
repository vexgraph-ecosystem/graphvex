#ifndef LANG_COMPONENT_H
#define LANG_COMPONENT_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/element_node.h"
#include "lang/graphics_component.h"

// lang/component.h — the element base (the interface every element shares).
//
// A Component is the tree/identity node: a name, a type (the type.c identity), a
// parent link, its graphics parts (1..N GraphicsComponents — a listpane paints N
// rows), and its children (an embedded ElementNode of Component*).
//
// EVERY element is a Component-FIRST struct, so a `Component *` is the universal
// handle: `(Component*) aLabel` is valid with no cast, and `type` tells you which
// element it really is. One `Element_add` adds any element; containers restrict
// with a type-dispatched accepts check, never with an add function per child.
//
// The arity surface (the Arity and Constructive Convenience Law):
//   Component()            -> Component_0()   — empty element
//   Component(name)        -> Component_1()   — named
//   Component(name, type)  -> Component_2()   — named + typed
//   Component_zero()       -> the empty element
//   Element_add(parent, child)                -> the ONE additive verb

typedef struct Component {
    char name[64];                 // element name
    uint64_t type;                 // type.c identity (project-scoped)
    Component *parent;             // the parent element (null at root)
    GraphicsComponent *graphics;   // OWNED 1..N graphics parts (heap array)
    uint32_t graphicsCount;
    ElementNode children;          // OWNED child list (embedded; holds borrowed Component*)
} Component;

// --- Constructors (arity) ---
// Component_init zeroes an EMBEDDED Component (no allocation); the arity
// constructors return a heap Component.
void Component_init(Component *component);
Component *Component_0(void);
Component *Component_1(const char *name);
Component *Component_2(const char *name, uint64_t type);

#define COMPONENT_CHOOSER(_0, _1, _2, NAME, ...) NAME
#define Component(...) COMPONENT_CHOOSER( \
    dummy __VA_OPT__(,) __VA_ARGS__, \
    Component_2, Component_1, Component_0 \
)(__VA_ARGS__)

// The empty element (name "", type 0) — the additive identity.
Component *Component_zero(void);

// Free the component, its graphics parts, and its OWNED children (recursively,
// the Teardown Order Law: children first, then this node). Null-safe.
void Component_free(Component *component);
// Free only the INTERNALS (graphics + children) of an embedded component,
// leaving the struct itself (used by embedded wrappers like Panel).
void Component_destroy(Component *component);

// --- Core functions ---
// The ONE additive verb: sets child->parent, appends to parent->children. The
// parent TAKES OWNERSHIP of the child (freed with the parent). Returns false on
// null/OOM.
bool Element_add(Component *parent, Component *child);

// The component's graphics part at `index` (mutable; null when out of range).
GraphicsComponent *Component_graphics(Component *component, uint32_t index);
// Append a graphics part (copied). Returns its index, or UINT32_MAX on failure.
uint32_t Component_addGraphics(Component *component, const GraphicsComponent *graphics);
uint32_t Component_graphicsCount(const Component *component);

// Resolve this component's subtree against an absolute box — the BOARD DIMENSION
// OVERRIDE (the Window Board Root Lock Law): the component's own box is FORCED
// to (px,py,pw,ph) and its children then resolve by their dials. This is the
// ONLY override in the system: only the window's scenepanel and contentpanel are
// overriders. Every other component resolves by its own origin/anchor/pivot.
void Component_layout(Component *component, float px, float py, float pw, float ph);

// --- Setters / Getters (the Symmetric Getter/Setter Completeness Law: null-safe) ---
void Component_setName(Component *component, const char *name);
const char *Component_getName(const Component *component);
void Component_setType(Component *component, uint64_t type);
uint64_t Component_getType(const Component *component);
void Component_setParent(Component *component, Component *parent);
Component *Component_getParent(const Component *component);
ElementNode *Component_getChildren(Component *component);       // mutable
uint32_t Component_childCount(const Component *component);
bool Component_isValid(const Component *component);

// --- toString Law (bounded, cold-path) ---
void Component_toString(const Component *self, char *dest, size_t cap, bool *outTruncated);
void Component_toStringStruct(const Component *self, char *dest, size_t cap, bool *outTruncated);

#endif // LANG_COMPONENT_H
