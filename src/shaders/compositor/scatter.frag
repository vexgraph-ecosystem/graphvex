#version 450
// Both float attachments are cleared to zero and require ONE/ONE ADD blending.
// Attachment 0 accumulates premultiplied RGBA, attachment 1 records surviving
// source-domain weight. Transparent source texels still contribute weight.
// Weight is diagnostic for fixed transparent-outside kernels; blindly dividing
// by it would remove the intentional edge fade. No source-over per splat.
layout(location = 0) flat in vec4 weightedColor;
layout(location = 1) flat in float kernelWeight;
layout(location = 0) out vec4 accumulatedColor;
layout(location = 1) out float accumulatedWeight;

void main() {
    accumulatedColor = weightedColor;
    accumulatedWeight = kernelWeight;
}
