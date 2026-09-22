#include "lang/element_node.h"

#include <stdlib.h>

#include "lang/component.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ElementNode
 * ============================================================================
 * The child container: a flat array of Component pointers. One node, N children,
 * contiguous slots (the Data-Oriented Storage Law). A Component embeds one of
 * these as its children; the node holds POINTERS (no slicing), so a child can be
 * any element kind and its `type` says which.
 *
 * ElementNode_layout is the ABSOLUTE layout: each child resolves its primary
 * GraphicsComponent against the node's absolute box, then the child's own
 * children recurse — so the whole tree resolves from one call at the root.
 *
 * The node does NOT own its children (the parent does); it owns only the slot
 * array. Free releases the array, never the children (the Teardown Order Law:
 * the owner frees its children, then the node).
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ElementNode (element/element_node.c)
 * LEVEL: L2 — Behavior (flat container of Component*)
 * ============================================================================
 * SUMMARY:
 *   Owns a flat, doubling Component* array. add appends a borrowed pointer;
 *   layout resolves the whole subtree (absolute); hitTest resolves the topmost
 *   child by z.
 *
 * STRUCT FIELDS (Mirroring lang/element_node.h):
 * ----------------------------------------------------------------------------
 *   Component **items;  // OWNED flat array of child Component pointers
 *   uint32_t count;     // active children
 *   uint32_t cap;       // array capacity (doubling)
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   arrayGrow(node) : double the array (Anti-Hardcoding Law)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - ElementNode_init(node) / ElementNode_free(node) / ElementNode_reserve(node, cap)
 *
 * Public Core Functions: (.h)
 *   - ElementNode_add(node, child) / ElementNode_remove(node, index)
 *   - ElementNode_layout(node, px, py, pw, ph) / ElementNode_hitTest(node, x, y)
 *
 * Public Getters: (.h)
 *   - ElementNode_getChildren(node, index) / ElementNode_count(node) / ElementNode_isValid(node)
 * ============================================================================
 */

static bool arrayGrow(ElementNode *node) {
    if ((*node).count < (*node).cap)
        return true;
    uint32_t newCap = ((*node).cap == 0) ? 4 : (*node).cap * 2;
    Component **nb = (Component**) realloc((void*) (*node).items, (size_t) newCap * sizeof(*nb));
    if (nb == nullptr)
        return false;
    (*node).items = nb;
    (*node).cap = newCap;
    return true;
}

// CONSTRUCTORS (PUBLIC & PRIVATE)

void ElementNode_init(ElementNode *node) {
    if (node == nullptr)
        return;
    (*node).items = nullptr;
    (*node).count = 0;
    (*node).cap = 0;
}

void ElementNode_free(ElementNode *node) {
    if (node == nullptr)
        return;
    free((void*) (*node).items);
    (*node).items = nullptr;
    (*node).count = 0;
    (*node).cap = 0;
}

bool ElementNode_reserve(ElementNode *node, uint32_t capacity) {
    if (node == nullptr)
        return false;
    while ((*node).cap < capacity)
        if (!arrayGrow(node))
            return false;
    return true;
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

bool ElementNode_add(ElementNode *node, Component *child) {
    if (node == nullptr || child == nullptr)
        return false;
    if (!arrayGrow(node))
        return false;
    (*node).items[(*node).count++] = child;
    return true;
}

bool ElementNode_remove(ElementNode *node, uint32_t index) {
    if (node == nullptr || index >= (*node).count)
        return false;
    (*node).items[index] = (*node).items[--(*node).count];
    return true;
}

void ElementNode_layout(ElementNode *node, float px, float py, float pw, float ph) {
    if (node == nullptr)
        return;
    for (uint32_t i = 0; i < (*node).count; i++) {
        Component *child = (*node).items[i];
        if (child == nullptr)
            continue;
        GraphicsComponent *primary = Component_graphics(child, 0);
        if (primary == nullptr)
            continue;
        GraphicsComponent_setParentAbs(primary, px, py, pw, ph);
        // Recurse: the child's own children resolve against the child's abs box.
        ElementNode_layout(&(*child).children,
                           (*primary).absX, (*primary).absY, (*primary).absW, (*primary).absH);
    }
}

uint32_t ElementNode_hitTest(const ElementNode *node, float pointX, float pointY) {
    if (node == nullptr)
        return UINT32_MAX;
    uint32_t best = UINT32_MAX;
    int32_t bestZ = 0;
    for (uint32_t i = 0; i < (*node).count; i++) {
        Component *child = (*node).items[i];
        if (child == nullptr)
            continue;
        GraphicsComponent *primary = Component_graphics(child, 0);
        if (primary == nullptr || !GraphicsComponent_hitTest(primary, pointX, pointY))
            continue;
        if (best == UINT32_MAX || (*primary).z >= bestZ) {
            best = i;
            bestZ = (*primary).z;
        }
    }
    return best;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
Component *ElementNode_getChildren(const ElementNode *node, uint32_t index) {
    if (node == nullptr || index >= (*node).count)
        return nullptr;
    return (*node).items[index];
}

;;GETTER
uint32_t ElementNode_count(const ElementNode *node) {
    return node ? (*node).count : 0u;
}

;;GETTER
bool ElementNode_isValid(const ElementNode *node) {
    return node != nullptr;
}
