#version 450

layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float cameraX;
    float cameraY;
    float cameraZoom;
} pc;

layout(location = 0) in vec2 inCenterPosition;
layout(location = 1) in vec4 inParticleParams;  // halfSize, rotZ, lifeRatio, unused
layout(location = 2) in vec4 inStartColor;
layout(location = 3) in vec4 inEndColor;
layout(location = 4) in vec4 inUVBounds;  // minX, minY, maxX, maxY

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out vec4 fragUVBounds;

void main() {
    int cornerIndex = gl_VertexIndex & 3;

    vec2 corner;
    if (cornerIndex == 0) {
        corner = vec2(-1.0, -1.0);
    } else if (cornerIndex == 1) {
        corner = vec2(1.0, -1.0);
    } else if (cornerIndex == 2) {
        corner = vec2(1.0, 1.0);
    } else {
        corner = vec2(-1.0, 1.0);
    }

    float halfSize = inParticleParams.x;
    float rotZ = inParticleParams.y;
    float lifeRatio = inParticleParams.z;

    vec2 localPos = corner * halfSize;
    float cosR = cos(rotZ);
    float sinR = sin(rotZ);
    vec2 rotatedPos = vec2(
        localPos.x * cosR - localPos.y * sinR,
        localPos.x * sinR + localPos.y * cosR
    );

    vec2 worldPos = inCenterPosition + rotatedPos;

    float aspect = pc.width / pc.height;
    // Apply camera transform: offset then zoom
    vec2 pos = (worldPos - vec2(pc.cameraX, pc.cameraY)) * pc.cameraZoom;
    gl_Position = vec4(pos.x / aspect, -pos.y, 0.0, 1.0);

    if (cornerIndex == 0) {
        fragTexCoord = vec2(inUVBounds.x, inUVBounds.w);
    } else if (cornerIndex == 1) {
        fragTexCoord = vec2(inUVBounds.z, inUVBounds.w);
    } else if (cornerIndex == 2) {
        fragTexCoord = vec2(inUVBounds.z, inUVBounds.y);
    } else {
        fragTexCoord = vec2(inUVBounds.x, inUVBounds.y);
    }

    fragColor = mix(inStartColor, inEndColor, lifeRatio);
    fragUVBounds = inUVBounds;
}
