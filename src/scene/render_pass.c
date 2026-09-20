#include "scene/pass.h"

#include <stdlib.h>

#include "annotation/overview.h"
#include "nio/mem.h"
#include "oop/type.h"
#include "sync/command_buffer.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Pass (scene/pass.c)
 * LEVEL: L2 — Behavior (render pass instance lifecycle and CPU stubs)
 * ============================================================================
 * One render pass instance (shadowmap, scene, UI). Encapsulates pass
 * configuration, clear values, dimensions, and borrowed target Image.
 *
 * STRUCT FIELDS (Mirroring scene/pass.h):
 * ----------------------------------------------------------------------------
 *   Pass {
 *     uint32_t passType;      // render pass category or type code
 *     uint32_t width;         // pass render width in pixels
 *     uint32_t height;        // pass render height in pixels
 *     uint32_t clearColor;    // packed clear color (0xAARRGGBB)
 *     float clearDepth;       // depth attachment clear value
 *     bool clearOnLoad;       // true if attachments are cleared on load
 *     uint64_t typeId;        // block-header type id (TYPE_PASS_SINGLETON)
 *     Image *target;          // borrowed target Image (never freed by Pass)
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Pass()                               : Pass_0()
 *   - Pass(passType, width, height)        : Pass_3(passType, width, height)
 *
 * Core Functions:
 *   - Pass_free(self)
 *   - Pass_begin(self, cb)
 *   - Pass_end(self, cb)
 *
 * Setters:
 *   - Pass_setPassType(self, passType)
 *   - Pass_setWidth(self, width)
 *   - Pass_setHeight(self, height)
 *   - Pass_setClearColor(self, clearColor)
 *   - Pass_setClearDepth(self, clearDepth)
 *   - Pass_setClearOnLoad(self, clearOnLoad)
 *   - Pass_setTarget(self, target)
 *
 * Getters:
 *   - Pass_getPassType(self)
 *   - Pass_getWidth(self)
 *   - Pass_getHeight(self)
 *   - Pass_getClearColor(self)
 *   - Pass_getClearDepth(self)
 *   - Pass_isClearOnLoad(self)
 *   - Pass_getTarget(self)
 *   - Pass_getTypeId(self)
 * ============================================================================
 */

// CONSTRUCTORS

Pass *Pass_0(void) {
    return Pass_3(0, 0, 0);
}

Pass *Pass_3(uint32_t passType, uint32_t width, uint32_t height) {
    Pass *self = (Pass*) Memory_alloc(TYPE_PASS_SINGLETON, sizeof(Pass));
    if (!self)
        self = (Pass*) calloc(1, sizeof(Pass));
    if (!self)
        return nullptr;
    (*self).passType = passType;
    (*self).width = width;
    (*self).height = height;
    (*self).clearColor = 0;
    (*self).clearDepth = 1.0f;
    (*self).clearOnLoad = true;
    (*self).typeId = TYPE_PASS_SINGLETON;
    (*self).target = nullptr;
    return self;
}

// CORE FUNCTIONS

void Pass_free(Pass *self) {
    if (!self)
        return;
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

void Pass_begin(Pass *self, struct CommandBuffer *cb) {
    if (!self || !cb)
        return;
    (void)self;
    (void)cb;
}

void Pass_end(Pass *self, struct CommandBuffer *cb) {
    if (!self || !cb)
        return;
    (void)self;
    (void)cb;
}

// SETTERS

void Pass_setPassType(Pass *self, uint32_t passType) {
    if (!self)
        return;
    (*self).passType = passType;
}

void Pass_setWidth(Pass *self, uint32_t width) {
    if (!self)
        return;
    (*self).width = width;
}

void Pass_setHeight(Pass *self, uint32_t height) {
    if (!self)
        return;
    (*self).height = height;
}

void Pass_setClearColor(Pass *self, uint32_t clearColor) {
    if (!self)
        return;
    (*self).clearColor = clearColor;
}

void Pass_setClearDepth(Pass *self, float clearDepth) {
    if (!self)
        return;
    (*self).clearDepth = clearDepth;
}

void Pass_setClearOnLoad(Pass *self, bool clearOnLoad) {
    if (!self)
        return;
    (*self).clearOnLoad = clearOnLoad;
}

void Pass_setTarget(Pass *self, Image *target) {
    if (!self)
        return;
    (*self).target = target;
}

// GETTERS

uint32_t Pass_getPassType(const Pass *self) {
    return self ? (*self).passType : 0;
}

uint32_t Pass_getWidth(const Pass *self) {
    return self ? (*self).width : 0;
}

uint32_t Pass_getHeight(const Pass *self) {
    return self ? (*self).height : 0;
}

uint32_t Pass_getClearColor(const Pass *self) {
    return self ? (*self).clearColor : 0;
}

float Pass_getClearDepth(const Pass *self) {
    return self ? (*self).clearDepth : 0.0f;
}

bool Pass_isClearOnLoad(const Pass *self) {
    return self ? (*self).clearOnLoad : false;
}

Image *Pass_getTarget(const Pass *self) {
    return self ? (*self).target : nullptr;
}

uint64_t Pass_getTypeId(const Pass *self) {
    return self ? (*self).typeId : 0;
}
