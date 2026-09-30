#version 450
// graphvex R3 — vulkan/shaders/quad.vert
// Forward quad pass. One vertex buffer of rectangles; px -> NDC in the shader,
// Y-down top-left (the Native Pixel Law: NDC exists only here).

layout(push_constant) uniform Push {
    vec2 viewport;   // native px
} pc;

layout(location = 0) in vec2 inPos;     // native px, Y-down
layout(location = 1) in vec2 inUV;      // 0..1 (images) / 0..1 (glyph mask)
layout(location = 2) in vec4 inFill;    // 0xRRGGBBAA -> linear 0..1
layout(location = 3) in vec4 inBorder;
layout(location = 4) in vec4 inParams;  // x=radius px, y=stroke px, z=mode, w=layer
layout(location = 5) in vec2 inSize;    // quad size px

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vFill;
layout(location = 2) out vec4 vBorder;
layout(location = 3) out vec4 vParams;
layout(location = 4) out vec2 vLocal;   // px within the quad
layout(location = 5) out vec2 vSize;

void main() {
    // native px, Y-down (top-left) -> Vulkan NDC (Y-up); NDC lives ONLY here.
    vec2 ndc = vec2(inPos.x / pc.viewport.x * 2.0 - 1.0,
                    inPos.y / pc.viewport.y * 2.0 - 1.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = inUV;
    vFill = inFill;
    vBorder = inBorder;
    vParams = inParams;
    vSize = inSize;
    vLocal = inUV * inSize;   // uv runs 0..1 across the quad
}
