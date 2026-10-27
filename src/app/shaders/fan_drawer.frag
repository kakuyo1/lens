#version 440

// Cuts the copy drawer's fan out of a rectangular GlassSurface capture (UI-SPEC.md 4.10).
//
// The drawer's blades are one glass fan, as the bar's are: fan.frag cuts the bar's pentagon, and this cuts the union of
// the two blades in the drawer's own 140 px box, a quadrilateral from the hinge at (0, 140) up and out to the right.
// With one item (solo) only the lower blade is cut, so the glass covers the blade the item is on and nothing more.
// The texture is the glass surface as it renders; the pixels outside the cut are left to the desktop.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;
    float solo;
} ubuf;

layout(binding = 1) uniform sampler2D glass;

const vec2 P0 = vec2(0.0, 0.0);
const vec2 P1 = vec2(99.0, 41.0);
const vec2 P2 = vec2(140.0, 140.0);
const vec2 P3 = vec2(0.0, 140.0);

/// Signed distance to one edge, positive inside the counter-clockwise quadrilateral.
float edge(vec2 a, vec2 b, vec2 p)
{
    vec2  e = b - a;
    vec2  q = p - a;
    return (e.x * q.y - e.y * q.x) / length(e);
}

void main()
{
    vec2  p = qt_TexCoord0 * ubuf.size;
    // One item draws the lower blade alone: the triangle from P3 over P1 to P2.
    float lone = min(min(edge(P3, P1, p), edge(P1, P2, p)), edge(P2, P3, p));
    float both = min(min(edge(P0, P1, p), edge(P1, P2, p)),
                     min(edge(P2, P3, p), edge(P3, P0, p)));
    float d = ubuf.solo > 0.5 ? lone : both;
    float coverage = smoothstep(-0.5, 0.5, d);
    fragColor = texture(glass, qt_TexCoord0) * coverage * ubuf.qt_Opacity;
}
