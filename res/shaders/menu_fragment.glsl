#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2 iResolution;
    float iTime;
} pc;

void main() {
    vec2 uv = fragTexCoord * 2.0 - 1.0; // Convert to -1 to 1 range

    // Create plasma effect with multiple sine waves
    float v1 = sin(uv.x * 10.0 + pc.iTime);
    float v2 = sin(uv.y * 10.0 + pc.iTime * 1.5);
    float v3 = sin((uv.x + uv.y) * 8.0 + pc.iTime * 0.7);
    float v4 = sin(length(uv) * 12.0 + pc.iTime * 2.0);

    float plasma = (v1 + v2 + v3 + v4) * 0.25 + 0.5; // Combine and normalize to 0-1

    // Create color based on plasma value
    vec3 color = vec3(
        sin(plasma * 6.28 + pc.iTime) * 0.5 + 0.5,
        sin(plasma * 6.28 + pc.iTime * 1.3 + 2.0) * 0.5 + 0.5,
        sin(plasma * 6.28 + pc.iTime * 0.7 + 4.0) * 0.5 + 0.5
    );

    // Add some brightness variation
    color *= plasma * 0.8 + 0.2;

    outColor = vec4(color, 1.0);
}