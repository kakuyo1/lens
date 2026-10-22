#version 440
#extension GL_GOOGLE_include_directive : require

// The glass of a settings card (UI.md 3.1c and 3.1d, docs/adr/0039, docs/adr/0070, docs/adr/0082). The desktop under the
// window is taken once, at the screen's own resolution, into a sheet a little larger than the window. glass_hblur.frag
// blurs that sheet along x; this shader blurs the result along y, saturates it, places the window within the sheet on
// every frame, bends the sheet through the displacement map for the liquid theme, and cuts the card's rounded corners
// with a signed distance. Its inputs move with the window, so dragging the window needs no new capture, and a slider
// move changes a uniform rather than reloading the picture.
//
// Nine taps along the axis, each at 0.95 sigma and reading a mip three quarters of a level coarser than that spacing.
// The mip is the prefilter: a box of exactly one tap's width folds fine detail back as a visible weave, and the extra
// level depth is what flattens it (docs/adr/0082). The two passes together cost what the old single pass's twenty-five
// taps did, the weave is gone, and the same slider lands a little softer: a Gaussian of about 1.2 sigma.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float refraction;   // DIP; the edge offset is half of this either way
    float radius;       // DIP, the card's corner radius
    float blur;         // percent of the full scale; 100 is 60 DIP
    float saturation;   // percent; 100 is untouched
    vec2 cardSize;      // DIP
    vec2 origin;        // DIP, where the card's top-left corner is within the sheet
    vec2 sheetSize;     // DIP
    vec2 sheetPixels;   // device pixels the sheet's texture covers; sheetSize / this is one texel in DIP
} ubuf;

layout(binding = 1) uniform sampler2D blurred; // the sheet, horizontally blurred by glass_hblur.frag
layout(binding = 2) uniform sampler2D map;     // offsets in red and green, 0.5 meaning none

#include "blur_common.glsl"

/// One tap of the nine along the axis: the outer pair, the inner pair, and the centre. Written as a function of the
/// index rather than indexed out of a vector: the HLSL the shaders are baked to does not carry a dynamically indexed
/// constant vector over intact, and the blur came out white.
float weight(int k)
{
    int a = abs(k);
    return a == 0 ? 0.379 : (a == 1 ? 0.241358 : (a == 2 ? 0.062336 : (a == 3 ? 0.006529 : 0.000277)));
}

/// The sheet sampled at @p uv, blurred along y by a Gaussian of @p sigma DIP. Alpha is blurred too, so an empty sheet
/// stays empty. Below a fifth of a pixel there is nothing to blur: one tap is the whole answer.
vec4 blurredVertically(vec2 uv, float sigma)
{
    if (sigma < 0.2) return texture(blurred, uv);
    // One texel at mip 0, in DIP, and the mip whose texel is one tap wide, three quarters of a level coarser. The tap
    // step is that texel in UV: a distance in DIP added to a normalized coordinate would walk a whole sheet per tap.
    vec2  texel  = ubuf.sheetSize / ubuf.sheetPixels;
    float lod    = max(0.0, log2(kSpacingSigma * sigma / texel.y) + kLodBias);
    float stepUV = exp2(lod) / ubuf.sheetPixels.y;
    vec4  sum    = vec4(0.0);
    for (int j = -4; j <= 4; ++j)
        sum += textureLod(blurred, uv + vec2(0.0, float(j) * stepUV), lod) * weight(j);
    return sum;
}

void main()
{
    vec2 p = qt_TexCoord0 * ubuf.cardSize;
    vec2 offset = (texture(map, qt_TexCoord0).rg - vec2(0.5)) * ubuf.refraction;
    vec2 uv = (ubuf.origin + p + offset) / ubuf.sheetSize;
    vec4 desktop = blurredVertically(uv, ubuf.blur / 100.0 * kBlurAt100);
    // CSS saturate(): each channel mixes toward the luma, so 0 is grey and 100 is untouched.
    float luma = dot(desktop.rgb, vec3(0.213, 0.715, 0.072));
    desktop.rgb = mix(vec3(luma), desktop.rgb, ubuf.saturation / 100.0);
    vec2 q = abs(p - ubuf.cardSize * 0.5) - (ubuf.cardSize * 0.5 - vec2(ubuf.radius));
    float sd = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - ubuf.radius;
    float coverage = (1.0 - smoothstep(-0.5, 0.5, sd)) * desktop.a;
    fragColor = vec4(desktop.rgb * coverage, coverage) * ubuf.qt_Opacity;
}
