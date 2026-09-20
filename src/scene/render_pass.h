#ifndef SCENE_PASS_H
#define SCENE_PASS_H

#include <stdbool.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "graphvex/type.h"
#include "image/image.h"

struct CommandBuffer;

// scene/pass.h — One render pass instance (shadowmap, scene, UI).

typedef struct Pass {
    uint32_t passType;
    uint32_t width;
    uint32_t height;
    uint32_t clearColor;
    float clearDepth;
    bool clearOnLoad;
    uint64_t typeId;
    Image *target; // borrowed target Image
} Pass;

// Constructors
Pass *Pass_0(void);
Pass *Pass_3(uint32_t passType, uint32_t width, uint32_t height);

// Core functions
void Pass_free(Pass *self);
void Pass_begin(Pass *self, struct CommandBuffer *cb);
void Pass_end(Pass *self, struct CommandBuffer *cb);

// Setters
void Pass_setPassType(Pass *self, uint32_t passType);
void Pass_setWidth(Pass *self, uint32_t width);
void Pass_setHeight(Pass *self, uint32_t height);
void Pass_setClearColor(Pass *self, uint32_t clearColor);
void Pass_setClearDepth(Pass *self, float clearDepth);
void Pass_setClearOnLoad(Pass *self, bool clearOnLoad);
void Pass_setTarget(Pass *self, Image *target);

// Getters
uint32_t Pass_getPassType(const Pass *self);
uint32_t Pass_getWidth(const Pass *self);
uint32_t Pass_getHeight(const Pass *self);
uint32_t Pass_getClearColor(const Pass *self);
float Pass_getClearDepth(const Pass *self);
bool Pass_isClearOnLoad(const Pass *self);
Image *Pass_getTarget(const Pass *self);
uint64_t Pass_getTypeId(const Pass *self);

#define Pass(...) CONSTRUCTOR_DISPATCH(Pass, __VA_ARGS__)

#endif
