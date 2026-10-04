#version 450
// Prototype linear-premultiplied resolve, not sRGB presentation. Mode 0 is fixed
// normalized scatter with transparent outside: retain edge fade, no division.
// Mode 1 explicitly renormalizes surviving logical-domain weight; use only when
// that boundary policy is requested. Host supplies separate attachment images,
// synchronization between accumulation/sampling, valid mode (0/1) and extents.
// Final source-over uses ONE / ONE_MINUS_SRC_ALPHA, not additive blending.
layout(set = 0, binding = 0) uniform sampler2D accumulatedColor;
layout(set = 0, binding = 1) uniform sampler2D accumulatedWeight;
layout(push_constant) uniform ResolvePush {
    uint normalizationMode;
} pc;
layout(location = 0) out vec4 resolvedColor;

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 color = texelFetch(accumulatedColor, pixel, 0);
    if (pc.normalizationMode == 1u) {
        float weight = texelFetch(accumulatedWeight, pixel, 0).r;
        color = weight > 0.0 ? color / weight : vec4(0);
    }
    // Fixed kernels/valid sources guarantee alpha <= 1 mathematically. Clamp
    // accumulation roundoff only; this is not a variable-radius correction.
    color.a = clamp(color.a, 0.0, 1.0);
    resolvedColor = color.a > 0.0 ? color : vec4(0);
}
