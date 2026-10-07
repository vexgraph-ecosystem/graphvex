#ifndef GRAPHVEX_GPU_SCOPE_H
#define GRAPHVEX_GPU_SCOPE_H
#include "vulkan/device.h"
#include "image.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct GpuScope GpuScope;
enum { GPU_SCOPE_BACKDROP, GPU_SCOPE_FOREGROUND, GPU_SCOPE_ELEMENT };
/* Cold explicit rectangular GPU scope compositor. Borrow Device; shaderDir
 * contains b-generated scatter.vert/frag, resolve.vert and scope.frag SPIR-V.
 * maxPixels is a caller safety/work budget, not a hidden entity ceiling.
 * No CPU filtering/composition, no fallback. Source upload and final readback
 * are transport for the current Image/Picture bridge, not pixel algorithms.
 * Calls require external synchronization. */
GpuScope *GpuScope_3(Device *device, const char *shaderDir, uint32_t maxPixels);
GpuScope *GpuScope_0(void);
#define GRAPHVEX_GPU_SCOPE_CTOR(_0,_1,_2,_3,NAME,...) NAME
#define GpuScope(...) GRAPHVEX_GPU_SCOPE_CTOR(0 __VA_OPT__(,) __VA_ARGS__, \
    GpuScope_3,GpuScope_invalidArity,GpuScope_invalidArity,GpuScope_0)(__VA_ARGS__)
GpuScope *GpuScope_zero(void);
bool GpuScope_isPending(const GpuScope *self);
/* Waits at most 100ms. False leaves object and in-flight resources alive; retry
 * after completion/device recovery. Never destroy borrowed Device first. */
bool GpuScope_destroy(GpuScope *self);
/* prior: opaque straight sRGB RGBA8 Image; decoration must fit inside prior;
 * foreground can extend outside panel and is clipped before group filtering.
 * Scope/radius validated cold; current finite box kernel radius <=16 native px.
 * On failure *out is unchanged, no borrowed image is modified. Output transfers
 * ownership on success. A GPU timeout retains the job on self, not freed early.
 * Device supports sampled/color-attachment/blend RGBA32F and sampled/sRGB target
 * formats, probed once at construction. No rounded-mask/automatic tree contract. */
bool GpuScope_render(GpuScope *self, unsigned scope, const Image *prior,
    const Image *decoration, int32_t panelX, int32_t panelY,
    const Image *foreground, int32_t foregroundX, int32_t foregroundY,
    uint32_t radius, Image **out);
/* Same validated GPU scope, but transfers a GPU-only drawable Image: no output
 * staging/readback/CPU shadow. Its sampled texture outlives the scope, borrows
 * the same Device, and must be released before that Device. Consumers retain
 * resources through submission completion. Use render for numeric readback. */
bool GpuScope_renderSampled(GpuScope *self, unsigned scope, const Image *prior,
    const Image *decoration, int32_t panelX, int32_t panelY,
    const Image *foreground, int32_t foregroundX, int32_t foregroundY,
    uint32_t radius, Image **out);
void GpuScope_toString(const GpuScope *self,char *dest,size_t cap,bool *outTruncated);
void GpuScope_toStringStruct(const GpuScope *self,char *dest,size_t cap,bool *outTruncated);
#endif
