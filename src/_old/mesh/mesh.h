#ifndef MESH_MESH_H
#define MESH_MESH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "c23/constructor.h"
#include "../graphics/type.h"

// mesh/mesh.h — Whole mesh vertex and index storage with OBJ parsing.
//
// A Mesh is an off-heap container for vertex and index geometry data.
// Vertices are stored as 8 interleaved floats:
//   pos: x, y, z (3 floats)
//   norm: nx, ny, nz (3 floats)
//   uv: u, v (2 floats)
// Indices form triangle lists (3 uint32_t indices per triangle).

typedef struct Mesh {
    float *vertices;     // Interleaved vertex attributes: 8 floats per vertex (pos: x,y,z; norm: nx,ny,nz; uv: u,v)
    size_t vertexCount;  // Number of 8-float vertices
    uint32_t *indices;   // Triangle index buffer
    size_t indexCount;   // Number of indices (must be multiple of 3 for complete triangles)
    uint64_t typeId;     // Block-header type id (TYPE_MESH_SINGLETON)
    float bounds[6];     // Axis-aligned bounding box: minX, minY, minZ, maxX, maxY, maxZ
} Mesh;

// Empty mesh (0 vertices, 0 indices)
Mesh *Mesh_0(void);

// Preallocated mesh capacity
Mesh *Mesh_2(size_t vertexCount, size_t indexCount);

// Release mesh memory and internal vertex/index buffers
void Mesh_free(Mesh *self);

// Parse Wavefront OBJ from file path (resolves virtual URIs or native paths)
bool Mesh_fromObj(const char *path, Mesh *dest);

// Parse Wavefront OBJ from memory buffer (parses 'v', 'vn', 'vt', 'f' lines)
bool Mesh_fromMemory(const char *objData, size_t len, Mesh *dest);

// Recompute axis-aligned bounding box from vertex positions
void Mesh_recomputeBounds(Mesh *self);

// Symmetric mutators (null-safe no-op on null self)
void Mesh_setVertices(Mesh *self, const float *verts, size_t count);
void Mesh_setIndices(Mesh *self, const uint32_t *indices, size_t count);
void Mesh_setBounds(Mesh *self, const float *bounds6);

// Null-safe inspectors (integers/counts yield 0, pointers yield nullptr)
size_t Mesh_getVertexCount(const Mesh *self);
size_t Mesh_getIndexCount(const Mesh *self);
const float *Mesh_getVertices(const Mesh *self);
const uint32_t *Mesh_getIndices(const Mesh *self);
uint64_t Mesh_getTypeId(const Mesh *self);
void Mesh_getBounds(const Mesh *self, float *outBounds6);

#define Mesh(...) CONSTRUCTOR_DISPATCH(Mesh, __VA_ARGS__)
#endif
