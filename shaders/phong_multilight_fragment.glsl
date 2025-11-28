#version 450

// Push constants for camera and basic parameters
layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float cameraX;
    float cameraY;
    float cameraZoom;
    // Additional parameters (not used in multi-light version)
    float param0;
    float param1;
    float param2;
    float param3;
    float param4;
    float param5;
    float param6;
} pc;

// Light structure matching C++ side (std140 layout)
// C++ struct uses: posX, posY, posZ (12 bytes) + padding1 (4 bytes) = 16 bytes
//                  colorR, colorG, colorB (12 bytes) + intensity (4 bytes) = 16 bytes
// Total: 32 bytes per light
struct Light {
    vec3 position;      // 12 bytes, aligned to 16
    float padding1;     // 4 bytes padding
    vec3 color;         // 12 bytes
    float intensity;    // 4 bytes
};  // 32 bytes total

// Light uniform buffer (std140 layout)
layout(std140, set = 1, binding = 0) uniform LightBuffer {
    Light lights[8];    // 32 * 8 = 256 bytes
    int numLights;      // 4 bytes
    vec3 ambient;       // 12 bytes (but aligned to 16 in std140)
    vec3 padding;       // Padding for alignment
} lightData;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragPos;
layout(location = 2) in vec3 fragLightPos;  // Not used, kept for compatibility
layout(location = 3) in vec3 fragViewPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in vec3 fragBitangent;
layout(location = 6) in vec2 fragNormalTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;
layout(set = 0, binding = 1) uniform sampler2D normalSampler;

// Lighting parameters
const float diffuseStrength = 0.8;
const float specularStrength = 0.5;
const float shininess = 32.0;

// Calculate lighting contribution from a single light
vec3 calculateLight(Light light, vec3 normal, vec3 viewDir, vec3 texColor) {
    vec3 lightPos = light.position;
    vec3 lightColor = light.color;
    float intensity = light.intensity;

    vec3 lightDir = normalize(lightPos - fragPos);

    // Distance attenuation
    float distance = length(lightPos - fragPos);
    float attenuation = intensity / (1.0 + 0.5 * distance + 0.3 * distance * distance);

    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diffuseStrength * diff * texColor * lightColor * attenuation;

    // Specular (Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), shininess);
    vec3 specular = specularStrength * spec * lightColor * attenuation;

    return diffuse + specular;
}

void main() {
    // Sample the base texture
    vec4 texColor = texture(texSampler, fragTexCoord);

    // Sample the normal map and convert from [0,1] to [-1,1]
    vec3 tangentNormal = texture(normalSampler, fragNormalTexCoord).rgb;
    tangentNormal = normalize(tangentNormal * 2.0 - 1.0);

    // Compute tangent space basis using screen-space derivatives
    vec3 dPosX = dFdx(fragPos);
    vec3 dPosY = dFdy(fragPos);
    vec2 dTexX = dFdx(fragTexCoord);
    vec2 dTexY = dFdy(fragTexCoord);

    vec3 N = vec3(0.0, 0.0, 1.0);
    vec3 T = normalize(dPosX * dTexY.t - dPosY * dTexX.t);
    vec3 B = normalize(dPosY * dTexX.s - dPosX * dTexY.s);

    if (dot(cross(T, B), N) < 0.0) {
        B = -B;
    }

    mat3 TBN = mat3(T, B, N);
    vec3 normal = normalize(TBN * tangentNormal);

    // View direction
    vec3 viewDir = normalize(fragViewPos - fragPos);

    // Ambient lighting
    vec3 ambient = lightData.ambient * texColor.rgb;

    // Accumulate lighting from all active lights
    vec3 lighting = vec3(0.0);
    for (int i = 0; i < lightData.numLights && i < 8; i++) {
        lighting += calculateLight(lightData.lights[i], normal, viewDir, texColor.rgb);
    }

    vec3 result = ambient + lighting;
    outColor = vec4(result, texColor.a);
}
