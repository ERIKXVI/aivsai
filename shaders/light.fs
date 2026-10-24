#version 330
// Light accumulation at cell resolution. Every point light is ray-marched through the
// occlusion field (alpha of the emission layer) so solid matter casts soft shadows. The
// alpha output stores the local solid occupancy used to darken rock interiors.

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;   // emission (rgb) + occlusion (a)
uniform vec2 texSize;         // size of the layer in cells

#define MAX_LIGHTS 64
uniform int lightCount;
uniform vec4 lightPosRadius[MAX_LIGHTS]; // xy = position in layer cells, z = radius, w = self-shadow skip
uniform vec4 lightColor[MAX_LIGHTS];     // rgb = colour * intensity

vec2 toUv(vec2 p) { return vec2(p.x, texSize.y - p.y) / texSize; }
float occlusionAt(vec2 p) { return texture(texture0, toUv(p)).a; }

void main() {
    vec2 p = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y) * texSize;
    vec3 sum = vec3(0.0);

    for (int i = 0; i < lightCount; ++i) {
        vec2 lp = lightPosRadius[i].xy;
        float radius = lightPosRadius[i].z;
        vec2 d = lp - p;
        float dist = length(d);
        if (dist >= radius) continue;
        float n = dist / radius;
        float att = 1.0 - n * n;
        att = att * att / (1.0 + n * n * 6.0);

        float transmit = 1.0;
        int steps = int(min(dist, 40.0));
        if (steps > 1) {
            vec2 stepv = d / float(steps);
            vec2 q = p + stepv;
            int skip = int(lightPosRadius[i].w); // don't let the light's own body shadow it
            for (int s = 1; s < steps - skip; ++s) {
                transmit *= 1.0 - occlusionAt(q) * 0.42;
                if (transmit < 0.02) break;
                q += stepv;
            }
        }
        sum += lightColor[i].rgb * att * transmit;
    }

    // Local solid occupancy (two rings) for interior darkening.
    float occ = 0.0;
    const vec2 dirs[8] = vec2[8](vec2(1, 0), vec2(-1, 0), vec2(0, 1), vec2(0, -1),
                                 vec2(0.707, 0.707), vec2(-0.707, 0.707), vec2(0.707, -0.707), vec2(-0.707, -0.707));
    for (int k = 0; k < 8; ++k) {
        occ += occlusionAt(p + dirs[k] * 2.5);
        occ += occlusionAt(p + dirs[k] * 6.0);
    }
    finalColor = vec4(sum, occ / 16.0);
}
