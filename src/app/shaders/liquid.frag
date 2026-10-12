#version 440

// The liquid theme's refraction (UI.md 3.1d). The backdrop arrives already blurred; each pixel samples
// it at an offset read from the displacement map (GlassMap), and the card's rounded corners are cut
// here with a signed distance, so the shader needs no mask item.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float refraction;   // px; the edge offset is half of this either way
    float radius;       // px, the card's corner radius
    vec2 cardSize;      // px
} ubuf;

layout(binding = 1) uniform sampler2D source;   // the blurred desktop
layout(binding = 2) uniform sampler2D map;      // offsets in red and green, 0.5 meaning none

void main()
{
    vec2 offset = (texture(map, qt_TexCoord0).rg - vec2(0.5)) * ubuf.refraction / ubuf.cardSize;
    vec2 p = (qt_TexCoord0 - vec2(0.5)) * ubuf.cardSize;
    vec2 q = abs(p) - (ubuf.cardSize * 0.5 - vec2(ubuf.radius));
    float sd = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - ubuf.radius;
    float coverage = 1.0 - smoothstep(-0.5, 0.5, sd);
    fragColor = texture(source, qt_TexCoord0 + offset) * ubuf.qt_Opacity * coverage;
}
