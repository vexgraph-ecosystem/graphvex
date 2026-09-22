#ifndef MESH_MESHLET_H
#define MESH_MESHLET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "c23/constructor.h"
#include "../graphics/type.h"
#include "mesh/mesh.h"

// mesh/meshlet.h — 128-triangle cluster slice of a Mesh.
//
// A Meshlet is a borrowed view into a slice of a Mesh for cluster culling
// and GPU mesh shader dispatch. It never owns or frees vertices.

#define MESHLET_MAX_VERTICES 64u
#define MESHLET_MAX_TRIANGLES 128u

typedef struct Meshlet {
    const Mesh *mesh;         // Borrowed view into parent Mesh (never freed)
    uint32_t vertexOffset;    // Base vertex offset in parent Mesh
    uint32_t vertexCount;     // Vertex count spanned by this cluster
    uint32_t triangleOffset;  // Triangle offset in parent Mesh (in triangles)
    uint32_t triangleCount;   // Triangle count in this cluster (up to 128)
    float bounds[6];          // Cluster bounding box: minX, minY, minZ, maxX, maxY, maxZ
    float cone[4];            // Normal cone: nx, ny, nz, cutoff
    uint64_t typeId;          // Block-header type id (TYPE_MESHLET_SINGLETON)
} Meshlet;

// Empty meshlet
Meshlet *Meshlet_0(void);

// Meshlet initialized from mesh at meshletIndex
Meshlet *Meshlet_2(const Mesh *mesh, uint32_t meshletIndex);

// Release meshlet struct (does NOT free borrowed mesh)
void Meshlet_free(Meshlet *self);

// Build cluster slice from mesh at meshletIndex
bool Meshlet_build(const Mesh *mesh, uint32_t meshletIndex, Meshlet *dest);

// Pack cluster header + indices for GPU payload dispatch
uint32_t Meshlet_pack(const Meshlet *self, uint8_t *outBytes, size_t maxBytes);

// Symmetric mutators (null-safe no-op on null self)
void Meshlet_setMesh(Meshlet *self, const Mesh *mesh);
void Meshlet_setVertexOffset(Meshlet *self, uint32_t offset);
void Meshlet_setVertexCount(Meshlet *self, uint32_t count);
void Meshlet_setTriangleOffset(Meshlet *self, uint32_t offset);
void Meshlet_setTriangleCount(Meshlet *self, uint32_t count);
void Meshlet_setBounds(Meshlet *self, const float *bounds6);
void Meshlet_setCone(Meshlet *self, const float *cone4);

// Null-safe inspectors (integers yield 0, pointers yield nullptr)
const Mesh *Meshlet_getMesh(const Meshlet *self);
uint32_t Meshlet_getVertexOffset(const Meshlet *self);
uint32_t Meshlet_getVertexCount(const Meshlet *self);
uint32_t Meshlet_getTriangleOffset(const Meshlet *self);
uint32_t Meshlet_getTriangleCount(const Meshlet *self);
uint64_t Meshlet_getTypeId(const Meshlet *self);
void Meshlet_getBounds(const Meshlet *self, float *outBounds6);
void Meshlet_getCone(const Meshlet *self, float *outCone4);

#define Meshlet(...) CONSTRUCTOR_DISPATCH(Meshlet, __VA_ARGS__)
#endif
