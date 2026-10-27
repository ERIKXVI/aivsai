#version 330
// Final composite at screen resolution: albedo * light + emission + bloom, tonemapped.
// Albedo and emission use sharp-bilinear sampling so cells stay crisp at any scale while
// the camera scrolls smoothly at sub-cell precision.

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;     // albedo layer (cells)
uniform sampler2D lightTex;     // rgb light, a = solid occupancy
uniform sampler2D glowTex;      // blurred emission pyramid
uniform sampler2D emissionTex;  // emission layer (cells), a = occlusion

uniform vec2 texSize;           // layer size in cells
uniform float pixelScale;       // screen pixels per cell
uniform vec3 ambient;
uniform float glowLight;        // how much blurred emission lights surfaces
uniform float bloom;            // additive bloom strength
uniform float exposure;
uniform float time;
uniform vec2 screenSize;
uniform float damageFlash;      // 0..1 red vignette pulse
uniform float saturation;
uniform vec3 tint;
uniform float aberration;       // chromatic split in cells (damage feedback)
uniform vec3 airColor;          // haze in open air, so unlit caves keep their shape
uniform float airTop;           // layer row the haze fades in below (a sky above it stays clear)

#define MAX_SHOCKWAVES 6
uniform int shockCount;
uniform vec4 shockwaves[MAX_SHOCKWAVES]; // xy = centre (layer cells), z = ring radius, w = strength

vec2 sharpUv(vec2 uv) {
    vec2 texel = uv * texSize;
    vec2 base = floor(texel);
    vec2 f = texel - base;
    float region = clamp(0.5 / pixelScale, 0.0, 0.5);
    vec2 blend = clamp((f - (0.5 - region)) / (2.0 * region), 0.0, 1.0);
    return (base + 0.5 + blend) / texSize;
}

vec3 acesFit(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

// Explosion shockwaves bend the image around an expanding ring.
vec2 distort(vec2 uv) {
    vec2 p = vec2(uv.x, 1.0 - uv.y) * texSize;
    vec2 offset = vec2(0.0);
    for (int i = 0; i < shockCount; ++i) {
        vec2 d = p - shockwaves[i].xy;
        float dist = length(d);
        float ring = (dist - shockwaves[i].z) / 5.0;
        offset += (dist > 0.001 ? d / dist : vec2(0.0)) * shockwaves[i].w * exp(-ring * ring);
    }
    return uv + vec2(offset.x, -offset.y) / texSize;
}

void main() {
    vec2 uv = shockCount > 0 ? distort(fragTexCoord) : fragTexCoord;
    vec2 suv = sharpUv(uv);
    vec4 albedo = texture(texture0, suv);
    if (aberration > 0.0) {
        vec2 split = vec2(aberration / texSize.x, 0.0);
        albedo.r = texture(texture0, sharpUv(uv + split)).r;
        albedo.b = texture(texture0, sharpUv(uv - split)).b;
    }
    vec4 emission = texture(emissionTex, suv);
    vec4 light = texture(lightTex, uv);
    vec3 glow = texture(glowTex, uv).rgb;

    // Solid interiors fade to darkness; open-air pixels (background) are lit a bit less, but
    // figures and treasure (albedo alpha driven to 0 as they are drawn) stand in front of it.
    float solid = emission.a;
    float actor = 1.0 - smoothstep(0.2, 0.5, albedo.a);
    float fg = max(solid, actor);
    float depth = smoothstep(0.84, 1.0, light.a) * solid;
    float shade = mix(1.0, 0.42, depth);
    float backgroundDim = mix(0.7, 1.0, fg);

    vec3 lighting = ambient + light.rgb + glow * glowLight;
    vec3 color = albedo.rgb * lighting * shade * backgroundDim;
    float row = (1.0 - uv.y) * texSize.y;
    color += airColor * (1.0 - fg) * smoothstep(airTop, airTop + 40.0, row);
    color += emission.rgb * 1.25;
    color += glow * bloom;

    color *= exposure;
    color = acesFit(color);

    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(lum), color, saturation) * tint;

    // Vignette, damage pulse and a whisper of film grain.
    vec2 q = gl_FragCoord.xy / screenSize;
    float vig = smoothstep(1.15, 0.35, length(q - 0.5) * 1.25);
    color *= mix(0.62, 1.0, vig);
    color = mix(color, vec3(0.55, 0.02, 0.02), damageFlash * (1.0 - vig) * 0.85);
    color += (hash(gl_FragCoord.xy + fract(time) * 91.7) - 0.5) * 0.018;

    finalColor = vec4(pow(color, vec3(1.0 / 1.08)), 1.0);
}
