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

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragPos;
layout(location = 2) out vec3 fragLightPos;
layout(location = 3) out vec3 fragViewPos;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out vec3 fragBitangent;

void main() {
    float aspect = pc.width / pc.height;
    
    // Transform position to world space (we're in 2D, so z=0)
    fragPos = vec3(inPosition.x, inPosition.y, 0.0);
    
    // Light position in world space
    fragLightPos = vec3(pc.lightX, pc.lightY, pc.lightZ);
    
    // Camera/view position (looking down the -z axis)
    fragViewPos = vec3(0.0, 0.0, 1.0);
    
    // For 2D sprites, compute tangent space from texture coordinates
    // The tangent follows the U direction (horizontal in texture)
    // The bitangent follows the V direction (vertical in texture)
    // Since we're in 2D and UVs map to rotated quad positions:
    // dPos/dU gives us the tangent direction in world space
    // We'll compute these in the fragment shader using derivatives
    // For now, pass basis vectors that will be adjusted per-fragment
    
    // Default tangent space (will be properly computed in fragment shader using derivatives)
    fragTangent = vec3(1.0, 0.0, 0.0);
    fragBitangent = vec3(0.0, 1.0, 0.0);
    
    gl_Position = vec4(inPosition.x / aspect, -inPosition.y, 0.0, 1.0);
    fragTexCoord = inTexCoord;
}
