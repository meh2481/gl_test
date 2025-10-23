#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2 iResolution;
    float iTime;
} pc;

// Simple noise function
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    fp = fp * fp * (3.0 - 2.0 * fp);

    float a = hash(ip);
    float b = hash(ip + vec2(1.0, 0.0));
    float c = hash(ip + vec2(0.0, 1.0));
    float d = hash(ip + vec2(1.0, 1.0));

    return mix(mix(a, b, fp.x), mix(c, d, fp.x), fp.y);
}

// Fractal Brownian Motion
float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for(int i = 0; i < 5; i++) {
        value += amplitude * noise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value;
}

void main() {
    vec2 uv = fragTexCoord * 2.0 - 1.0; // Convert to -1 to 1 range

    // Domain warping for more organic cloud shapes
    vec2 warped_uv = uv + vec2(
        fbm(uv * 2.0 + pc.iTime * 0.02),
        fbm(uv * 2.0 + 100.0 - pc.iTime * 0.015)
    ) * 0.3;

    // Create multiple cloud layers
    float cloud1 = fbm(warped_uv * 3.0 + pc.iTime * 0.05);
    float cloud2 = fbm(warped_uv * 4.0 - pc.iTime * 0.03);
    float cloud3 = fbm(warped_uv * 2.0 + vec2(pc.iTime * 0.01, pc.iTime * 0.02));

    // Combine layers with different weights
    float clouds = cloud1 * 0.4 + cloud2 * 0.3 + cloud3 * 0.3;

    // Add some large-scale variation
    float large_scale = fbm(warped_uv * 0.5 + pc.iTime * 0.01) * 0.2;
    clouds += large_scale;

    // Shape the clouds - make them more billowy
    clouds = pow(clouds, 1.5);
    clouds = clamp(clouds, 0.0, 1.0);

    // Create soft cloud color - mostly white with subtle blue tint
    vec3 cloudColor = vec3(1.0, 1.0, 0.95) + vec3(0.0, 0.0, 0.05) * fbm(warped_uv * 5.0);

    // Add some brightness variation
    cloudColor *= 0.8 + clouds * 0.4;

    outColor = vec4(cloudColor, clouds);
}