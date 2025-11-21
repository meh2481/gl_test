#version 450

layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float lightX;
    float lightY;
    float lightZ;
    float levels;      // Number of shading levels (e.g., 3.0, 4.0, 5.0)
    float param1;      // Unused
    float param2;      // Unused
    float param3;      // Unused
} pc;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragPos;
layout(location = 2) in vec3 fragLightPos;
layout(location = 3) in vec3 fragViewPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;
layout(binding = 1) uniform sampler2D normalSampler;

void main() {
    // Sample the base texture
    vec4 texColor = texture(texSampler, fragTexCoord);
    
    // Sample the normal map
    vec3 tangentNormal = texture(normalSampler, fragTexCoord).rgb * 2.0 - 1.0;
    
    // Compute TBN matrix using screen-space derivatives
    vec3 dPosX = dFdx(fragPos);
    vec3 dPosY = dFdy(fragPos);
    vec2 dTexX = dFdx(fragTexCoord);
    vec2 dTexY = dFdy(fragTexCoord);
    
    // Compute tangent and bitangent from derivatives
    vec3 T = normalize(dPosX * dTexY.t - dPosY * dTexX.t);
    vec3 B = normalize(dPosY * dTexX.s - dPosX * dTexY.s);
    vec3 N = vec3(0.0, 0.0, 1.0);
    
    // Construct TBN matrix and transform normal
    mat3 TBN = mat3(T, B, N);
    vec3 normal = normalize(TBN * tangentNormal);
    
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
