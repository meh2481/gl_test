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
    // Animation parameters
    float spinSpeed;      // Degrees per second
    float centerX;        // Spin center X
    float centerY;        // Spin center Y
    float blinkSecondsOn;
    float blinkSecondsOff;
    float blinkRiseTime;
    float blinkFallTime;
    float waveWavelength;
    float waveSpeed;
    float waveAngle;      // Wave direction in radians
    float waveAmplitude;
    float colorR;
    float colorG;
    float colorB;
    float colorA;
    float colorEndR;
    float colorEndG;
    float colorEndB;
    float colorEndA;
    float colorCycleTime;
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inUVBounds;  // minX, minY, maxX, maxY

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragUVBounds;
layout(location = 2) out float fragBlinkAlpha;
layout(location = 3) out vec4 fragColor;

const float PI = 3.14159265359;
const float DEG_TO_RAD = PI / 180.0;

void main() {
    float aspect = pc.width / pc.height;

    vec2 pos = inPosition;

    // Apply spin rotation around center point
    if (pc.spinSpeed != 0.0) {
        float spinAngle = pc.time * pc.spinSpeed * DEG_TO_RAD;
        float cosA = cos(spinAngle);
        float sinA = sin(spinAngle);
        vec2 offset = pos - vec2(pc.centerX, pc.centerY);
        pos = vec2(
            offset.x * cosA - offset.y * sinA,
            offset.x * sinA + offset.y * cosA
        ) + vec2(pc.centerX, pc.centerY);
    }

    // Apply wave displacement
    if (pc.waveAmplitude != 0.0 && pc.waveWavelength != 0.0) {
        // Wave direction vector
        float waveAngleRad = pc.waveAngle * DEG_TO_RAD;
        vec2 waveDir = vec2(cos(waveAngleRad), sin(waveAngleRad));
        vec2 perpDir = vec2(-waveDir.y, waveDir.x);

        // Calculate position along wave direction
        float wavePos = dot(pos, waveDir);

        // Calculate wave phase based on position and time
        float phase = (wavePos / pc.waveWavelength + pc.time * pc.waveSpeed) * 2.0 * PI;

        // Displacement perpendicular to wave direction
        float displacement = sin(phase) * pc.waveAmplitude;
        pos += perpDir * displacement;
    }

    // Apply camera transform
    vec2 finalPos = (pos - vec2(pc.cameraX, pc.cameraY)) * pc.cameraZoom;
    gl_Position = vec4(finalPos.x / aspect, -finalPos.y, 0.0, 1.0);
    fragTexCoord = inTexCoord;
    fragUVBounds = inUVBounds;

    // Calculate blink alpha
    float blinkAlpha = 1.0;
    float blinkCycle = pc.blinkSecondsOn + pc.blinkSecondsOff + pc.blinkRiseTime + pc.blinkFallTime;
    if (blinkCycle > 0.0) {
        float t = mod(pc.time, blinkCycle);

        if (t < pc.blinkRiseTime) {
            // Rising phase
            blinkAlpha = t / pc.blinkRiseTime;
        } else if (t < pc.blinkRiseTime + pc.blinkSecondsOn) {
            // Fully on phase
            blinkAlpha = 1.0;
        } else if (t < pc.blinkRiseTime + pc.blinkSecondsOn + pc.blinkFallTime) {
            // Falling phase
            float fallT = t - pc.blinkRiseTime - pc.blinkSecondsOn;
            blinkAlpha = 1.0 - (fallT / pc.blinkFallTime);
        } else {
            // Off phase
            blinkAlpha = 0.0;
        }
    }
    fragBlinkAlpha = blinkAlpha;

    // Calculate animated color
    vec4 startColor = vec4(pc.colorR, pc.colorG, pc.colorB, pc.colorA);
    vec4 endColor = vec4(pc.colorEndR, pc.colorEndG, pc.colorEndB, pc.colorEndA);

    if (pc.colorCycleTime > 0.0) {
        float colorT = mod(pc.time, pc.colorCycleTime * 2.0);
        if (colorT < pc.colorCycleTime) {
            fragColor = mix(startColor, endColor, colorT / pc.colorCycleTime);
        } else {
            fragColor = mix(endColor, startColor, (colorT - pc.colorCycleTime) / pc.colorCycleTime);
        }
    } else {
        fragColor = startColor;
    }
}
