#version 450
// Fullscreen triangle for one resolve pass; no vertex buffer. Host draws three
// vertices with firstVertex zero and establishes target viewport/scissor.
void main() {
    vec2 corner = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(corner * 2.0 - 1.0, 0, 1);
}
