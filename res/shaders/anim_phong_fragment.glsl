#version 450

// Push constants for camera and material parameters
layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float cameraX;
    float cameraY;
    float cameraZoom;
    // Material parameters (set from Lua via setShaderParameters)
    float ambientStrength;
    float diffuseStrength;
    float specularStrength;
    float shininess;
    float param4;
    float param5;
    float param6;
    // Animation parameters (not used in fragment but needed for layout)
    float spinSpeed;
    float centerX;
    float centerY;
    float blinkSecondsOn;
    float blinkSecondsOff;
    float blinkRiseTime;
    float blinkFallTime;
    float waveWavelength;
    float waveSpeed;
    float waveAngle;
    float waveAmplitude;
    float colorR;
    float colorG;
    float colorB;
    float colorA;
    float colorEndR;
    float colorEndG;
    float colorEndB;
    float colorEndA;
    float colorCycleTime;
} pc;

// Light structure matching C++ side (std140 layout)
struct Light {
    vec3 position;
    float padding1;
    vec3 color;
    float intensity;
};

// Light uniform buffer (std140 layout)
layout(std140, set = 1, binding = 0) uniform LightBuffer {
    Light lights[8];
    int numLights;
    vec3 ambient;
    vec3 padding;
} lightData;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragPos;
layout(location = 2) in vec3 fragLightPos;
layout(location = 3) in vec3 fragViewPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in vec3 fragBitangent;
layout(location = 6) in vec2 fragNormalTexCoord;
layout(location = 7) in vec4 fragUVBounds;
layout(location = 8) in float fragBlinkAlpha;
layout(location = 9) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;
layout(set = 0, binding = 1) uniform sampler2D normalSampler;

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
    vec3 diffuse = pc.diffuseStrength * diff * texColor * lightColor * attenuation;

    // Specular (Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), pc.shininess);
    vec3 specular = pc.specularStrength * spec * lightColor * attenuation;

    return diffuse + specular;
}

void main() {
    // Clamp texture coordinates to UV bounds
    vec2 clampedUV = clamp(fragTexCoord, fragUVBounds.xy, fragUVBounds.zw);

    // Sample the base texture
    vec4 texColor = texture(texSampler, clampedUV);

    // Sample the normal map
    vec3 tangentNormal = texture(normalSampler, fragNormalTexCoord).rgb;
    tangentNormal = normalize(tangentNormal * 2.0 - 1.0);

    // Compute tangent space basis
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
    vec3 ambient = pc.ambientStrength * lightData.ambient * texColor.rgb;

    // Accumulate lighting from all active lights
    vec3 lighting = vec3(0.0);
    for (int i = 0; i < lightData.numLights && i < 8; i++) {
        lighting += calculateLight(lightData.lights[i], normal, viewDir, texColor.rgb);
    }

    vec3 result = ambient + lighting;

    // Apply animation color modulation
    vec4 color = vec4(result, texColor.a) * fragColor;

    // Apply blink alpha
    color.a *= fragBlinkAlpha;

    // Discard fully transparent fragments
    if (color.a < 0.01) discard;

    outColor = color;
}
