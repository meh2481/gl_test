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
    float ambientStrength;
    float diffuseStrength;
    float specularStrength;
    float shininess;
} pc;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragPos;
layout(location = 2) in vec3 fragLightPos;
layout(location = 3) in vec3 fragViewPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in vec3 fragBitangent;
layout(location = 6) in vec2 fragNormalTexCoord;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;
layout(binding = 1) uniform sampler2D normalSampler;

void main() {
    // Sample the base texture
    vec4 texColor = texture(texSampler, fragTexCoord);

    // Sample the normal map using separate UV coordinates and convert from [0,1] to [-1,1]
    vec3 tangentNormal = texture(normalSampler, fragNormalTexCoord).rgb;
    tangentNormal = normalize(tangentNormal * 2.0 - 1.0);

    // Compute tangent space basis using screen-space derivatives
    // This automatically accounts for sprite rotation
    float aspect = pc.width / pc.height;

    // Get position derivatives - these are affected by aspect ratio in gl_Position
    // gl_Position.x = pos.x / aspect means screen pixels span MORE world X units
    // So dFdx(fragPos.x) is aspect times larger than it should be - divide to correct
    vec3 dPosX = dFdx(fragPos);
    vec3 dPosY = dFdy(fragPos);

    // Correct for aspect ratio: dFdx(fragPos.x) is scaled by aspect, divide to normalize
    dPosX.x /= aspect;
    dPosY.x /= aspect;

    vec2 dTexX = dFdx(fragTexCoord);
    vec2 dTexY = dFdy(fragTexCoord);

    // Compute tangent and bitangent from position and UV derivatives
    vec3 N = vec3(0.0, 0.0, 1.0); // Base normal pointing at camera
    vec3 T = normalize(dPosX * dTexY.t - dPosY * dTexX.t);
    vec3 B = normalize(dPosY * dTexX.s - dPosX * dTexY.s);

    // Ensure right-handed coordinate system
    if (dot(cross(T, B), N) < 0.0) {
        B = -B;
    }

    // Construct TBN matrix to transform from tangent space to world space
    mat3 TBN = mat3(T, B, N);

    // Transform normal from tangent space to world space
    vec3 normal = normalize(TBN * tangentNormal);

    // Calculate lighting in world space (TBN is now in world space after aspect correction)
    vec3 lightDir = normalize(fragLightPos - fragPos);
    vec3 viewDir = normalize(fragViewPos - fragPos);

    // Ambient
    vec3 ambient = pc.ambientStrength * texColor.rgb;

    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = pc.diffuseStrength * diff * texColor.rgb;

    // Specular (Blinn-Phong for better results)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), pc.shininess);
    vec3 specular = pc.specularStrength * spec * vec3(1.0);

    vec3 result = ambient + diffuse + specular;
    outColor = vec4(result, texColor.a);
}
