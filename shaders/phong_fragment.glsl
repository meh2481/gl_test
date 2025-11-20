#version 450

layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
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

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;
layout(binding = 1) uniform sampler2D normalSampler;

void main() {
    // Sample the base texture
    vec4 texColor = texture(texSampler, fragTexCoord);
    
    // Sample the normal map and convert from [0,1] to [-1,1]
    vec3 normal = texture(normalSampler, fragTexCoord).rgb;
    normal = normalize(normal * 2.0 - 1.0);
    
    // If we're in 2D, the normal should point generally towards the camera
    // The normal map provides x,y perturbations, and z is the "height"
    // We need to ensure the normal is in world space
    // For 2D sprites, we assume the surface normal is (0, 0, 1) by default
    normal = normalize(vec3(normal.x, normal.y, normal.z));
    
    // Calculate lighting
    vec3 lightDir = normalize(fragLightPos - fragPos);
    vec3 viewDir = normalize(fragViewPos - fragPos);
    vec3 reflectDir = reflect(-lightDir, normal);
    
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
