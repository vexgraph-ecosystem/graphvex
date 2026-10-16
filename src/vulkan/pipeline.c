#include "vulkan/pipeline.h"
#include "annotation/definition.h"
#include "annotation/overview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

;;DEFINITION
/** Pipeline owns copied vertex and fragment SPIR-V bytes plus the immutable
 * vertex/push-constant geometry needed by a renderer. The current unit does not
 * create or bind a native VkPipeline; native is an opaque reserved projection. */
;;OVERVIEW
/** ========================================================================
 * CLASS: Pipeline (vulkan/pipeline.c)
 * STRUCT FIELDS (Mirroring Pipeline)
 * void *vert                     owned vertex SPIR-V byte copy
 * uint32_t vertSize              vertex byte length
 * void *frag                     owned fragment SPIR-V byte copy
 * uint32_t fragSize              fragment byte length
 * uint32_t vertexStride          configured bytes per vertex
 * uint32_t pushConstantSize      configured push-constant byte extent
 * void *native                   reserved opaque native pipeline handle
 * FUNCTION REGISTRY
 * Constructors: Pipeline *Pipeline_new(const PipelineDesc *desc)
 *   Pipeline *Pipeline_fromFiles(const char *vertSpvPath,
 *     const char *fragSpvPath, uint32_t vertexStride, uint32_t pushConstantSize)
 * Lifecycle: void Pipeline_destroy(Pipeline *pipeline)
 * Projections: bool Pipeline_isValid(const Pipeline *pipeline)
 *   uint32_t Pipeline_vertexStride(const Pipeline *pipeline)
 *   void *Pipeline_native(const Pipeline *pipeline)
 * Private helper: slurp reads one nonempty shader file into owned bytes.
 * Admission, failure and ownership contract:
 * Constructors copy supplied/file bytes; temporary file buffers are freed after
 * construction. Rejection returns nullptr and preserves no partial Pipeline.
 * Destroy accepts nullptr. Native pipeline creation is not implemented here.
 * ======================================================================== */

struct Pipeline {
    void *vert;         // SPIR-V Bytes
    uint32_t vertSize;
    void *frag;
    uint32_t fragSize;
    uint32_t vertexStride;
    uint32_t pushConstantSize;
    void *native;       // opaque VkPipeline
};

static void *slurp(const char *path, uint32_t *outSize) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return nullptr; }
    void *buf = malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = nullptr;
    }
    fclose(f);
    if (buf) *outSize = (uint32_t)n;
    return buf;
}

Pipeline *Pipeline_new(const PipelineDesc *desc) {
    if (!desc || !(*desc).vertSpirv || !(*desc).fragSpirv) return nullptr;
    Pipeline *p = calloc(1, sizeof *p);
    if (!p) return nullptr;
    (*p).vert = malloc((*desc).vertSize);
    (*p).frag = malloc((*desc).fragSize);
    if (!(*p).vert || !(*p).frag) {
        Pipeline_destroy(p);
        return nullptr;
    }
    memcpy((*p).vert, (*desc).vertSpirv, (*desc).vertSize);
    memcpy((*p).frag, (*desc).fragSpirv, (*desc).fragSize);
    (*p).vertSize = (*desc).vertSize;
    (*p).fragSize = (*desc).fragSize;
    (*p).vertexStride = (*desc).vertexStride;
    (*p).pushConstantSize = (*desc).pushConstantSize;
    return p;
}

Pipeline *Pipeline_fromFiles(const char *vertSpvPath, const char *fragSpvPath,
                             uint32_t vertexStride, uint32_t pushConstantSize) {
    uint32_t vs = 0, fs = 0;
    void *v = slurp(vertSpvPath, &vs);
    void *f = slurp(fragSpvPath, &fs);
    if (!v || !f) {
        free(v);
        free(f);
        return nullptr;
    }
    PipelineDesc d = {v, vs, f, fs, vertexStride, pushConstantSize};
    Pipeline *p = Pipeline_new(&d);
    free(v);
    free(f);
    return p;
}

// Destroys the owned pipeline and layout objects; null is ignored.
void Pipeline_destroy(Pipeline *pipeline) {
    if (!pipeline) return;
    free((*pipeline).vert);
    free((*pipeline).frag);
    free(pipeline);
}

// Reports whether the wrapper has vertex and fragment stages and a positive vertex stride.
bool Pipeline_isValid(const Pipeline *pipeline) {
    return pipeline && (*pipeline).vert && (*pipeline).frag && (*pipeline).vertexStride > 0;
}

// Returns the configured vertex stride, or zero when pipeline is null.
uint32_t Pipeline_vertexStride(const Pipeline *pipeline) {
    return pipeline ? (*pipeline).vertexStride : 0u;
}

void *Pipeline_native(const Pipeline *pipeline) { return pipeline ? (*pipeline).native : nullptr; }
