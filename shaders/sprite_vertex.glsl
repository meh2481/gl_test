#version 450

layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float cameraX;
    float cameraY;
    float cameraZoom;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    float aspect = pc.width / pc.height;
    // Apply camera transform: offset then zoom
    vec2 pos = (inPosition - vec2(pc.cameraX, pc.cameraY)) * pc.cameraZoom;
    gl_Position = vec4(pos.x / aspect, -pos.y, 0.0, 1.0);
    fragTexCoord = inTexCoord;
}
