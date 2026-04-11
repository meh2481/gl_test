#version 450

layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float cameraX;
    float cameraY;
    float cameraZoom;
    float modelX;
    float modelY;
    float modelScale;
    float r;
    float g;
    float b;
    float a;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inKlm;
layout(location = 2) in float inSign;

layout(location = 0) out vec3 fragKlm;
layout(location = 1) out float fragSign;

void main() {
    // Apply model transform (scale then translate)
    vec2 worldPos = inPosition * pc.modelScale + vec2(pc.modelX, pc.modelY);
    // Apply camera transform: offset then zoom
    float aspect = pc.width / pc.height;
    vec2 pos = (worldPos - vec2(pc.cameraX, pc.cameraY)) * pc.cameraZoom;
    gl_Position = vec4(pos.x / aspect, -pos.y, 0.0, 1.0);
    fragKlm  = inKlm;
    fragSign = inSign;
}
