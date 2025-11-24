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
    // Use simple position transformation without aspect correction
    // This ensures both triangles and lines are rendered correctly
    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragColor = inColor;
}
