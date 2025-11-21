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

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragPos;
layout(location = 2) out vec3 fragLightPos;
layout(location = 3) out vec3 fragViewPos;

void main() {
    float aspect = pc.width / pc.height;
    
    // Transform position to world space (we're in 2D, so z=0)
    fragPos = vec3(inPosition.x, inPosition.y, 0.0);
    
    // Light position in world space
    fragLightPos = vec3(pc.lightX, pc.lightY, pc.lightZ);
    
    // Camera/view position (looking down the -z axis)
    fragViewPos = vec3(0.0, 0.0, 1.0);
    
    gl_Position = vec4(inPosition.x / aspect, -inPosition.y, 0.0, 1.0);
    fragTexCoord = inTexCoord;
}
