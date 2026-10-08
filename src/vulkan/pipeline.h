#ifndef GRAPHICS_PIPELINE_H
#define GRAPHICS_PIPELINE_H

#include <stdbool.h>
#include <stdint.h>

// graphvex R3 — vulkan/pipeline.h
//
// ONE pipeline draws everything: rounded rects, images, glyph masks. It owns
// the vertex/fragment SPIR-V (compiled by `b` from vulkan/shaders/*.vert|frag)
// and the fixed state. No swapchain, no render pass tied to a window — the
// pipeline renders into whatever Image/Board is the current target.

typedef struct Pipeline Pipeline;

typedef struct PipelineDesc {
    const void *vertSpirv;    // SPIR-V words
    uint32_t vertSize;        // Bytes
    const void *fragSpirv;
    uint32_t fragSize;
    uint32_t vertexStride;    // Bytes per vertex (VK_VERTEX_FLOATS * 4)
    uint32_t pushConstantSize;// e.g. sizeof(vec2 viewport)
} PipelineDesc;

Pipeline *Pipeline_new(const PipelineDesc *desc);

// Convenience: load the two .spv files produced by `b` (glslangValidator).
Pipeline *Pipeline_fromFiles(const char *vertSpvPath, const char *fragSpvPath,
                             uint32_t vertexStride, uint32_t pushConstantSize);

void Pipeline_destroy(Pipeline *pipeline);
bool Pipeline_isValid(const Pipeline *pipeline);
uint32_t Pipeline_vertexStride(const Pipeline *pipeline);
void *Pipeline_native(const Pipeline *pipeline);   // opaque VkPipeline (once bound)

#endif // GRAPHICS_PIPELINE_H
