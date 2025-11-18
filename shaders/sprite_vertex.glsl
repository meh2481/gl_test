#version 450

layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;

void main() {
    float aspect = pc.width / pc.height;
    gl_Position = vec4(inPosition.x / aspect, -inPosition.y, 0.0, 1.0);
    fragTexCoord = inTexCoord;
}
