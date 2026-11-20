#version 440
#extension GL_GOOGLE_include_directive : require

// The horizontal half of the glass's blur (DESIGN.md 3.1c and 3.1d, docs/adr/0082, docs/adr/0201). The sheet is blurred
// along x into a mipmapped texture here, and glass.frag blurs that along y. One pass of twenty-five taps used to do both
// axes; thirteen taps per axis in two passes now read a prefilter one texel wide at quarter steps, under a Gaussian
// sampled at half its sigma (blur_common.glsl). The dense train is what keeps the kernel smooth between taps: taps whose
// tents merely touched used to draw the tap grid out along the blur axis as stripes (docs/adr/0111, docs/adr/0201).
//
// The weights are fixed because the tap spacing is a fixed fraction of the prefilter texel, not of sigma: the softness
// rides on which mip the taps read, so a slider move changes one uniform and the kernel shape not at all. Weight is a
// function of the index rather than a vector lookup: the HLSL qsb bakes does not carry a dynamically indexed constant
// vector over intact (docs/adr/0070).
//
// The pass covers the whole sheet, not just the card, so the vertical pass still has the sheet's margin to read when its
// outer taps and the refraction reach past the card's edge.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float blur;         // percent of the full scale; 100 is 60 DIP
    vec2 sheetSize;     // DIP
    vec2 sheetPixels;   // device pixels the sheet's texture covers; sheetSize / this is one texel in DIP
} ubuf;

layout(binding = 1) uniform sampler2D sheet;   // the desktop at the screen's resolution, transparent until it has loaded

#include "blur_common.glsl"

void main()
{
    float sigma = ubuf.blur / 100.0 * kBlurAt100;
    // Below a fifth of a pixel there is nothing to blur: one tap is the whole answer.
    if (sigma < 0.2) {
        fragColor = texture(sheet, qt_TexCoord0) * ubuf.qt_Opacity;
        return;
    }
    vec2  texel  = ubuf.sheetSize / ubuf.sheetPixels;
    float lod    = max(0.0, log2(kPrefilterSigma * sigma / texel.x));
    float stepUV = exp2(lod) / ubuf.sheetPixels.x * kTapStepTexels;
    vec4  sum    = vec4(0.0);
    for (int i = -6; i <= 6; ++i)
        sum += textureLod(sheet, qt_TexCoord0 + vec2(float(i) * stepUV, 0.0), lod) * tapWeight(i);
    fragColor = sum * ubuf.qt_Opacity;
}
