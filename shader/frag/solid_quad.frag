#version 450

// Solid panel fill: color arrives as the second push-constant slot (offset 16,
// fragment stage) — straight 0xRRGGBBAA decoded to RGBA on the CPU.

layout(push_constant) uniform Push {
    layout(offset = 16) vec4 u_color;
} push;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = push.u_color;
}
