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

void main() {
    vec2 suv = sharpUv(fragTexCoord);
    vec4 albedo = texture(texture0, suv);
    vec4 emission = texture(emissionTex, suv);
    vec4 light = texture(lightTex, fragTexCoord);
    vec3 glow = texture(glowTex, fragTexCoord).rgb;

    // Solid interiors fade to darkness; open-air pixels (background) are lit a bit less.
    float solid = emission.a;
    float depth = smoothstep(0.55, 0.98, light.a) * solid;
    float shade = mix(1.0, 0.12, depth);
    float backgroundDim = mix(0.55, 1.0, solid);

    vec3 lighting = ambient + light.rgb + glow * glowLight;
    vec3 color = albedo.rgb * lighting * shade * backgroundDim;
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
