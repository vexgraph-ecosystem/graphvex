#version 450
// Explicit rectangular gallery/group scopes. Inputs 0..2 are straight sRGB
// source textures (Vulkan sampling decodes sRGB); input 3 is premultiplied float
// scatter accumulation. Phase 0 isolates decoration+clipped foreground. Phase 1
// composes: backdrop prefix replacement, clipped foreground, or expanded element.
// Output phase 0 is float linear; phase 1 targets sRGB with an opaque prior scene.
layout(set=0,binding=0) uniform sampler2D priorImage;
layout(set=0,binding=1) uniform sampler2D decorationImage;
layout(set=0,binding=2) uniform sampler2D foregroundImage;
layout(set=0,binding=3) uniform sampler2D filteredImage;
layout(push_constant) uniform ScopePush {
    int scope;
    int phase;
    ivec2 panelOrigin;
    ivec2 panelExtent;
    ivec2 foregroundOrigin;
    int radius;
} pc;
layout(location=0) out vec4 outputColor;

vec4 readStraight(sampler2D image, ivec2 point) {
    ivec2 extent = textureSize(image,0);
    if (any(lessThan(point,ivec2(0))) || any(greaterThanEqual(point,extent)))
        return vec4(0);
    vec4 value = texelFetch(image,point,0);
    value.rgb *= value.a;
    return value;
}
vec4 readFiltered(ivec2 point) {
    ivec2 extent = textureSize(filteredImage,0);
    if (any(lessThan(point,ivec2(0))) || any(greaterThanEqual(point,extent)))
        return vec4(0);
    vec4 value = texelFetch(filteredImage,point,0);
    value.a = clamp(value.a,0.0,1.0); // finite-kernel accumulation roundoff only
    return value;
}
vec4 over(vec4 source, vec4 dest) { return source + dest * (1.0-source.a); }
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    bool inside = all(greaterThanEqual(pixel,pc.panelOrigin)) &&
                  all(lessThan(pixel,pc.panelOrigin+pc.panelExtent));
    vec4 prior = readStraight(priorImage,pixel);
    vec4 group = vec4(0);
    if (inside) {
        vec4 decoration = readStraight(decorationImage,pixel-pc.panelOrigin);
        vec4 foreground = readStraight(foregroundImage,pixel-pc.foregroundOrigin);
        if (pc.phase == 1 && pc.scope == 1)
            foreground = readFiltered(pixel-pc.foregroundOrigin+pc.radius);
        group = over(foreground,decoration);
    }
    if (pc.phase == 0) {
        outputColor = group;
        return;
    }
    if (pc.scope == 0 && inside)
        prior = readFiltered(pixel+pc.radius); // replace prefix; never double it
    if (pc.scope == 2)
        group = readFiltered(pixel+pc.radius); // halo not clipped to event rectangle
    outputColor = over(group,prior);
    outputColor.a = clamp(outputColor.a,0.0,1.0);
    // Image/Picture bridge is straight sRGB RGBA8, not premultiplied storage.
    outputColor.rgb = outputColor.a > 0.0 ? outputColor.rgb/outputColor.a : vec3(0);
}
