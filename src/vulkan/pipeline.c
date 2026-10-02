#include "vulkan/pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// graphvex R3 — vulkan/pipeline.c
// Owns the shader modules + fixed state. The VkPipeline itself is created once
// a Device is bound (next slice); until then this is the shader/state record.

struct Pipeline {
    void *vert;         // SPIR-V bytes
    uint32_t vertSize;
    void *frag;
    uint32_t fragSize;
    uint32_t vertexStride;
    uint32_t pushConstantSize;
    void *native;       // opaque VkPipeline
};

static void *slurp(const char *path, uint32_t *outSize) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    void *buf = malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) *outSize = (uint32_t)n;
    return buf;
}

Pipeline *Pipeline_new(const PipelineDesc *desc) {
    if (!desc || !(*desc).vertSpirv || !(*desc).fragSpirv) return NULL;
    Pipeline *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    (*p).vert = malloc((*desc).vertSize);
    (*p).frag = malloc((*desc).fragSize);
    if (!(*p).vert || !(*p).frag) {
        Pipeline_destroy(p);
        return NULL;
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
        return NULL;
    }
    PipelineDesc d = {v, vs, f, fs, vertexStride, pushConstantSize};
    Pipeline *p = Pipeline_new(&d);
    free(v);
    free(f);
    return p;
}

void Pipeline_destroy(Pipeline *pipeline) {
    if (!pipeline) return;
    free((*pipeline).vert);
    free((*pipeline).frag);
    free(pipeline);
}

bool Pipeline_isValid(const Pipeline *pipeline) {
    return pipeline && (*pipeline).vert && (*pipeline).frag && (*pipeline).vertexStride > 0;
}

uint32_t Pipeline_vertexStride(const Pipeline *pipeline) {
    return pipeline ? (*pipeline).vertexStride : 0u;
}

void *Pipeline_native(const Pipeline *pipeline) { return pipeline ? (*pipeline).native : NULL; }
