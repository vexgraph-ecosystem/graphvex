#include "mesh/meshlet.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Meshlet
 * ============================================================================
 * 128-triangle cluster geometry slice representing a sub-mesh segment of a parent Mesh.
 * Operates as a lightweight borrowed view that neither owns nor frees the underlying
 * mesh vertex or index buffers.
 *
 * Encapsulates cluster-local bounding boxes and surface normal cones for early GPU
 * frustum and backface cluster culling. Packs 64-bit aligned cluster headers and
 * localized 8-bit index arrays into discrete GPU payload dispatches in compliance
 * with the Unified Graphics Abstraction Law.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Meshlet (mesh/meshlet.c)
 * LEVEL: L2 — Behavior (128-triangle cluster slice API)
 * ============================================================================
 * 128-triangle slice of a parent Mesh. Borrowed view: never owns or frees
 * mesh vertex or index buffers. Computes cluster bounding box and normal
 * cone for cluster culling. Packs cluster header and 8-bit local indices
 * for GPU payload dispatch.
 *
 * STRUCT FIELDS (Mirroring mesh/meshlet.h):
 * ----------------------------------------------------------------------------
 *   const Mesh *mesh;         // Borrowed view into parent Mesh (never freed)
 *   uint32_t vertexOffset;    // Base vertex offset in parent Mesh
 *   uint32_t vertexCount;     // Vertex count spanned by this cluster
 *   uint32_t triangleOffset;  // Triangle offset in parent Mesh (in triangles)
 *   uint32_t triangleCount;   // Triangle count in this cluster (up to 128)
 *   float bounds[6];          // Cluster bounding box: minX, minY, minZ, maxX, maxY, maxZ
 *   float cone[4];            // Normal cone: nx, ny, nz, cutoff
 *   uint64_t typeId;          // Block-header type id (TYPE_MESHLET_SINGLETON)
 *
 * PRIVATE HELPERS (file-local pure-data only):
 * ----------------------------------------------------------------------------
 *   MeshletPayloadHeader {
 *     float bounds[6];         // Cluster AABB (minX, minY, minZ, maxX, maxY, maxZ)
 *     float cone[4];           // Normal cone (nx, ny, nz, cutoff)
 *     uint32_t vertexOffset;   // Base vertex offset in parent Mesh
 *     uint32_t vertexCount;    // Vertex count spanned by this cluster
 *     uint32_t triangleOffset; // Triangle offset in parent Mesh (in triangles)
 *     uint32_t triangleCount;  // Triangle count in this cluster
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Meshlet_0(void)                               : Allocate empty cluster slice
 *   - Meshlet_2(mesh, meshletIndex)                 : Slice cluster from parent Mesh
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Meshlet_free(self)                            : Release cluster slice container
 *   - Meshlet_build(mesh, meshletIndex, dest)       : Build cluster bounds and cone
 *   - Meshlet_pack(self, outBytes, maxBytes)        : Pack GPU cluster payload bytes
 *
 * Private Core Functions: (.c static)
 *   - meshletInitDefaults(self)                     : Zero-initialize struct state
 *
 * Public Setters: (.h)
 *   - Meshlet_setMesh(self, mesh)                   : Set parent borrowed mesh
 *   - Meshlet_setVertexOffset(self, offset)         : Set cluster base vertex index
 *   - Meshlet_setVertexCount(self, count)           : Set cluster vertex span
 *   - Meshlet_setTriangleOffset(self, offset)       : Set cluster triangle start
 *   - Meshlet_setTriangleCount(self, count)         : Set cluster triangle count
 *   - Meshlet_setBounds(self, bounds6)              : Set cluster bounding box
 *   - Meshlet_setCone(self, cone4)                  : Set cluster normal cone
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Meshlet_getMesh(self)                         : Query borrowed parent mesh
 *   - Meshlet_getVertexOffset(self)                 : Query cluster base vertex
 *   - Meshlet_getVertexCount(self)                  : Query cluster vertex count
 *   - Meshlet_getTriangleOffset(self)               : Query cluster triangle start
 *   - Meshlet_getTriangleCount(self)                : Query cluster triangle count
 *   - Meshlet_getTypeId(self)                       : Query block type ID
 *   - Meshlet_getBounds(self, outBounds6)           : Query cluster bounding box
 *   - Meshlet_getCone(self, outCone4)               : Query cluster normal cone
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// mesh/meshlet.c — 128-triangle cluster slice implementation.

typedef struct MeshletPayloadHeader {
    float bounds[6];         // Cluster AABB (minX, minY, minZ, maxX, maxY, maxZ)
    float cone[4];           // Normal cone (nx, ny, nz, cutoff)
    uint32_t vertexOffset;   // Base vertex offset in parent Mesh
    uint32_t vertexCount;    // Vertex count spanned by this cluster
    uint32_t triangleOffset; // Triangle offset in parent Mesh (in triangles)
    uint32_t triangleCount;  // Triangle count in this cluster
} MeshletPayloadHeader;

static void meshletInitDefaults(Meshlet *self) {
    (*self).mesh = nullptr;
    (*self).vertexOffset = 0u;
    (*self).vertexCount = 0u;
    (*self).triangleOffset = 0u;
    (*self).triangleCount = 0u;
    for (size_t i = 0; i < 6; i++)
        (*self).bounds[i] = 0.0f;
    for (size_t i = 0; i < 4; i++)
        (*self).cone[i] = 0.0f;
    (*self).typeId = TYPE_MESHLET_SINGLETON;
}

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================


Meshlet *Meshlet_0(void) {
    Meshlet *self = (Meshlet*) Memory_alloc(TYPE_MESHLET_SINGLETON, sizeof(Meshlet));
    if (!self)
        self = (Meshlet*) calloc(1, sizeof(Meshlet));
    if (!self)
        return nullptr;
    meshletInitDefaults(self);
    return self;
}

Meshlet *Meshlet_2(const Mesh *mesh, uint32_t meshletIndex) {
    Meshlet *self = (Meshlet*) Memory_alloc(TYPE_MESHLET_SINGLETON, sizeof(Meshlet));
    if (!self)
        self = (Meshlet*) calloc(1, sizeof(Meshlet));
    if (!self)
        return nullptr;
    meshletInitDefaults(self);
    if (mesh)
        Meshlet_build(mesh, meshletIndex, self);
    return self;
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

void Meshlet_free(Meshlet *self) {
    if (!self)
        return;
    (*self).mesh = nullptr;
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

bool Meshlet_build(const Mesh *mesh, uint32_t meshletIndex, Meshlet *dest) {
    if (!mesh || !dest)
        return false;
    const uint32_t *indices = (*mesh).indices;
    const float *vertices = (*mesh).vertices;
    if (!indices || !vertices)
        return false;

    size_t totalTriangles = (*mesh).indexCount / 3;
    size_t triOffset = (size_t) meshletIndex * (size_t) MESHLET_MAX_TRIANGLES;
    if (triOffset >= totalTriangles)
        return false;

    size_t rem = totalTriangles - triOffset;
    uint32_t triCount = (rem < (size_t) MESHLET_MAX_TRIANGLES) ? (uint32_t) rem : (uint32_t) MESHLET_MAX_TRIANGLES;

    uint32_t minV = UINT32_MAX;
    uint32_t maxV = 0;
    float minX = 1e30f;
    float minY = 1e30f;
    float minZ = 1e30f;
    float maxX = -1e30f;
    float maxY = -1e30f;
    float maxZ = -1e30f;

    float avgNx = 0.0f;
    float avgNy = 0.0f;
    float avgNz = 0.0f;

    for (uint32_t t = 0; t < triCount; t++) {
        size_t baseIdx = (triOffset + (size_t) t) * 3;
        uint32_t i0 = indices[baseIdx];
        uint32_t i1 = indices[baseIdx + 1];
        uint32_t i2 = indices[baseIdx + 2];

        uint32_t triIndices[3] = { i0, i1, i2 };
        for (int k = 0; k < 3; k++) {
            uint32_t v = triIndices[k];
            if (v < minV)
                minV = v;
            if (v > maxV)
                maxV = v;
            if ((size_t) v < (*mesh).vertexCount) {
                float vx = vertices[(size_t) v * 8 + 0];
                float vy = vertices[(size_t) v * 8 + 1];
                float vz = vertices[(size_t) v * 8 + 2];
                if (vx < minX)
                    minX = vx;
                if (vy < minY)
                    minY = vy;
                if (vz < minZ)
                    minZ = vz;
                if (vx > maxX)
                    maxX = vx;
                if (vy > maxY)
                    maxY = vy;
                if (vz > maxZ)
                    maxZ = vz;
            }
        }

        // Triangle face normal
        if ((size_t) i0 < (*mesh).vertexCount && (size_t) i1 < (*mesh).vertexCount && (size_t) i2 < (*mesh).vertexCount) {
            float x0 = vertices[(size_t) i0 * 8 + 0];
            float y0 = vertices[(size_t) i0 * 8 + 1];
            float z0 = vertices[(size_t) i0 * 8 + 2];

            float x1 = vertices[(size_t) i1 * 8 + 0];
            float y1 = vertices[(size_t) i1 * 8 + 1];
            float z1 = vertices[(size_t) i1 * 8 + 2];

            float x2 = vertices[(size_t) i2 * 8 + 0];
            float y2 = vertices[(size_t) i2 * 8 + 1];
            float z2 = vertices[(size_t) i2 * 8 + 2];

            float e1x = x1 - x0;
            float e1y = y1 - y0;
            float e1z = z1 - z0;

            float e2x = x2 - x0;
            float e2y = y2 - y0;
            float e2z = z2 - z0;

            float tnx = e1y * e2z - e1z * e2y;
            float tny = e1z * e2x - e1x * e2z;
            float tnz = e1x * e2y - e1y * e2x;
            float tlenSq = tnx * tnx + tny * tny + tnz * tnz;
            if (tlenSq > 1e-12f) {
                float invLen = 1.0f / sqrtf(tlenSq);
                avgNx += tnx * invLen;
                avgNy += tny * invLen;
                avgNz += tnz * invLen;
            }
        }
    }

    (*dest).mesh = mesh;
    (*dest).triangleOffset = (uint32_t) triOffset;
    (*dest).triangleCount = triCount;
    (*dest).typeId = TYPE_MESHLET_SINGLETON;

    if (minV <= maxV) {
        (*dest).vertexOffset = minV;
        (*dest).vertexCount = maxV - minV + 1u;
    } else {
        (*dest).vertexOffset = 0u;
        (*dest).vertexCount = 0u;
    }

    if (minX <= maxX) {
        (*dest).bounds[0] = minX;
        (*dest).bounds[1] = minY;
        (*dest).bounds[2] = minZ;
        (*dest).bounds[3] = maxX;
        (*dest).bounds[4] = maxY;
        (*dest).bounds[5] = maxZ;
    } else {
        for (size_t i = 0; i < 6; i++)
            (*dest).bounds[i] = 0.0f;
    }

    float avgLenSq = avgNx * avgNx + avgNy * avgNy + avgNz * avgNz;
    if (avgLenSq > 1e-12f) {
        float invAvg = 1.0f / sqrtf(avgLenSq);
        float cx = avgNx * invAvg;
        float cy = avgNy * invAvg;
        float cz = avgNz * invAvg;
        (*dest).cone[0] = cx;
        (*dest).cone[1] = cy;
        (*dest).cone[2] = cz;

        float minDot = 1.0f;
        for (uint32_t t = 0; t < triCount; t++) {
            size_t baseIdx = (triOffset + (size_t) t) * 3;
            uint32_t i0 = indices[baseIdx];
            uint32_t i1 = indices[baseIdx + 1];
            uint32_t i2 = indices[baseIdx + 2];
            if ((size_t) i0 < (*mesh).vertexCount && (size_t) i1 < (*mesh).vertexCount && (size_t) i2 < (*mesh).vertexCount) {
                float e1x = vertices[(size_t) i1 * 8 + 0] - vertices[(size_t) i0 * 8 + 0];
                float e1y = vertices[(size_t) i1 * 8 + 1] - vertices[(size_t) i0 * 8 + 1];
                float e1z = vertices[(size_t) i1 * 8 + 2] - vertices[(size_t) i0 * 8 + 2];

                float e2x = vertices[(size_t) i2 * 8 + 0] - vertices[(size_t) i0 * 8 + 0];
                float e2y = vertices[(size_t) i2 * 8 + 1] - vertices[(size_t) i0 * 8 + 1];
                float e2z = vertices[(size_t) i2 * 8 + 2] - vertices[(size_t) i0 * 8 + 2];

                float tnx = e1y * e2z - e1z * e2y;
                float tny = e1z * e2x - e1x * e2z;
                float tnz = e1x * e2y - e1y * e2x;
                float tlenSq = tnx * tnx + tny * tny + tnz * tnz;
                if (tlenSq > 1e-12f) {
                    float dot = (tnx * cx + tny * cy + tnz * cz) / sqrtf(tlenSq);
                    if (dot < minDot)
                        minDot = dot;
                }
            }
        }
        (*dest).cone[3] = minDot;
    } else {
        (*dest).cone[0] = 0.0f;
        (*dest).cone[1] = 0.0f;
        (*dest).cone[2] = 1.0f;
        (*dest).cone[3] = -1.0f;
    }

    return true;
}

uint32_t Meshlet_pack(const Meshlet *self, uint8_t *outBytes, size_t maxBytes) {
    if (!self)
        return 0u;
    size_t headerSize = sizeof(MeshletPayloadHeader);
    size_t indexBytes = (size_t) (*self).triangleCount * 3u;
    size_t totalBytes = headerSize + indexBytes;

    if (!outBytes)
        return (uint32_t) totalBytes;
    if (maxBytes < totalBytes)
        return 0u;

    MeshletPayloadHeader hdr;
    for (size_t i = 0; i < 6; i++)
        hdr.bounds[i] = (*self).bounds[i];
    for (size_t i = 0; i < 4; i++)
        hdr.cone[i] = (*self).cone[i];
    hdr.vertexOffset = (*self).vertexOffset;
    hdr.vertexCount = (*self).vertexCount;
    hdr.triangleOffset = (*self).triangleOffset;
    hdr.triangleCount = (*self).triangleCount;

    memcpy(outBytes, &hdr, headerSize);

    const Mesh *mesh = (*self).mesh;
    if (mesh && (*mesh).indices) {
        size_t triBase = (size_t) (*self).triangleOffset * 3u;
        for (size_t i = 0; i < indexBytes; i++) {
            uint32_t gIdx = (*mesh).indices[triBase + i];
            uint8_t localIdx = 0;
            if (gIdx >= (*self).vertexOffset)
                localIdx = (uint8_t) (gIdx - (*self).vertexOffset);
            else
                localIdx = (uint8_t) gIdx;
            outBytes[headerSize + i] = localIdx;
        }
    } else {
        memset(outBytes + headerSize, 0, indexBytes);
    }
    return (uint32_t) totalBytes;
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void Meshlet_setMesh(Meshlet *self, const Mesh *mesh) {
    if (!self)
        return;
    (*self).mesh = mesh;
}

;;SETTER
void Meshlet_setVertexOffset(Meshlet *self, uint32_t offset) {
    if (!self)
        return;
    (*self).vertexOffset = offset;
}

;;SETTER
void Meshlet_setVertexCount(Meshlet *self, uint32_t count) {
    if (!self)
        return;
    (*self).vertexCount = count;
}

;;SETTER
void Meshlet_setTriangleOffset(Meshlet *self, uint32_t offset) {
    if (!self)
        return;
    (*self).triangleOffset = offset;
}

;;SETTER
void Meshlet_setTriangleCount(Meshlet *self, uint32_t count) {
    if (!self)
        return;
    (*self).triangleCount = count;
}

;;SETTER
void Meshlet_setBounds(Meshlet *self, const float *bounds6) {
    if (!self || !bounds6)
        return;
    for (size_t i = 0; i < 6; i++)
        (*self).bounds[i] = bounds6[i];
}

;;SETTER
void Meshlet_setCone(Meshlet *self, const float *cone4) {
    if (!self || !cone4)
        return;
    for (size_t i = 0; i < 4; i++)
        (*self).cone[i] = cone4[i];
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
const Mesh *Meshlet_getMesh(const Meshlet *self) {
    return self ? (*self).mesh : nullptr;
}

;;GETTER
uint32_t Meshlet_getVertexOffset(const Meshlet *self) {
    return self ? (*self).vertexOffset : 0u;
}

;;GETTER
uint32_t Meshlet_getVertexCount(const Meshlet *self) {
    return self ? (*self).vertexCount : 0u;
}

;;GETTER
uint32_t Meshlet_getTriangleOffset(const Meshlet *self) {
    return self ? (*self).triangleOffset : 0u;
}

;;GETTER
uint32_t Meshlet_getTriangleCount(const Meshlet *self) {
    return self ? (*self).triangleCount : 0u;
}

;;GETTER
uint64_t Meshlet_getTypeId(const Meshlet *self) {
    return self ? (*self).typeId : 0;
}

;;GETTER
void Meshlet_getBounds(const Meshlet *self, float *outBounds6) {
    if (!outBounds6)
        return;
    if (!self) {
        for (size_t i = 0; i < 6; i++)
            outBounds6[i] = 0.0f;
        return;
    }
    for (size_t i = 0; i < 6; i++)
        outBounds6[i] = (*self).bounds[i];
}

;;GETTER
void Meshlet_getCone(const Meshlet *self, float *outCone4) {
    if (!outCone4)
        return;
    if (!self) {
        for (size_t i = 0; i < 4; i++)
            outCone4[i] = 0.0f;
        return;
    }
    for (size_t i = 0; i < 4; i++)
        outCone4[i] = (*self).cone[i];
}
