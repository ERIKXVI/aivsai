#version 330
// Sharp-bilinear upscale: crisp pixel edges at non-integer scales without shimmering.
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 sourceSize;   // texture size in texels
uniform float scale;       // screen pixels per texel

void main() {
    vec2 texel = fragTexCoord * sourceSize;
    vec2 texelFloor = floor(texel);
    vec2 f = texel - texelFloor;
    // Width of the blended region on each texel edge, in texel units.
    float region = clamp(0.5 / scale, 0.0, 0.5);
    vec2 blend = clamp((f - (0.5 - region)) / (2.0 * region), 0.0, 1.0);
    vec2 uv = (texelFloor + 0.5 + blend) / sourceSize;
    finalColor = texture(texture0, uv) * fragColor;
}
