#ifndef GRAPHVEX_COLOR_PASS_H
#define GRAPHVEX_COLOR_PASS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "filter/filter_functions.h"
#include "vulkan/device.h"

typedef struct ColorPass ColorPass;

/* Cold pipeline creation. Borrow a live Device and compatible one-color
 * render pass (subpass 0, sample count 1), plus aligned SPIR-V words. No window
 * ownership. Every failed construction releases its partial resources.
 * Opaque native handles are Vulkan transit only; no driver types in clients.
 * Caller owns synchronization and must retire all submitted uses before free.
 * No CPU fallback. */
ColorPass *ColorPass_6(Device *device, void *nativeRenderPass,
                       const uint32_t *vertexWords, size_t vertexBytes,
                       const uint32_t *fragmentWords, size_t fragmentBytes);
ColorPass *ColorPass_0(void);
#define GRAPHVEX_COLOR_PASS_CTOR(_0,_1,_2,_3,_4,_5,_6,NAME,...) NAME
#define ColorPass(...) GRAPHVEX_COLOR_PASS_CTOR(0 __VA_OPT__(,) __VA_ARGS__, \
    ColorPass_6, ColorPass_invalidArity, ColorPass_invalidArity, ColorPass_invalidArity, \
    ColorPass_invalidArity, ColorPass_invalidArity, ColorPass_0)(__VA_ARGS__)
ColorPass *ColorPass_zero(void);
void ColorPass_destroy(ColorPass *self);
void *ColorPass_getDescriptorLayout(const ColorPass *self);
bool ColorPass_validateToken(FilterToken token);

/* Caller allocates binding-0 combined-image-sampler descriptor matching the
 * queried layout; source is a completed linear-premultiplied float texture in
 * SHADER_READ_ONLY_OPTIMAL. Source and destination must be distinct, same size.
 * Caller probes sampled/color format capabilities and device extent limits at
 * cold preparation; Vulkan resource validity/format compatibility is a precondition.
 * Call inside a compatible render pass, with externally synchronized borrowed
 * command buffer and descriptor. Records viewport/scissor/bind/push/draw only,
 * no allocations, waits or submits. One call is one ordered operation; stacks
 * ping-pong separate images with dependencies between passes. Invalid token or
 * zero extent rejects before recording. Vulkan handle validity is a caller
 * precondition, not something this seam can infer from an arbitrary address. */
bool ColorPass_record(const ColorPass *self, void *nativeCommandBuffer,
                      void *nativeDescriptorSet, uint32_t width, uint32_t height,
                      FilterToken token);
void ColorPass_toString(const ColorPass *self, char *dest, size_t cap, bool *outTruncated);
void ColorPass_toStringStruct(const ColorPass *self, char *dest, size_t cap, bool *outTruncated);
#endif
