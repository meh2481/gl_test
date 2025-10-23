#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec2 iResolution;
    float iTime;
} pc;

void main() {
    vec2 uv = fragTexCoord * 2.0 - 1.0; // Convert to -1 to 1 range

    // Create cloud-like effect with noise
    float cloud1 = sin(uv.x * 5.0 + pc.iTime * 0.5) * cos(uv.y * 3.0 + pc.iTime * 0.3);
    float cloud2 = sin((uv.x + uv.y) * 4.0 + pc.iTime * 0.7) * sin(uv.x * 2.0 - pc.iTime * 0.4);
    float cloud3 = cos(length(uv) * 6.0 + pc.iTime * 0.6) * sin(uv.y * 4.0 + pc.iTime * 0.2);

    float clouds = (cloud1 + cloud2 + cloud3) * 0.2 + 0.5;
    clouds = clamp(clouds, 0.0, 1.0);

    // Create soft cloud color
    vec3 cloudColor = vec3(1.0, 1.0, 1.2) * clouds;

    // Make it semi-transparent for layering
    outColor = vec4(cloudColor, clouds * 0.9);
}