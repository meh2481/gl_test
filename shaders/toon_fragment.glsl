#version 450

layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float cameraX;
    float cameraY;
    float cameraZoom;
    float lightX;
    float lightY;
    float lightZ;
    float levels;      // Number of shading levels (e.g., 3.0, 4.0, 5.0)
} pc;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragPos;
layout(location = 2) in vec3 fragLightPos;
layout(location = 3) in vec3 fragViewPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

void main() {
    // Sample the base texture
    vec4 texColor = texture(texSampler, fragTexCoord);

    // Normal for 2D sprite (pointing at camera)
    vec3 normal = vec3(0.0, 0.0, 1.0);

    // Calculate lighting
    vec3 lightDir = normalize(fragLightPos - fragPos);
    float diff = max(dot(normal, lightDir), 0.0);

    // Quantize the diffuse lighting into discrete levels (cel-shading)
    float numLevels = max(pc.levels, 2.0);  // At least 2 levels
    float celDiff = floor(diff * numLevels) / numLevels;

    // Apply toon shading
    vec3 toonColor = texColor.rgb * (0.3 + 0.7 * celDiff);

    outColor = vec4(toonColor, texColor.a);
}
