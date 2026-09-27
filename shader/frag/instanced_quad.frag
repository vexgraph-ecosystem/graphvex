#version 450

layout(std430, set = 0, binding = 0) readonly buffer Rectangles {
    vec4 rows[];
} data;
layout(location = 0) flat in int rectIndex;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = data.rows[rectIndex * 2 + 1];
}
