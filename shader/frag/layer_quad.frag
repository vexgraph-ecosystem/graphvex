#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform Push {
    layout(offset = 16) vec4 u_color; // tint rgba
} push;

layout(set = 0, binding = 0) uniform sampler2D u_layer;

void main() {
    // Flip Y: Vulkan's top-down image vs CoreAnimation's bottom-up space.
    vec4 texColor = texture(u_layer, vec2(v_uv.x, 1.0 - v_uv.y));
    fragColor = texColor * push.u_color;
}