#include "mesh/mesh.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"
#include "io/vfs.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Mesh
 * ============================================================================
 * 3D polygonal mesh container managing contiguous off-heap vertex attribute and index arrays.
 * Encodes 8-float interleaved vertex records (position, normal, UV texture coordinates)
 * and 32-bit triangle index buffers.
 *
 * Implements high-performance Wavefront OBJ stream parsing with deduplication,
 * memory-based geometry synthesis, and automated axis-aligned bounding box recomputation
 * in compliance with the Unified Graphics Abstraction Law.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Mesh (mesh/mesh.c)
 * LEVEL: L2 — Behavior (whole mesh vertex/index storage and OBJ parser API)
 * ============================================================================
 * Whole mesh container: off-heap vertex and index buffer storage. Vertices
 * have 8 floats (pos: x,y,z; norm: nx,ny,nz; uv: u,v). Supports Wavefront
 * OBJ parsing from disk or memory buffers, and bounds recomputation.
 *
 * STRUCT FIELDS (Mirroring mesh/mesh.h):
 * ----------------------------------------------------------------------------
 *   float *vertices;     // Interleaved vertex attributes: 8 floats per vertex (pos: x,y,z; norm: nx,ny,nz; uv: u,v)
 *   size_t vertexCount;  // Number of 8-float vertices
 *   uint32_t *indices;   // Triangle index buffer
 *   size_t indexCount;   // Number of indices (must be multiple of 3 for complete triangles)
 *   uint64_t typeId;     // Block-header type id (TYPE_MESH_SINGLETON)
 *   float bounds[6];     // Axis-aligned bounding box: minX, minY, minZ, maxX, maxY, maxZ
 *
 * PRIVATE HELPERS (file-local pure-data only):
 * ----------------------------------------------------------------------------
 *   ObjVertexKey {
 *     int32_t v;           // Position 1-based OBJ index (0 = none)
 *     int32_t vt;          // UV 1-based OBJ index (0 = none)
 *     int32_t vn;          // Normal 1-based OBJ index (0 = none)
 *     uint32_t meshIndex;  // Assigned de-duplicated mesh vertex index
 *   }
 *   ObjFaceCorner {
 *     int32_t v;           // Position index from face element
 *     int32_t vt;          // Texture coordinate index from face element
 *     int32_t vn;          // Normal index from face element
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Mesh_0(void)                           : Allocate empty mesh instance
 *   - Mesh_2(vertexCount, indexCount)        : Allocate sized mesh instance
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Mesh_free(self)                        : Release mesh vertex/index heap storage
 *   - Mesh_fromObj(path, dest)               : Load OBJ geometry from VFS file
 *   - Mesh_fromMemory(objData, len, dest)    : Parse OBJ geometry from memory buffer
 *   - Mesh_recomputeBounds(self)             : Recalculate axis-aligned bounding box
 *
 * Private Core Functions: (.c static)
 *   - meshFreeBlob(blob)                     : Free heap or typed memory blob
 *   - hashVertex(v, vt, vn)                  : Fast multiplicative vertex hash
 *   - parseFaceLine(...)                     : Parse OBJ face polygon indices
 *
 * Public Setters: (.h)
 *   - Mesh_setVertices(self, verts, count)   : Replace vertex attribute buffer
 *   - Mesh_setIndices(self, indices, count)  : Replace index element buffer
 *   - Mesh_setBounds(self, bounds6)          : Set explicit axis-aligned bounds
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Mesh_getVertexCount(self)              : Query total 8-float vertex count
 *   - Mesh_getIndexCount(self)               : Query total triangle index count
 *   - Mesh_getVertices(self)                 : Query immutable vertex attribute pointer
 *   - Mesh_getIndices(self)                  : Query immutable index element pointer
 *   - Mesh_getTypeId(self)                   : Query block type identifier
 *   - Mesh_getBounds(self, outBounds6)       : Retrieve 6-float bounding box extents
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// mesh/mesh.c — Whole mesh implementation.

typedef struct ObjVertexKey {
    int32_t v;           // Position 1-based OBJ index (0 = none)
    int32_t vt;          // UV 1-based OBJ index (0 = none)
    int32_t vn;          // Normal 1-based OBJ index (0 = none)
    uint32_t meshIndex;  // Assigned de-duplicated mesh vertex index
} ObjVertexKey;

typedef struct ObjFaceCorner {
    int32_t v;           // Position index from face element
    int32_t vt;          // Texture coordinate index from face element
    int32_t vn;          // Normal index from face element
} ObjFaceCorner;

static void meshFreeBlob(void *blob) {
    if (!blob)
        return;
    if (Memory_length(blob) != 0)
        Memory_free(blob);
    else
        free(blob);
}

static inline uint32_t hashVertex(int32_t v, int32_t vt, int32_t vn) {
    uint32_t h = (uint32_t) v * 73856093u;
    h ^= (uint32_t) vt * 19349663u;
    h ^= (uint32_t) vn * 83492791u;
    return h;
}

static size_t countFaceTokens(const char *p, const char *end) {
    size_t count = 0;
    while (p < end && *p != '\n' && *p != '\r') {
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;
        if (p < end && *p != '\n' && *p != '\r' && *p != '#') {
            count++;
            while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
                p++;
        } else {
            break;
        }
    }
    return count;
}

static size_t parseFaceLine(const char *line, const char *end, ObjFaceCorner *corners, size_t maxCorners) {
    const char *p = line;
    size_t count = 0;
    while (p < end && count < maxCorners) {
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;
        if (p >= end || *p == '\n' || *p == '\r')
            break;
        char *endptr = nullptr;
        int32_t v = (int32_t) strtol(p, &endptr, 10);
        if (endptr != p) {
            int32_t vt = 0;
            int32_t vn = 0;
            p = endptr;
            if (p < end && *p == '/') {
                p++;
                if (p < end && *p != '/') {
                    vt = (int32_t) strtol(p, &endptr, 10);
                    p = endptr;
                }
                if (p < end && *p == '/') {
                    p++;
                    vn = (int32_t) strtol(p, &endptr, 10);
                    p = endptr;
                }
            }
            corners[count].v = v;
            corners[count].vt = vt;
            corners[count].vn = vn;
            count++;
        } else {
            while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
                p++;
        }
    }
    return count;
}

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

Mesh *Mesh_0(void) {
    return Mesh_2(0, 0);
}

Mesh *Mesh_2(size_t vertexCount, size_t indexCount) {
    Mesh *self = (Mesh*) Memory_alloc(TYPE_MESH_SINGLETON, sizeof(Mesh));
    if (!self)
        self = (Mesh*) calloc(1, sizeof(Mesh));
    if (!self)
        return nullptr;
    (*self).vertices = nullptr;
    (*self).vertexCount = 0;
    (*self).indices = nullptr;
    (*self).indexCount = 0;
    (*self).typeId = TYPE_MESH_SINGLETON;
    for (size_t i = 0; i < 6; i++)
        (*self).bounds[i] = 0.0f;
    if (vertexCount > 0) {
        float *v = (float*) Memory_alloc(TYPE_ARRAY, vertexCount * 8 * sizeof(float));
        if (!v)
            v = (float*) calloc(vertexCount * 8, sizeof(float));
        if (!v) {
            Mesh_free(self);
            return nullptr;
        }
        (*self).vertices = v;
        (*self).vertexCount = vertexCount;
    }
    if (indexCount > 0) {
        uint32_t *idx = (uint32_t*) Memory_alloc(TYPE_ARRAY, indexCount * sizeof(uint32_t));
        if (!idx)
            idx = (uint32_t*) calloc(indexCount, sizeof(uint32_t));
        if (!idx) {
            Mesh_free(self);
            return nullptr;
        }
        (*self).indices = idx;
        (*self).indexCount = indexCount;
    }
    return self;
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

void Mesh_free(Mesh *self) {
    if (!self)
        return;
    if ((*self).vertices) {
        meshFreeBlob((*self).vertices);
        (*self).vertices = nullptr;
    }
    if ((*self).indices) {
        meshFreeBlob((*self).indices);
        (*self).indices = nullptr;
    }
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

bool Mesh_fromObj(const char *path, Mesh *dest) {
    if (!path || !dest)
        return false;
    char resolved[1024];
    const char *filePath = path;
    if (Vfs_resolve(path, resolved, sizeof(resolved)))
        filePath = resolved;
    FILE *f = fopen(filePath, "rb");
    if (!f)
        f = fopen(path, "rb");
    if (!f)
        return false;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    char *buffer = (char*) malloc((size_t) size + 1);
    if (!buffer) {
        fclose(f);
        return false;
    }
    size_t readBytes = fread(buffer, 1, (size_t) size, f);
    fclose(f);
    buffer[readBytes] = '\0';
    bool ok = Mesh_fromMemory(buffer, readBytes, dest);
    free(buffer);
    return ok;
}

bool Mesh_fromMemory(const char *objData, size_t len, Mesh *dest) {
    if (!objData || len == 0 || !dest)
        return false;

    const char *end = objData + len;
    const char *p = objData;

    size_t numPos = 0;
    size_t numNorm = 0;
    size_t numUv = 0;
    size_t numFaceCorners = 0;

    // Pass 1: Count elements
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;
        if (p >= end)
            break;
        if (*p == '\n') {
            p++;
            continue;
        }
        if (*p == '#') {
            while (p < end && *p != '\n')
                p++;
            if (p < end && *p == '\n')
                p++;
            continue;
        }

        if (p + 1 < end && p[0] == 'v' && (p[1] == ' ' || p[1] == '\t'))
            numPos++;
        else if (p + 2 < end && p[0] == 'v' && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t'))
            numNorm++;
        else if (p + 2 < end && p[0] == 'v' && p[1] == 't' && (p[2] == ' ' || p[2] == '\t'))
            numUv++;
        else if (p + 1 < end && p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            size_t tokens = countFaceTokens(p + 2, end);
            if (tokens >= 3)
                numFaceCorners += (tokens - 2) * 3;
        }

        while (p < end && *p != '\n')
            p++;
        if (p < end && *p == '\n')
            p++;
    }

    if (numPos == 0 || numFaceCorners == 0)
        return false;

    float *tempPos = (float*) malloc(numPos * 3 * sizeof(float));
    if (!tempPos)
        return false;

    float *tempNorm = nullptr;
    if (numNorm > 0) {
        tempNorm = (float*) malloc(numNorm * 3 * sizeof(float));
        if (!tempNorm) {
            free(tempPos);
            return false;
        }
    }

    float *tempUv = nullptr;
    if (numUv > 0) {
        tempUv = (float*) malloc(numUv * 2 * sizeof(float));
        if (!tempUv) {
            if (tempNorm)
                free(tempNorm);
            free(tempPos);
            return false;
        }
    }

    // Allocate output buffers
    float *meshVerts = (float*) Memory_alloc(TYPE_ARRAY, numFaceCorners * 8 * sizeof(float));
    if (!meshVerts)
        meshVerts = (float*) calloc(numFaceCorners * 8, sizeof(float));
    if (!meshVerts) {
        if (tempUv)
            free(tempUv);
        if (tempNorm)
            free(tempNorm);
        free(tempPos);
        return false;
    }

    uint32_t *meshIndices = (uint32_t*) Memory_alloc(TYPE_ARRAY, numFaceCorners * sizeof(uint32_t));
    if (!meshIndices)
        meshIndices = (uint32_t*) calloc(numFaceCorners, sizeof(uint32_t));
    if (!meshIndices) {
        meshFreeBlob(meshVerts);
        if (tempUv)
            free(tempUv);
        if (tempNorm)
            free(tempNorm);
        free(tempPos);
        return false;
    }

    uint32_t tableCap = 64u;
    while (tableCap < (uint32_t) (numFaceCorners * 2))
        tableCap <<= 1;
    ObjVertexKey *hashTable = (ObjVertexKey*) calloc(tableCap, sizeof(ObjVertexKey));
    if (!hashTable) {
        meshFreeBlob(meshIndices);
        meshFreeBlob(meshVerts);
        if (tempUv)
            free(tempUv);
        if (tempNorm)
            free(tempNorm);
        free(tempPos);
        return false;
    }
    for (uint32_t i = 0; i < tableCap; i++)
        hashTable[i].meshIndex = UINT32_MAX;

    // Pass 2: Parse data and build mesh
    p = objData;
    size_t curPos = 0;
    size_t curNorm = 0;
    size_t curUv = 0;
    size_t meshVertexCount = 0;
    size_t meshIndexCount = 0;
    uint32_t mask = tableCap - 1u;

    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;
        if (p >= end)
            break;
        if (*p == '\n') {
            p++;
            continue;
        }
        if (*p == '#') {
            while (p < end && *p != '\n')
                p++;
            if (p < end && *p == '\n')
                p++;
            continue;
        }

        if (p + 1 < end && p[0] == 'v' && (p[1] == ' ' || p[1] == '\t')) {
            char *next = nullptr;
            const char *cur = p + 2;
            float x = strtof(cur, &next);
            cur = next;
            float y = strtof(cur, &next);
            cur = next;
            float z = strtof(cur, &next);
            if (curPos < numPos) {
                tempPos[curPos * 3 + 0] = x;
                tempPos[curPos * 3 + 1] = y;
                tempPos[curPos * 3 + 2] = z;
                curPos++;
            }
        } else if (p + 2 < end && p[0] == 'v' && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t')) {
            char *next = nullptr;
            const char *cur = p + 3;
            float nx = strtof(cur, &next);
            cur = next;
            float ny = strtof(cur, &next);
            cur = next;
            float nz = strtof(cur, &next);
            if (tempNorm && curNorm < numNorm) {
                tempNorm[curNorm * 3 + 0] = nx;
                tempNorm[curNorm * 3 + 1] = ny;
                tempNorm[curNorm * 3 + 2] = nz;
                curNorm++;
            }
        } else if (p + 2 < end && p[0] == 'v' && p[1] == 't' && (p[2] == ' ' || p[2] == '\t')) {
            char *next = nullptr;
            const char *cur = p + 3;
            float u = strtof(cur, &next);
            cur = next;
            float v = strtof(cur, &next);
            if (tempUv && curUv < numUv) {
                tempUv[curUv * 2 + 0] = u;
                tempUv[curUv * 2 + 1] = v;
                curUv++;
            }
        } else if (p + 1 < end && p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            ObjFaceCorner corners[64];
            size_t cornerCount = parseFaceLine(p + 2, end, corners, 64);
            if (cornerCount >= 3) {
                for (size_t i = 1; i + 1 < cornerCount; i++) {
                    ObjFaceCorner tri[3] = { corners[0], corners[i], corners[i + 1] };
                    for (int k = 0; k < 3; k++) {
                        int32_t v = tri[k].v;
                        int32_t vt = tri[k].vt;
                        int32_t vn = tri[k].vn;

                        if (v < 0)
                            v = (int32_t) ((int64_t) curPos + v + 1);
                        if (vt < 0)
                            vt = (int32_t) ((int64_t) curUv + vt + 1);
                        if (vn < 0)
                            vn = (int32_t) ((int64_t) curNorm + vn + 1);

                        uint32_t slot = hashVertex(v, vt, vn) & mask;
                        uint32_t vertIndex = UINT32_MAX;
                        while (hashTable[slot].meshIndex != UINT32_MAX) {
                            if (hashTable[slot].v == v && hashTable[slot].vt == vt && hashTable[slot].vn == vn) {
                                vertIndex = hashTable[slot].meshIndex;
                                break;
                            }
                            slot = (slot + 1u) & mask;
                        }

                        if (vertIndex == UINT32_MAX) {
                            vertIndex = (uint32_t) meshVertexCount;
                            meshVertexCount++;
                            hashTable[slot].v = v;
                            hashTable[slot].vt = vt;
                            hashTable[slot].vn = vn;
                            hashTable[slot].meshIndex = vertIndex;

                            // Position
                            if (v > 0 && (size_t) (v - 1) < curPos) {
                                meshVerts[vertIndex * 8 + 0] = tempPos[(v - 1) * 3 + 0];
                                meshVerts[vertIndex * 8 + 1] = tempPos[(v - 1) * 3 + 1];
                                meshVerts[vertIndex * 8 + 2] = tempPos[(v - 1) * 3 + 2];
                            } else {
                                meshVerts[vertIndex * 8 + 0] = 0.0f;
                                meshVerts[vertIndex * 8 + 1] = 0.0f;
                                meshVerts[vertIndex * 8 + 2] = 0.0f;
                            }

                            // Normal
                            if (vn > 0 && tempNorm && (size_t) (vn - 1) < curNorm) {
                                meshVerts[vertIndex * 8 + 3] = tempNorm[(vn - 1) * 3 + 0];
                                meshVerts[vertIndex * 8 + 4] = tempNorm[(vn - 1) * 3 + 1];
                                meshVerts[vertIndex * 8 + 5] = tempNorm[(vn - 1) * 3 + 2];
                            } else {
                                meshVerts[vertIndex * 8 + 3] = 0.0f;
                                meshVerts[vertIndex * 8 + 4] = 0.0f;
                                meshVerts[vertIndex * 8 + 5] = 0.0f;
                            }

                            // UV
                            if (vt > 0 && tempUv && (size_t) (vt - 1) < curUv) {
                                meshVerts[vertIndex * 8 + 6] = tempUv[(vt - 1) * 2 + 0];
                                meshVerts[vertIndex * 8 + 7] = tempUv[(vt - 1) * 2 + 1];
                            } else {
                                meshVerts[vertIndex * 8 + 6] = 0.0f;
                                meshVerts[vertIndex * 8 + 7] = 0.0f;
                            }
                        }
                        meshIndices[meshIndexCount++] = vertIndex;
                    }
                }
            }
        }

        while (p < end && *p != '\n')
            p++;
        if (p < end && *p == '\n')
            p++;
    }

    free(hashTable);
    if (tempUv)
        free(tempUv);
    if (tempNorm)
        free(tempNorm);
    free(tempPos);

    if (meshVertexCount == 0 || meshIndexCount == 0) {
        meshFreeBlob(meshIndices);
        meshFreeBlob(meshVerts);
        return false;
    }

    // Generate normals if none were provided in the OBJ
    if (numNorm == 0) {
        for (size_t i = 0; i < meshIndexCount; i += 3) {
            uint32_t i0 = meshIndices[i];
            uint32_t i1 = meshIndices[i + 1];
            uint32_t i2 = meshIndices[i + 2];
            float e1x = meshVerts[i1 * 8 + 0] - meshVerts[i0 * 8 + 0];
            float e1y = meshVerts[i1 * 8 + 1] - meshVerts[i0 * 8 + 1];
            float e1z = meshVerts[i1 * 8 + 2] - meshVerts[i0 * 8 + 2];
            float e2x = meshVerts[i2 * 8 + 0] - meshVerts[i0 * 8 + 0];
            float e2y = meshVerts[i2 * 8 + 1] - meshVerts[i0 * 8 + 1];
            float e2z = meshVerts[i2 * 8 + 2] - meshVerts[i0 * 8 + 2];
            float nx = e1y * e2z - e1z * e2y;
            float ny = e1z * e2x - e1x * e2z;
            float nz = e1x * e2y - e1y * e2x;
            meshVerts[i0 * 8 + 3] += nx;
            meshVerts[i0 * 8 + 4] += ny;
            meshVerts[i0 * 8 + 5] += nz;
            meshVerts[i1 * 8 + 3] += nx;
            meshVerts[i1 * 8 + 4] += ny;
            meshVerts[i1 * 8 + 5] += nz;
            meshVerts[i2 * 8 + 3] += nx;
            meshVerts[i2 * 8 + 4] += ny;
            meshVerts[i2 * 8 + 5] += nz;
        }
        for (size_t v = 0; v < meshVertexCount; v++) {
            float nx = meshVerts[v * 8 + 3];
            float ny = meshVerts[v * 8 + 4];
            float nz = meshVerts[v * 8 + 5];
            float lenSq = nx * nx + ny * ny + nz * nz;
            if (lenSq > 1e-12f) {
                float invLen = 1.0f / sqrtf(lenSq);
                meshVerts[v * 8 + 3] = nx * invLen;
                meshVerts[v * 8 + 4] = ny * invLen;
                meshVerts[v * 8 + 5] = nz * invLen;
            } else {
                meshVerts[v * 8 + 3] = 0.0f;
                meshVerts[v * 8 + 4] = 0.0f;
                meshVerts[v * 8 + 5] = 1.0f;
            }
        }
    }

    // Shrink vertex buffer to exact vertex count if smaller
    if (meshVertexCount < numFaceCorners) {
        size_t exactBytes = meshVertexCount * 8 * sizeof(float);
        float *tightVerts = (float*) Memory_alloc(TYPE_ARRAY, exactBytes);
        if (!tightVerts)
            tightVerts = (float*) malloc(exactBytes);
        if (tightVerts) {
            memcpy(tightVerts, meshVerts, exactBytes);
            meshFreeBlob(meshVerts);
            meshVerts = tightVerts;
        }
    }

    // Release old destination data if any
    if ((*dest).vertices) {
        meshFreeBlob((*dest).vertices);
        (*dest).vertices = nullptr;
        (*dest).vertexCount = 0;
    }
    if ((*dest).indices) {
        meshFreeBlob((*dest).indices);
        (*dest).indices = nullptr;
        (*dest).indexCount = 0;
    }

    (*dest).vertices = meshVerts;
    (*dest).vertexCount = meshVertexCount;
    (*dest).indices = meshIndices;
    (*dest).indexCount = meshIndexCount;
    (*dest).typeId = TYPE_MESH_SINGLETON;

    Mesh_recomputeBounds(dest);
    return true;
}

void Mesh_recomputeBounds(Mesh *self) {
    if (!self)
        return;
    if ((*self).vertexCount == 0 || !(*self).vertices) {
        for (size_t i = 0; i < 6; i++)
            (*self).bounds[i] = 0.0f;
        return;
    }
    float minX = (*self).vertices[0];
    float minY = (*self).vertices[1];
    float minZ = (*self).vertices[2];
    float maxX = minX;
    float maxY = minY;
    float maxZ = minZ;
    for (size_t i = 1; i < (*self).vertexCount; i++) {
        size_t base = i * 8;
        float x = (*self).vertices[base];
        float y = (*self).vertices[base + 1];
        float z = (*self).vertices[base + 2];
        if (x < minX)
            minX = x;
        if (y < minY)
            minY = y;
        if (z < minZ)
            minZ = z;
        if (x > maxX)
            maxX = x;
        if (y > maxY)
            maxY = y;
        if (z > maxZ)
            maxZ = z;
    }
    (*self).bounds[0] = minX;
    (*self).bounds[1] = minY;
    (*self).bounds[2] = minZ;
    (*self).bounds[3] = maxX;
    (*self).bounds[4] = maxY;
    (*self).bounds[5] = maxZ;
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void Mesh_setVertices(Mesh *self, const float *verts, size_t count) {
    if (!self)
        return;
    if ((*self).vertices) {
        meshFreeBlob((*self).vertices);
        (*self).vertices = nullptr;
        (*self).vertexCount = 0;
    }
    if (verts && count > 0) {
        size_t bytes = count * 8 * sizeof(float);
        float *copy = (float*) Memory_alloc(TYPE_ARRAY, bytes);
        if (!copy)
            copy = (float*) malloc(bytes);
        if (!copy)
            return;
        memcpy(copy, verts, bytes);
        (*self).vertices = copy;
        (*self).vertexCount = count;
        Mesh_recomputeBounds(self);
    }
}

;;SETTER
void Mesh_setIndices(Mesh *self, const uint32_t *indices, size_t count) {
    if (!self)
        return;
    if ((*self).indices) {
        meshFreeBlob((*self).indices);
        (*self).indices = nullptr;
        (*self).indexCount = 0;
    }
    if (indices && count > 0) {
        size_t bytes = count * sizeof(uint32_t);
        uint32_t *copy = (uint32_t*) Memory_alloc(TYPE_ARRAY, bytes);
        if (!copy)
            copy = (uint32_t*) malloc(bytes);
        if (!copy)
            return;
        memcpy(copy, indices, bytes);
        (*self).indices = copy;
        (*self).indexCount = count;
    }
}

;;SETTER
void Mesh_setBounds(Mesh *self, const float *bounds6) {
    if (!self || !bounds6)
        return;
    for (size_t i = 0; i < 6; i++)
        (*self).bounds[i] = bounds6[i];
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
size_t Mesh_getVertexCount(const Mesh *self) {
    return self ? (*self).vertexCount : 0;
}

;;GETTER
size_t Mesh_getIndexCount(const Mesh *self) {
    return self ? (*self).indexCount : 0;
}

;;GETTER
const float *Mesh_getVertices(const Mesh *self) {
    return self ? (*self).vertices : nullptr;
}

;;GETTER
const uint32_t *Mesh_getIndices(const Mesh *self) {
    return self ? (*self).indices : nullptr;
}

;;GETTER
uint64_t Mesh_getTypeId(const Mesh *self) {
    return self ? (*self).typeId : 0;
}

;;GETTER
void Mesh_getBounds(const Mesh *self, float *outBounds6) {
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
