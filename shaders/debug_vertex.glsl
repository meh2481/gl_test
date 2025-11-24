#version 450

layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

void main() {
    float aspect = pc.width / pc.height;
    // Keep aspect correction but remove Y negation for debug rendering
    // This should help lines render correctly while maintaining proper scaling
    gl_Position = vec4(inPosition.x / aspect, inPosition.y, 0.0, 1.0);
    fragColor = inColor;
}
