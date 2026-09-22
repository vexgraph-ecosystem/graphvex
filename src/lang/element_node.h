#ifndef LANG_ELEMENT_NODE_H
#define LANG_ELEMENT_NODE_H

#include <stdbool.h>
#include <stdint.h>

// lang/element_node.h — the child container (a flat array of Component*).
//
// An ElementNode owns a flat array of child Component POINTERS — no slicing, so
// a child can be any element kind (Panel, Label, ListPane, …) and its `type`
// tells you which. This is the Data-Oriented Storage Law: one node, N children,
// contiguous slots.
//
// The node is public data so a Component can embed it by value. The child type
// (Component) is forward-declared here and completed in lang/component.h — so a
// Component embeds an ElementNode, and an ElementNode holds Component*.
//
// Layout is the ABSOLUTE layout: ElementNode_layout feeds every child the node's
// own absolute box, lets the child resolve its dials, then RECURSES into that
// child's own children — so a whole element tree resolves from one call at the
// root.

typedef struct Component Component;   // completed in lang/component.h

typedef struct ElementNode {
    Component **items;   // OWNED flat array of child Component pointers
    uint32_t count;      // active children
    uint32_t cap;        // array capacity (doubling)
} ElementNode;

// --- Constructors ---
void ElementNode_init(ElementNode *node);     // zero an embedded node
void ElementNode_free(ElementNode *node);     // free the array (embedded-safe)
bool ElementNode_reserve(ElementNode *node, uint32_t capacity);

// --- Core functions ---
// Append a child pointer (the node does NOT own the child). Grows by doubling
// (the Dynamic Scalability & Anti-Hardcoding Law). Returns false on null/OOM.
bool ElementNode_add(ElementNode *node, Component *child);
bool ElementNode_remove(ElementNode *node, uint32_t index);

// Resolve the whole subtree against the node's absolute box (the ABSOLUTE
// layout). Defined in component.c (it needs Component complete).
void ElementNode_layout(ElementNode *node, float px, float py, float pw, float ph);

// Topmost visible child whose abs AABB contains the point (highest z wins; ties
// go to the later child). Returns UINT32_MAX when nothing is hit.
uint32_t ElementNode_hitTest(const ElementNode *node, float pointX, float pointY);

// --- Getters (the Symmetric Getter/Setter Completeness Law: null-safe) ---
Component *ElementNode_getChildren(const ElementNode *node, uint32_t index);
uint32_t ElementNode_count(const ElementNode *node);
bool ElementNode_isValid(const ElementNode *node);

#endif // LANG_ELEMENT_NODE_H
