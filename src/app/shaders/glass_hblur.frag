#version 440

// The horizontal half of the glass's blur (UI.md 3.1c and 3.1d, docs/adr/0082). The sheet is blurred along x into a
// mipmapped texture here, and glass.frag blurs that along y. One pass of twenty-five taps used to do both axes; nine
// taps per axis in two passes is the same sampling cost, but the mip the taps read is three quarters of a level coarser
// than the tap spacing, and that stronger prefilter is what removes the weave the sparse pass folded into text and fine
// detail.
//
// The weights are fixed because the tap spacing is a fixed fraction of sigma: w(i) = exp(-(0.95 i)^2 / 2), normalized.
// Weight is a function of the index rather than a vector lookup: the HLSL qsb bakes does not carry a dynamically indexed
// constant vector over intact (docs/adr/0070).
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

/// The blur at 100%, in DIP: the prototype's largest blur.
const float kBlurAt100 = 60.0;
/// A tap spacing of this many sigma leaves the nine taps' second moment at one sigma.
const float kSpacingSigma = 0.95;
/// How much coarser the prefilter mip is than the tap spacing. A box of exactly one tap's width has a stopband that
/// folds fine detail back as a visible weave; three quarters of a level more flattens it and lands the same slider a
/// little softer than the old single pass (docs/adr/0082).
const float kLodBias = 0.75;

float weight(int k)
{
    int a = abs(k);
    return a == 0 ? 0.379 : (a == 1 ? 0.241358 : (a == 2 ? 0.062336 : (a == 3 ? 0.006529 : 0.000277)));
}

void main()
{
    float sigma = ubuf.blur / 100.0 * kBlurAt100;
    // Below a fifth of a pixel there is nothing to blur: one tap is the whole answer.
    if (sigma < 0.2) {
        fragColor = texture(sheet, qt_TexCoord0) * ubuf.qt_Opacity;
        return;
    }
    vec2  texel  = ubuf.sheetSize / ubuf.sheetPixels;
    float lod    = max(0.0, log2(kSpacingSigma * sigma / texel.x) + kLodBias);
    float stepUV = exp2(lod) / ubuf.sheetPixels.x;
    vec4  sum    = vec4(0.0);
    for (int i = -4; i <= 4; ++i)
        sum += textureLod(sheet, qt_TexCoord0 + vec2(float(i) * stepUV, 0.0), lod) * weight(i);
    fragColor = sum * ubuf.qt_Opacity;
}
