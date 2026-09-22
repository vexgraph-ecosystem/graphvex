#include "scene/pipeline.h"

#include <stdlib.h>
#include <string.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"
#include "nio/mem.h"
#include "oop/type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Pipeline
 * ============================================================================
 * Backend-agnostic compiled graphics and compute pipeline state encapsulation.
 * Configures execution stage masks, Porter-Duff color blending operations, polygon
 * backface culling, and depth test/write state flags.
 *
 * Provides safe state tracking and shader entry point specification across direct
 * and accelerated graphics render loops in compliance with the Unified Graphics
 * Abstraction Law.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Pipeline (scene/pipeline.c)
 * LEVEL: L2 — Behavior (compiled raster and compute state)
 * ============================================================================
 * Compiled raster and compute state (shaders, blend modes, depth states).
 * Backend agnostic.
 *
 * STRUCT FIELDS (Mirroring scene/pipeline.h):
 * ----------------------------------------------------------------------------
 *   Pipeline {
 *     uint32_t stageMask;     // pipeline execution stage mask
 *     uint32_t blendMode;     // color blending configuration mode
 *     uint32_t cullMode;      // face culling mode flags
 *     bool depthTest;         // true if depth testing enabled
 *     bool depthWrite;        // true if depth writing enabled
 *     char shaderName[64];    // shader name or entry point identifier
 *     uint64_t typeId;        // block-header type id (TYPE_PIPELINE_SINGLETON)
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Pipeline_0(void)                             : Allocate default pipeline state
 *   - Pipeline_1(shaderName)                       : Allocate pipeline bound to shader
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Pipeline_free(self)                          : Release pipeline heap storage
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - Pipeline_setStageMask(self, stageMask)       : Set execution stage mask
 *   - Pipeline_setBlendMode(self, blendMode)       : Set color blending mode
 *   - Pipeline_setCullMode(self, cullMode)         : Set polygon culling mode
 *   - Pipeline_setDepthTest(self, depthTest)       : Enable/disable depth testing
 *   - Pipeline_setDepthWrite(self, depthWrite)     : Enable/disable depth writing
 *   - Pipeline_setShaderName(self, shaderName)     : Assign shader entry identifier
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Pipeline_getStageMask(self)                  : Query execution stage mask
 *   - Pipeline_getBlendMode(self)                  : Query color blending mode
 *   - Pipeline_getCullMode(self)                   : Query polygon culling mode
 *   - Pipeline_isDepthTest(self)                   : Query depth testing status
 *   - Pipeline_isDepthWrite(self)                  : Query depth writing status
 *   - Pipeline_getShaderName(self)                 : Query shader entry identifier
 *   - Pipeline_getTypeId(self)                     : Query block type ID
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

Pipeline *Pipeline_0(void) {
    Pipeline *self = (Pipeline*) Memory_alloc(TYPE_PIPELINE_SINGLETON, sizeof(Pipeline));
    if (!self)
        self = (Pipeline*) calloc(1, sizeof(Pipeline));
    if (!self)
        return nullptr;
    (*self).stageMask = 0;
    (*self).blendMode = 0;
    (*self).cullMode = 0;
    (*self).depthTest = false;
    (*self).depthWrite = false;
    (*self).shaderName[0] = '\0';
    (*self).typeId = TYPE_PIPELINE_SINGLETON;
    return self;
}

Pipeline *Pipeline_1(const char *shaderName) {
    Pipeline *self = Pipeline_0();
    if (!self)
        return nullptr;
    Pipeline_setShaderName(self, shaderName);
    return self;
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

void Pipeline_free(Pipeline *self) {
    if (!self)
        return;
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void Pipeline_setStageMask(Pipeline *self, uint32_t stageMask) {
    if (!self)
        return;
    (*self).stageMask = stageMask;
}

;;SETTER
void Pipeline_setBlendMode(Pipeline *self, uint32_t blendMode) {
    if (!self)
        return;
    (*self).blendMode = blendMode;
}

;;SETTER
void Pipeline_setCullMode(Pipeline *self, uint32_t cullMode) {
    if (!self)
        return;
    (*self).cullMode = cullMode;
}

;;SETTER
void Pipeline_setDepthTest(Pipeline *self, bool depthTest) {
    if (!self)
        return;
    (*self).depthTest = depthTest;
}

;;SETTER
void Pipeline_setDepthWrite(Pipeline *self, bool depthWrite) {
    if (!self)
        return;
    (*self).depthWrite = depthWrite;
}

;;SETTER
void Pipeline_setShaderName(Pipeline *self, const char *shaderName) {
    if (!self)
        return;
    if (!shaderName) {
        (*self).shaderName[0] = '\0';
        return;
    }
    strncpy((*self).shaderName, shaderName, sizeof((*self).shaderName) - 1);
    (*self).shaderName[sizeof((*self).shaderName) - 1] = '\0';
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
uint32_t Pipeline_getStageMask(const Pipeline *self) {
    return self ? (*self).stageMask : 0;
}

;;GETTER
uint32_t Pipeline_getBlendMode(const Pipeline *self) {
    return self ? (*self).blendMode : 0;
}

;;GETTER
uint32_t Pipeline_getCullMode(const Pipeline *self) {
    return self ? (*self).cullMode : 0;
}

;;GETTER
bool Pipeline_isDepthTest(const Pipeline *self) {
    return self ? (*self).depthTest : false;
}

;;GETTER
bool Pipeline_isDepthWrite(const Pipeline *self) {
    return self ? (*self).depthWrite : false;
}

;;GETTER
const char *Pipeline_getShaderName(const Pipeline *self) {
    return self ? (*self).shaderName : nullptr;
}

;;GETTER
uint64_t Pipeline_getTypeId(const Pipeline *self) {
    return self ? (*self).typeId : 0;
}
