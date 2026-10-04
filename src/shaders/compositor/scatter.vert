#version 450
// Prototype entrypoint, not yet bound by VulkanBackend. Each source texel emits
// one six-vertex footprint. Host validates nonzero extents, radius <= 16,
// instanceCount == sourceWidth*sourceHeight, target == source+2*radius, and float
// sampled/attachment support. Source is linear-light premultiplied RGBA.
// Expanded target origin is sourceWorldOrigin-radius: allocation never moves
// the source's world placement. Positive-height Vulkan viewport, native Y-down.
layout(set = 0, binding = 0) uniform sampler2D sourceImage;
layout(push_constant) uniform ScatterPush {
    ivec2 sourceExtent;
    ivec2 targetExtent;
    int radius;
} pc;
layout(location = 0) flat out vec4 weightedColor;
layout(location = 1) flat out float kernelWeight;

const vec2 corners[6] = vec2[6](
    vec2(0, 0), vec2(1, 0), vec2(1, 1),
    vec2(0, 0), vec2(1, 1), vec2(0, 1));

void main() {
    ivec2 source = ivec2(gl_InstanceIndex % pc.sourceExtent.x,
                        gl_InstanceIndex / pc.sourceExtent.x);
    float diameter = float(2 * pc.radius + 1);
    vec2 position = vec2(source) + corners[gl_VertexIndex] * diameter;
    gl_Position = vec4(position / vec2(pc.targetExtent) * 2.0 - 1.0, 0, 1);
    kernelWeight = 1.0 / (diameter * diameter);
    weightedColor = texelFetch(sourceImage, source, 0) * kernelWeight;
}
