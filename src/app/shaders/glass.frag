#version 440

// The glass of a settings card (UI.md 3.1c and 3.1d, docs/adr/0039). The desktop under the window is blurred once, in
// C++, into a sheet a little larger than the window. This shader places the window within the sheet on every frame,
// bends the sheet through the displacement map for the liquid theme, and cuts the card's rounded corners with a signed
// distance. Its inputs move with the window, so dragging the window needs no new capture.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float refraction;   // DIP; the edge offset is half of this either way
    float radius;       // DIP, the card's corner radius
    vec2 cardSize;      // DIP
    vec2 origin;        // DIP, where the card's top-left corner is within the sheet
    vec2 sheetSize;     // DIP
} ubuf;

layout(binding = 1) uniform sampler2D sheet;   // the blurred desktop, transparent until it has loaded
layout(binding = 2) uniform sampler2D map;     // offsets in red and green, 0.5 meaning none

void main()
{
    vec2 p = qt_TexCoord0 * ubuf.cardSize;
    vec2 offset = (texture(map, qt_TexCoord0).rg - vec2(0.5)) * ubuf.refraction;
    vec4 desktop = texture(sheet, (ubuf.origin + p + offset) / ubuf.sheetSize);
    vec2 q = abs(p - ubuf.cardSize * 0.5) - (ubuf.cardSize * 0.5 - vec2(ubuf.radius));
    float sd = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - ubuf.radius;
    float coverage = (1.0 - smoothstep(-0.5, 0.5, sd)) * desktop.a;
    fragColor = vec4(desktop.rgb * coverage, coverage) * ubuf.qt_Opacity;
}
