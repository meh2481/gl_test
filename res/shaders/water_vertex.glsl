#version 450

layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float cameraX;
    float cameraY;
    float cameraZoom;
    float param0;           // Water alpha
    float param1;           // Ripple amplitude
    float param2;           // Ripple speed
    float param3;           // Max Y in world space (surface)
    float param4;           // Min X in world space
    float param5;           // Min Y in world space
    float param6;           // Max X in world space
    // Splash ripple data (4 ripples x 3 values = 12 floats)
    float ripple0_x;
    float ripple0_time;
    float ripple0_amplitude;
    float ripple1_x;
    float ripple1_time;
    float ripple1_amplitude;
    float ripple2_x;
    float ripple2_time;
    float ripple2_amplitude;
    float ripple3_x;
    float ripple3_time;
    float ripple3_amplitude;
    // Unused animation slots (polygon data now in uniform buffer)
    float unused0;
    float unused1;
    float unused2;
    float unused3;
    float unused4;
    float unused5;
    float unused6;
    float unused7;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inUVBounds;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragUVBounds;
layout(location = 2) out vec2 fragWorldPos;
layout(location = 3) out vec2 fragWaterBounds;
layout(location = 4) out vec2 fragWaterBoundsMax;

void main() {
    float aspect = pc.width / pc.height;

    vec2 worldPos = inPosition;

    // Apply camera transform
    vec2 screenPos = (worldPos - vec2(pc.cameraX, pc.cameraY)) * pc.cameraZoom;
    gl_Position = vec4(screenPos.x / aspect, -screenPos.y, 0.0, 1.0);

    fragTexCoord = inTexCoord;
    fragUVBounds = inUVBounds;
    fragWorldPos = worldPos;

    // Water bounds are passed via params
    fragWaterBounds = vec2(pc.param4, pc.param5);
    fragWaterBoundsMax = vec2(pc.param6, pc.param3);
}
