#version 330
// Dual-Kawase upsample (tent filter); the result is added onto the next finer level.

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 srcTexel; // 1 / source size
uniform float weight;

void main() {
    vec2 uv = fragTexCoord;
    vec2 h = srcTexel * 0.5;
    vec4 sum = texture(texture0, uv + vec2(-h.x * 2.0, 0.0));
    sum += texture(texture0, uv + vec2(-h.x, h.y)) * 2.0;
    sum += texture(texture0, uv + vec2(0.0, h.y * 2.0));
    sum += texture(texture0, uv + vec2(h.x, h.y)) * 2.0;
    sum += texture(texture0, uv + vec2(h.x * 2.0, 0.0));
    sum += texture(texture0, uv + vec2(h.x, -h.y)) * 2.0;
    sum += texture(texture0, uv + vec2(0.0, -h.y * 2.0));
    sum += texture(texture0, uv + vec2(-h.x, -h.y)) * 2.0;
    finalColor = vec4(sum.rgb / 12.0 * weight, 1.0);
}
