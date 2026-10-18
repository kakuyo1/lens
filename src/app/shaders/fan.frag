#version 440

// Cuts the glass fan out of a rectangular GlassSurface capture (UI.md 4.10).
//
// The fan is one glass surface, not three cards; a rounded-rectangle GlassSurface cannot follow
// its pentagon, and Qt's mask route left the rectangle showing. The five edges below are the
// same pentagon the outline and seams are drawn from. The texture is the glass surface as it
// renders; this shader keeps only the pixels inside the pentagon and leaves the rest to the
// desktop.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;
} ubuf;

layout(binding = 1) uniform sampler2D glass;

const vec2 P0 = vec2(130.0, 150.0);
const vec2 P1 = vec2(10.0, 87.0);
const vec2 P2 = vec2(78.0, 26.0);
const vec2 P3 = vec2(182.0, 26.0);
const vec2 P4 = vec2(250.0, 87.0);

/// Signed distance to one edge, positive inside the counter-clockwise polygon.
float edge(vec2 a, vec2 b, vec2 p)
{
    vec2  e = b - a;
    vec2  q = p - a;
    return (e.x * q.y - e.y * q.x) / length(e);
}

void main()
{
    vec2  p = qt_TexCoord0 * ubuf.size;
    float d = min(min(edge(P0, P1, p), edge(P1, P2, p)),
                  min(edge(P2, P3, p), min(edge(P3, P4, p), edge(P4, P0, p))));
    float coverage = smoothstep(-0.5, 0.5, d);
    fragColor = texture(glass, qt_TexCoord0) * coverage * ubuf.qt_Opacity;
}
