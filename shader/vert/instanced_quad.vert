#version 450

layout(std430, set = 0, binding = 0) readonly buffer Rectangles {
    vec4 rows[];
} data;
layout(location = 0) flat out int rectIndex;

const vec2 CORNERS[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    rectIndex = gl_InstanceIndex;
    vec4 rect = data.rows[gl_InstanceIndex * 2];
    vec2 p = rect.xy + CORNERS[gl_VertexIndex] * rect.zw;
    gl_Position = vec4(p, 0.0, 1.0);
}
