#version 450
// graphvex R3 — vulkan/shaders/quad.frag
// Everything the UI draws is a rounded rectangle, an image, or a glyph mask.
// One fragment shader covers all three: an SDF for the rounded corner + border,
// a texture sample for image/glyph modes, and plain fill otherwise.
//
// Antialiased: the coverage transition is one *pixel* wide in screen space
// (fwidth), so curves read smooth while the straight edges stay sharp.

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vFill;
layout(location = 2) in vec4 vBorder;
layout(location = 3) in vec4 vParams;   // x=radius, y=stroke, z=mode, w=layer
layout(location = 4) in vec2 vLocal;
layout(location = 5) in vec2 vSize;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2DArray uAtlas;

// signed distance to a rounded box centred at the origin, half-extent = hs
float sdRoundBox(vec2 p, vec2 hs, float r) {
    vec2 q = abs(p) - hs + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

void main() {
    vec2 p = vLocal - vSize * 0.5;          // centred
    vec2 hs = vSize * 0.5;
    float radius = min(vParams.x, min(hs.x, hs.y));
    float d = sdRoundBox(p, hs, radius);

    // one-pixel AA in screen space — smooth curve, sharp straight edge
    float aa = max(fwidth(d), 0.0001);
    float coverage = 1.0 - smoothstep(-aa, aa, d);
    if (coverage <= 0.0) discard;

    float stroke = vParams.y;
    float mode = vParams.z;

    vec4 base = vFill;
    if (mode > 0.5) {
        vec4 tex = texture(uAtlas, vec3(vUV, vParams.w));
        base = mode > 1.5 ? vec4(vFill.rgb, vFill.a * tex.a) : tex * vFill;
    }

    // border: inside the shape, within `stroke` of the edge (AA'd too)
    if (stroke > 0.0) {
        float inner = 1.0 - smoothstep(-stroke - aa, -stroke + aa, d);
        base = mix(base, vBorder, vBorder.a * inner);
    }

    outColor = vec4(base.rgb, base.a * coverage);
}
