#include "lang/component.h"
#include "lang/str.h"

#include <stdlib.h>
#include <string.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Component
 * ============================================================================
 * The element base — the interface every element shares. A Component carries the
 * identity (name, type), the tree link (parent), its graphics parts (1..N
 * GraphicsComponents), and its children (an embedded ElementNode of Component*).
 *
 * Every element is Component-FIRST, so `(Component*) x` is the universal handle
 * and `type` says which element it really is. `Element_add` is the ONE additive
 * verb; the parent owns the children it adds (freed with the parent, recursively,
 * the Teardown Order Law).
 *
 * The arity surface is the Arity and Constructive Convenience Law: arity
 * constructors (Component_0/1/2 + the Component(...) chooser), Component_zero(),
 * and Element_add() as the additive verb.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Component (component/component.c)
 * LEVEL: L2 — Behavior (element identity + tree + graphics + children)
 * ============================================================================
 * SUMMARY:
 *   The element base. Owns its graphics parts and its children (Component*).
 *   Element_add sets the child's parent and appends it.
 *
 * STRUCT FIELDS (Mirroring lang/component.h):
 * ----------------------------------------------------------------------------
 *   char name[64];               // element name
 *   uint64_t type;               // type.c identity
 *   Component *parent;           // the parent element (null at root)
 *   GraphicsComponent *graphics; // OWNED 1..N graphics parts
 *   uint32_t graphicsCount;
 *   ElementNode children;        // OWNED child list (borrowed Component*)
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   graphicsGrow(component) : double the graphics array (Anti-Hardcoding Law)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Component_0(void) / Component_1(name) / Component_2(name, type)
 *   - Component_zero(void) / Component_free(component)
 *
 * Public Core Functions: (.h)
 *   - Element_add(parent, child)
 *   - Component_graphics(component, index) / Component_addGraphics(component, graphics)
 *   - Component_graphicsCount(component) / Component_layout(component, px, py, pw, ph)
 *
 * Private Core Functions: (.c static)
 *   - graphicsGrow(component)
 *
 * Public Setters: (.h)
 *   - Component_setName/Type/Parent(component, ...)
 *
 * Public Getters: (.h)
 *   - Component_getName/Type/Parent(component) / Component_getChildren(component)
 *   - Component_childCount(component) / Component_isValid(component)
 * ============================================================================
 */

static bool graphicsGrow(Component *component) {
    if ((*component).graphicsCount < 0u)
        return false;
    uint32_t newCap = ((*component).graphicsCount == 0) ? 1 : (*component).graphicsCount * 2;
    GraphicsComponent *nb = (GraphicsComponent*) realloc((void*) (*component).graphics,
                                                         (size_t) newCap * sizeof(*nb));
    if (nb == nullptr)
        return false;
    (*component).graphics = nb;
    return true;
}

// CONSTRUCTORS (PUBLIC & PRIVATE)

void Component_init(Component *component) {
    if (component == nullptr)
        return;
    (*component).name[0] = '\0';
    (*component).type = 0;
    (*component).parent = nullptr;
    (*component).graphics = nullptr;
    (*component).graphicsCount = 0;
    ElementNode_init(&(*component).children);
}

Component *Component_0(void) {
    Component *component = (Component*) calloc(1, sizeof(Component));
    if (component == nullptr)
        return nullptr;
    Component_init(component);
    return component;
}

Component *Component_1(const char *name) {
    Component *component = Component_0();
    if (component != nullptr)
        Component_setName(component, name);
    return component;
}

Component *Component_2(const char *name, uint64_t type) {
    Component *component = Component_1(name);
    if (component != nullptr)
        (*component).type = type;
    return component;
}

Component *Component_zero(void) {
    return Component_0();
}

void Component_destroy(Component *component) {
    if (component == nullptr)
        return;
    // Children first (the Teardown Order Law), recursively.
    for (uint32_t i = 0; i < (*component).children.count; i++)
        Component_free((*component).children.items[i]);
    ElementNode_free(&(*component).children);
    free((void*) (*component).graphics);
    (*component).graphics = nullptr;
    (*component).graphicsCount = 0;
}

void Component_free(Component *component) {
    if (component == nullptr)
        return;
    Component_destroy(component);
    free(component);
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

bool Element_add(Component *parent, Component *child) {
    if (parent == nullptr || child == nullptr)
        return false;
    (*child).parent = parent;                              // the tree link
    return ElementNode_add(&(*parent).children, child);    // the parent owns the child
}

GraphicsComponent *Component_graphics(Component *component, uint32_t index) {
    if (component == nullptr || index >= (*component).graphicsCount)
        return nullptr;
    return &(*component).graphics[index];
}

uint32_t Component_addGraphics(Component *component, const GraphicsComponent *graphics) {
    if (component == nullptr || graphics == nullptr)
        return UINT32_MAX;
    if (!graphicsGrow(component))
        return UINT32_MAX;
    uint32_t index = (*component).graphicsCount;
    (*component).graphics[index] = *graphics;
    (*component).graphicsCount = index + 1;
    return index;
}

uint32_t Component_graphicsCount(const Component *component) {
    return component ? (*component).graphicsCount : 0u;
}

void Component_layout(Component *component, float px, float py, float pw, float ph) {
    if (component == nullptr)
        return;
    GraphicsComponent *primary = Component_graphics(component, 0);
    if (primary == nullptr)
        return;
    // THE BOARD DIMENSION OVERRIDE (the Window Board Root Lock Law): the
    // component's OWN box IS the layout box. This is the ONLY override in the
    // system — only the window's scenepanel and contentpanel are overriders;
    // every other component resolves by its own origin/anchor/pivot dials.
    // Force TOP_LEFT placement so abs == (px,py).
    GraphicsComponent_setAnchor(primary, GRAPHICS_COMPONENT_ANCHOR_TOP_LEFT);
    GraphicsComponent_setPivot(primary, GRAPHICS_COMPONENT_PIVOT_TOP_LEFT);
    GraphicsComponent_setOrigin(primary, GRAPHICS_COMPONENT_ORIGIN_TOP_LEFT);
    GraphicsComponent_setLocation(primary, px, py);
    GraphicsComponent_setSize(primary, pw, ph);
    GraphicsComponent_setParentAbs(primary, 0.0f, 0.0f, 0.0f, 0.0f);
    // Children resolve against this component's abs box.
    ElementNode_layout(&(*component).children,
                       (*primary).absX, (*primary).absY, (*primary).absW, (*primary).absH);
}

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void Component_setName(Component *component, const char *name) {
    if (component == nullptr)
        return;
    if (name == nullptr) {
        (*component).name[0] = '\0';
        return;
    }
    size_t n = strlen(name);
    if (n >= sizeof((*component).name))
        n = sizeof((*component).name) - 1u;
    memcpy((*component).name, name, n);
    (*component).name[n] = '\0';
}

;;SETTER
void Component_setType(Component *component, uint64_t type) {
    if (component == nullptr)
        return;
    (*component).type = type;
}

;;SETTER
void Component_setParent(Component *component, Component *parent) {
    if (component == nullptr)
        return;
    (*component).parent = parent;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
const char *Component_getName(const Component *component) {
    return component ? (*component).name : "";
}

;;GETTER
uint64_t Component_getType(const Component *component) {
    return component ? (*component).type : 0u;
}

;;GETTER
Component *Component_getParent(const Component *component) {
    return component ? (*component).parent : nullptr;
}

;;GETTER
ElementNode *Component_getChildren(Component *component) {
    return component ? &(*component).children : nullptr;
}

;;GETTER
uint32_t Component_childCount(const Component *component) {
    return component ? (*component).children.count : 0u;
}

;;GETTER
bool Component_isValid(const Component *component) {
    return component != nullptr;
}

// --- toString Law (bounded, cold-path; ONE LAYER) ---

void Component_toString(const Component *self, char *dest, size_t cap, bool *outTruncated) {
    Str s;
    Str_init(&s, dest, cap);
    if (self == nullptr) {
        Str_put(&s, "nullptr");
        if (outTruncated) *outTruncated = false;
        return;
    }
    Str_printf(&s, "Component(\"%s\")", (*self).name);
    if (outTruncated) *outTruncated = Str_isTruncated(&s);
}

void Component_toStringStruct(const Component *self, char *dest, size_t cap, bool *outTruncated) {
    Str s;
    Str_init(&s, dest, cap);
    if (self == nullptr) {
        Str_put(&s, "nullptr");
        if (outTruncated) *outTruncated = false;
        return;
    }
    // ONE LAYER: this component's own fields only. A parent renders by name
    // (never recursing); children render as a COUNT (never recursing).
    Str_put(&s, "Component { ");
    Str_put(&s, "name: ");
    Str_putQuoted(&s, (*self).name);
    Str_printf(&s, ", type: 0x%llX, ", (unsigned long long) (*self).type);
    Str_put(&s, "parent: ");
    if ((*self).parent != nullptr)
        Str_putQuoted(&s, (*(*self).parent).name);
    else
        Str_put(&s, "nullptr");
    Str_printf(&s, ", graphics: %u, children: %u }",
               (*self).graphicsCount, (*self).children.count);
    if (outTruncated) *outTruncated = Str_isTruncated(&s);
}
