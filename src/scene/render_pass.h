#ifndef SCENE_RENDER_PASS_H
#define SCENE_RENDER_PASS_H

#include <stdbool.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "graphvex/type.h"
#include "image/image.h"

struct CommandBuffer;

// scene/render_pass.h — One render pass instance (shadowmap, scene, UI).

typedef struct RenderPass {
    uint32_t passType;
    uint32_t width;
    uint32_t height;
    uint32_t clearColor;
    float clearDepth;
    bool clearOnLoad;
    uint64_t typeId;
    Image *target; // borrowed target Image
} RenderPass;

// Backward-compatible alias
typedef RenderPass Pass;

// Constructors
RenderPass *RenderPass_0(void);
RenderPass *RenderPass_3(uint32_t passType, uint32_t width, uint32_t height);

// Core functions
void RenderPass_free(RenderPass *self);
void RenderPass_begin(RenderPass *self, struct CommandBuffer *cb);
void RenderPass_end(RenderPass *self, struct CommandBuffer *cb);

// Setters
void RenderPass_setPassType(RenderPass *self, uint32_t passType);
void RenderPass_setWidth(RenderPass *self, uint32_t width);
void RenderPass_setHeight(RenderPass *self, uint32_t height);
void RenderPass_setClearColor(RenderPass *self, uint32_t clearColor);
void RenderPass_setClearDepth(RenderPass *self, float clearDepth);
void RenderPass_setClearOnLoad(RenderPass *self, bool clearOnLoad);
void RenderPass_setTarget(RenderPass *self, Image *target);

// Getters
uint32_t RenderPass_getPassType(const RenderPass *self);
uint32_t RenderPass_getWidth(const RenderPass *self);
uint32_t RenderPass_getHeight(const RenderPass *self);
uint32_t RenderPass_getClearColor(const RenderPass *self);
float RenderPass_getClearDepth(const RenderPass *self);
bool RenderPass_isClearOnLoad(const RenderPass *self);
Image *RenderPass_getTarget(const RenderPass *self);
uint64_t RenderPass_getTypeId(const RenderPass *self);

#define RenderPass(...) CONSTRUCTOR_DISPATCH(RenderPass, __VA_ARGS__)

// Backward compatibility macros
#define Pass_0 RenderPass_0
#define Pass_3 RenderPass_3
#define Pass_free RenderPass_free
#define Pass_begin RenderPass_begin
#define Pass_end RenderPass_end
#define Pass_setPassType RenderPass_setPassType
#define Pass_setWidth RenderPass_setWidth
#define Pass_setHeight RenderPass_setHeight
#define Pass_setClearColor RenderPass_setClearColor
#define Pass_setClearDepth RenderPass_setClearDepth
#define Pass_setClearOnLoad RenderPass_setClearOnLoad
#define Pass_setTarget RenderPass_setTarget
#define Pass_getPassType RenderPass_getPassType
#define Pass_getWidth RenderPass_getWidth
#define Pass_getHeight RenderPass_getHeight
#define Pass_getClearColor RenderPass_getClearColor
#define Pass_getClearDepth RenderPass_getClearDepth
#define Pass_isClearOnLoad RenderPass_isClearOnLoad
#define Pass_getTarget RenderPass_getTarget
#define Pass_getTypeId RenderPass_getTypeId
#define Pass(...) CONSTRUCTOR_DISPATCH(RenderPass, __VA_ARGS__)

#endif
