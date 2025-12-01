#version 450

layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float cameraX;
    float cameraY;
    float cameraZoom;
    float param0;
    float param1;
    float param2;
    float param3;
    float param4;
    float param5;
    float param6;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec2 inNormalTexCoord;
layout(location = 3) in vec4 inUVBounds;  // minX, minY, maxX, maxY

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragPos;
layout(location = 2) out vec3 fragLightPos;  // Kept for compatibility
layout(location = 3) out vec3 fragViewPos;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out vec3 fragBitangent;
layout(location = 6) out vec2 fragNormalTexCoord;
layout(location = 7) out vec4 fragUVBounds;

void main() {
    float aspect = pc.width / pc.height;

    // Transform position to world space
    fragPos = vec3(inPosition.x, inPosition.y, 0.0);

    // Light position (for compatibility, not used in multi-light)
    fragLightPos = vec3(0.0, 0.0, 0.5);

    // Camera/view position
    fragViewPos = vec3(0.0, 0.0, 1.0);

    // Default tangent space
    fragTangent = vec3(1.0, 0.0, 0.0);
    fragBitangent = vec3(0.0, 1.0, 0.0);

    // Apply camera transform
    vec2 pos = (inPosition - vec2(pc.cameraX, pc.cameraY)) * pc.cameraZoom;
    gl_Position = vec4(pos.x / aspect, -pos.y, 0.0, 1.0);
    fragTexCoord = inTexCoord;
    fragNormalTexCoord = inNormalTexCoord;
    fragUVBounds = inUVBounds;
}
