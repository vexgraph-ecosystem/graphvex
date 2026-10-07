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
layout(location = 6) in vec4 vClip;     // local clip bounds (x0,y0,x1,y1)
layout(location = 7) in float vBlur;    // soft-edge falloff, px
layout(location = 8) in float vClipRadius;  // clip corner radius (0 = rect)

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2DArray uAtlas;

// signed distance to a rounded box centred at the origin, half-extent = hs
float sdRoundBox(vec2 p, vec2 hs, float r) {
    vec2 q = abs(p) - hs + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

void main() {
    // CLIP = DISCARD. The quad always keeps its true size, so the corner is
    // computed for the full box; pixels past the clip window are simply cut.
    if (vLocal.x < vClip.x || vLocal.y < vClip.y ||
        vLocal.x > vClip.z || vLocal.y > vClip.w)
        discard;

    // ROUNDED MASK: a parent's corner radius clips its children for real. The
    // clip's rect + radius are in this quad's local space, so one SDF discards
    // the corner pixels the rect scissor would have kept.
    if (vClipRadius > 0.0) {
        vec2 ccenter = vec2((vClip.x + vClip.z) * 0.5, (vClip.y + vClip.w) * 0.5);
        vec2 chs = vec2((vClip.z - vClip.x) * 0.5, (vClip.w - vClip.y) * 0.5);
        float cr = min(vClipRadius, min(chs.x, chs.y));
        if (sdRoundBox(vLocal - ccenter, chs, cr) > 0.0) discard;
    }

    // CPU-shadow color runs already describe pixel coverage. A one-pixel run
    // must not lose energy to another shape AA pass; ancestor clips still apply.
    if (vParams.z < 0.0) {
        outColor = vFill;
        return;
    }
    // Images already have exact pixel coverage. One textured quad replaces the
    // old CPU color-run expansion; only ancestor clips affect its coverage.
    if (vParams.z > 0.5 && vParams.z < 1.5) {
        outColor = texture(uAtlas, vec3(vUV, 0.0)) * vFill;
        return;
    }

    vec2 p = vLocal - vSize * 0.5;          // centred
    // The quad already carries the blur margin, so the SHAPE is inset by it.
    float blur = max(vBlur, 0.0);
    vec2 hs = max(vSize * 0.5 - vec2(blur), vec2(0.0));
    float radius = min(vParams.x, min(hs.x, hs.y));
    float d = sdRoundBox(p, hs, radius);

    // coverage: a soft falloff CENTRED on the shape edge (1 inside, 0.5 at the
    // edge, 0 at +blur). A WIDER blur spreads the same panel over more pixels,
    // so its colour gets weaker — the higher the blur, the fainter.
    float aa = max(fwidth(d), 0.0001);
    float coverage;
    if (blur > 0.0) {
        coverage = 1.0 - smoothstep(-blur, blur, d);
        coverage *= 24.0 / (24.0 + blur);   // energy spread thin
    } else {
        coverage = 1.0 - smoothstep(-aa, aa, d);
    }
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
        float edge = smoothstep(-stroke - aa, -stroke + aa, d);
        base = mix(base, vBorder, vBorder.a * edge);
    }

    outColor = vec4(base.rgb, base.a * coverage);
}
