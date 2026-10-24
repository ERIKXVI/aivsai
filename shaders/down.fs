#version 330
// Dual-Kawase downsample (13-tap weighted) used to build the glow pyramid.

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 srcTexel;   // 1 / source size
uniform float prefilter; // 1 on the first pass: converts emission into glow energy

vec4 fetch(vec2 uv) {
    vec4 c = texture(texture0, uv);
    if (prefilter > 0.5) c = vec4(c.rgb * c.rgb * 1.6 + c.rgb * 0.4, 0.0);
    return c;
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 h = srcTexel;
    vec4 sum = fetch(uv) * 4.0;
    sum += fetch(uv - h);
    sum += fetch(uv + h);
    sum += fetch(uv + vec2(h.x, -h.y));
    sum += fetch(uv - vec2(h.x, -h.y));
    finalColor = sum / 8.0;
}
