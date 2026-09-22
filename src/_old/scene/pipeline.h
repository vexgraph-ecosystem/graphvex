#ifndef SCENE_PIPELINE_H
#define SCENE_PIPELINE_H

#include <stdbool.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "../graphics/type.h"

// scene/pipeline.h — Compiled raster and compute state (shaders, blend, depth).

typedef struct Pipeline {
    uint32_t stageMask;
    uint32_t blendMode;
    uint32_t cullMode;
    bool depthTest;
    bool depthWrite;
    char shaderName[64];
    uint64_t typeId;
} Pipeline;

// Constructors
Pipeline *Pipeline_0(void);
Pipeline *Pipeline_1(const char *shaderName);

// Core functions
void Pipeline_free(Pipeline *self);

// Setters
void Pipeline_setStageMask(Pipeline *self, uint32_t stageMask);
void Pipeline_setBlendMode(Pipeline *self, uint32_t blendMode);
void Pipeline_setCullMode(Pipeline *self, uint32_t cullMode);
void Pipeline_setDepthTest(Pipeline *self, bool depthTest);
void Pipeline_setDepthWrite(Pipeline *self, bool depthWrite);
void Pipeline_setShaderName(Pipeline *self, const char *shaderName);

// Getters
uint32_t Pipeline_getStageMask(const Pipeline *self);
uint32_t Pipeline_getBlendMode(const Pipeline *self);
uint32_t Pipeline_getCullMode(const Pipeline *self);
bool Pipeline_isDepthTest(const Pipeline *self);
bool Pipeline_isDepthWrite(const Pipeline *self);
const char *Pipeline_getShaderName(const Pipeline *self);
uint64_t Pipeline_getTypeId(const Pipeline *self);

#define Pipeline(...) CONSTRUCTOR_DISPATCH(Pipeline, __VA_ARGS__)

#endif
