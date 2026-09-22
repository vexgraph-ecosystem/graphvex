#ifndef SCENE_SCENE_H
#define SCENE_SCENE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "draw/drawable.h"
#include "../graphics/type.h"

#include "mesh/mesh.h"

// scene/scene.h — Rendered scene: mesh + transform + material list.

typedef struct SceneNode {
    const Mesh *mesh;
    float transform[16];
    uint32_t materialId;
    bool visible;
} SceneNode;

typedef struct Scene {
    SceneNode *nodes;
    size_t nodeCount;
    size_t nodeCapacity;
    float cameraView[16];
    float cameraProj[16];
    uint32_t clearColor;
    bool dirty;
    uint64_t typeId;
} Scene;

// Constructors
Scene *Scene_0(void);

// Core functions
void Scene_free(Scene *self);
uint32_t Scene_addMesh(Scene *self, const Mesh *mesh, const float *transform16, uint32_t materialId);
bool Scene_removeMesh(Scene *self, uint32_t nodeIndex);
void Scene_clear(Scene *self);
void Scene_render(Scene *self, Drawable *drawable);

// Setters
void Scene_setCamera(Scene *self, const float *view16, const float *proj16);
void Scene_setClearColor(Scene *self, uint32_t clearColor);
void Scene_setDirty(Scene *self, bool dirty);

// Getters
size_t Scene_getNodeCount(const Scene *self);
const SceneNode *Scene_getNode(const Scene *self, size_t index);
const SceneNode *Scene_getNodes(const Scene *self);
uint32_t Scene_getClearColor(const Scene *self);
bool Scene_isDirty(const Scene *self);
uint64_t Scene_getTypeId(const Scene *self);
void Scene_getCamera(const Scene *self, float *outView16, float *outProj16);

#define Scene(...) CONSTRUCTOR_DISPATCH(Scene, __VA_ARGS__)

#endif
