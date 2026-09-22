#include "scene/scene.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"
#include "draw/drawable.h"
#include "nio/mem.h"
#include "oop/type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Scene
 * ============================================================================
 * Rendered scene representing an aggregated graph of renderable nodes, spatial
 * transforms, material IDs, and camera view-projection matrices. Manages a dynamically
 * growing array of SceneNode slots that borrow Mesh geometry pointers and 4x4 coordinate
 * frames without claiming ownership over external mesh buffers. Maintains camera state
 * as column-major 4x4 view and projection matrices alongside a frame clear color strictly
 * formatted in monotonic 0xRRGGBBAA representation (bits 31..24 red, 23..16 green, 15..8 blue,
 * 7..0 alpha) in accordance with the Strict 0xRRGGBBAA Color Law. Tracks state mutations
 * through a dirty flag that triggers scene invalidation and marks target Drawables as dirty
 * upon rendering.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Scene (scene/scene.c)
 * LEVEL: L3 — Module Code (rendered scene graph behavior)
 * ============================================================================
 * Rendered scene managing mesh nodes, transform matrices, material bindings,
 * camera view and projection matrices, and dispatching to Drawable targets.
 *
 * STRUCT FIELDS (Mirroring scene/scene.h):
 * ----------------------------------------------------------------------------
 *   Scene {
 *     SceneNode *nodes;         // dynamic array of scene mesh nodes
 *     size_t nodeCount;         // active node count
 *     size_t nodeCapacity;      // allocated node storage capacity
 *     float cameraView[16];     // 4x4 view matrix
 *     float cameraProj[16];     // 4x4 projection matrix
 *     uint32_t clearColor;      // scene clear color (0xRRGGBBAA)
 *     bool dirty;               // true when nodes or camera are updated
 *     uint64_t typeId;          // block-header type id (TYPE_GFX_SCENE_SINGLETON)
 *   }
 *
 * SLOT RECORD (dumb entry struct owned exclusively by Scene):
 * ----------------------------------------------------------------------------
 *   SceneNode {
 *     const Mesh *mesh;         // borrowed mesh geometry pointer
 *     float transform[16];      // 4x4 model-to-world transform matrix
 *     uint32_t materialId;      // material shader or pipeline identifier
 *     bool visible;             // visibility toggle for this node
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Scene_0(void)                                        : Allocate default scene
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Scene_free(self)                                     : Release scene storage
 *   - Scene_addMesh(self, mesh, transform16, materialId)   : Add mesh node
 *   - Scene_removeMesh(self, nodeIndex)                    : Remove node by index
 *   - Scene_clear(self)                                    : Clear all scene nodes
 *   - Scene_render(self, drawable)                         : Dispatch to drawable
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - Scene_setCamera(self, view16, proj16)                : Set camera matrices
 *   - Scene_setClearColor(self, clearColor)                : Set clear color (0xRRGGBBAA)
 *   - Scene_setDirty(self, dirty)                          : Set scene dirty flag
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Scene_getNodeCount(self)                             : Query node count
 *   - Scene_getNode(self, index)                           : Query node by index
 *   - Scene_getNodes(self)                                 : Query node array base
 *   - Scene_getClearColor(self)                            : Query clear color (0xRRGGBBAA)
 *   - Scene_isDirty(self)                                  : Query dirty flag
 *   - Scene_getTypeId(self)                                : Query block type ID
 *   - Scene_getCamera(self, outView16, outProj16)          : Query camera matrices
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

Scene *Scene_0(void) {
    Scene *self = (Scene*) Memory_alloc(TYPE_GFX_SCENE_SINGLETON, sizeof(Scene));
    if (!self)
        self = (Scene*) calloc(1, sizeof(Scene));
    if (!self)
        return nullptr;
    (*self).nodes = nullptr;
    (*self).nodeCount = 0;
    (*self).nodeCapacity = 0;
    for (size_t i = 0; i < 16; i++) {
        (*self).cameraView[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        (*self).cameraProj[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
    (*self).clearColor = 0;
    (*self).dirty = false;
    (*self).typeId = TYPE_GFX_SCENE_SINGLETON;
    return self;
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

void Scene_free(Scene *self) {
    if (!self)
        return;
    free((*self).nodes);
    (*self).nodes = nullptr;
    (*self).nodeCount = 0;
    (*self).nodeCapacity = 0;
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

uint32_t Scene_addMesh(Scene *self, const Mesh *mesh, const float *transform16, uint32_t materialId) {
    if (!self)
        return UINT32_MAX;
    if ((*self).nodeCount >= (*self).nodeCapacity) {
        size_t newCap = (*self).nodeCapacity == 0 ? 16 : (*self).nodeCapacity * 2;
        SceneNode *newNodes = (SceneNode*) realloc((*self).nodes, newCap * sizeof(SceneNode));
        if (!newNodes)
            return UINT32_MAX;
        (*self).nodes = newNodes;
        (*self).nodeCapacity = newCap;
    }
    uint32_t idx = (uint32_t) (*self).nodeCount;
    SceneNode *node = &(*self).nodes[idx];
    (*node).mesh = mesh;
    if (transform16) {
        for (size_t i = 0; i < 16; i++)
            (*node).transform[i] = transform16[i];
    } else {
        for (size_t i = 0; i < 16; i++)
            (*node).transform[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
    (*node).materialId = materialId;
    (*node).visible = true;
    (*self).nodeCount++;
    (*self).dirty = true;
    return idx;
}

bool Scene_removeMesh(Scene *self, uint32_t nodeIndex) {
    if (!self)
        return false;
    if ((size_t) nodeIndex >= (*self).nodeCount)
        return false;
    for (size_t i = (size_t) nodeIndex; i + 1 < (*self).nodeCount; i++)
        (*self).nodes[i] = (*self).nodes[i + 1];
    (*self).nodeCount--;
    (*self).dirty = true;
    return true;
}

void Scene_clear(Scene *self) {
    if (!self)
        return;
    (*self).nodeCount = 0;
    (*self).dirty = true;
}

void Scene_render(Scene *self, Drawable *drawable) {
    if (!self || !drawable)
        return;
    (void)self;
    Drawable_setDirty(drawable, true);
    (*self).dirty = false;
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void Scene_setCamera(Scene *self, const float *view16, const float *proj16) {
    if (!self)
        return;
    if (view16) {
        for (size_t i = 0; i < 16; i++)
            (*self).cameraView[i] = view16[i];
    }
    if (proj16) {
        for (size_t i = 0; i < 16; i++)
            (*self).cameraProj[i] = proj16[i];
    }
    (*self).dirty = true;
}

;;SETTER
void Scene_setClearColor(Scene *self, uint32_t clearColor) {
    if (!self)
        return;
    (*self).clearColor = clearColor;
}

;;SETTER
void Scene_setDirty(Scene *self, bool dirty) {
    if (!self)
        return;
    (*self).dirty = dirty;
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
size_t Scene_getNodeCount(const Scene *self) {
    return self ? (*self).nodeCount : 0;
}

;;GETTER
const SceneNode *Scene_getNode(const Scene *self, size_t index) {
    if (!self)
        return nullptr;
    if (index >= (*self).nodeCount)
        return nullptr;
    return &(*self).nodes[index];
}

;;GETTER
const SceneNode *Scene_getNodes(const Scene *self) {
    return self ? (*self).nodes : nullptr;
}

;;GETTER
uint32_t Scene_getClearColor(const Scene *self) {
    return self ? (*self).clearColor : 0;
}

;;GETTER
bool Scene_isDirty(const Scene *self) {
    return self ? (*self).dirty : false;
}

;;GETTER
uint64_t Scene_getTypeId(const Scene *self) {
    return self ? (*self).typeId : 0;
}

;;GETTER
void Scene_getCamera(const Scene *self, float *outView16, float *outProj16) {
    if (!self) {
        if (outView16) {
            for (size_t i = 0; i < 16; i++)
                outView16[i] = 0.0f;
        }
        if (outProj16) {
            for (size_t i = 0; i < 16; i++)
                outProj16[i] = 0.0f;
        }
        return;
    }
    if (outView16) {
        for (size_t i = 0; i < 16; i++)
            outView16[i] = (*self).cameraView[i];
    }
    if (outProj16) {
        for (size_t i = 0; i < 16; i++)
            outProj16[i] = (*self).cameraProj[i];
    }
}
