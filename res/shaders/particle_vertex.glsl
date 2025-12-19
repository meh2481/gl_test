#version 450

layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float cameraX;
    float cameraY;
    float cameraZoom;
} pc;

layout(location = 0) in vec2 inPosition;  // Particle center
layout(location = 1) in vec2 inTexCoord;  // Corner UV (0-1 range for which corner)
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec4 inUVBounds;  // minX, minY, maxX, maxY
layout(location = 4) in vec3 inRotation;  // rotX, rotY, rotZ (Euler angles in radians)
layout(location = 5) in float inSize;     // Particle size (half-width/height)

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out vec4 fragUVBounds;

void main() {
    float aspect = pc.width / pc.height;

    // Determine corner offset based on UV coordinates
    // UV (0,0) -> bottom-left corner, (1,1) -> top-right corner
    vec2 cornerOffset = (inTexCoord * 2.0 - 1.0) * inSize;

    // Build 3D rotation matrix from Euler angles (ZYX order)
    // This matches the typical game engine rotation order
    float cosX = cos(inRotation.x);
    float sinX = sin(inRotation.x);
    float cosY = cos(inRotation.y);
    float sinY = sin(inRotation.y);
    float cosZ = cos(inRotation.z);
    float sinZ = sin(inRotation.z);

    // Rotation matrix: Rz * Ry * Rx
    mat3 rotMatrix;
    rotMatrix[0] = vec3(
        cosY * cosZ,
        cosX * sinZ + sinX * sinY * cosZ,
        sinX * sinZ - cosX * sinY * cosZ
    );
    rotMatrix[1] = vec3(
        -cosY * sinZ,
        cosX * cosZ - sinX * sinY * sinZ,
        sinX * cosZ + cosX * sinY * sinZ
    );
    rotMatrix[2] = vec3(
        sinY,
        -sinX * cosY,
        cosX * cosY
    );

    // Apply rotation to corner offset (treat as 3D point with z=0)
    vec3 rotatedOffset = rotMatrix * vec3(cornerOffset, 0.0);

    // Project back to 2D (take x,y components)
    vec2 finalOffset = rotatedOffset.xy;

    // Add offset to particle center
    vec2 worldPos = inPosition + finalOffset;

    // Apply camera transform
    vec2 pos = (worldPos - vec2(pc.cameraX, pc.cameraY)) * pc.cameraZoom;
    gl_Position = vec4(pos.x / aspect, -pos.y, 0.0, 1.0);

    // Map corner UV (0-1) to actual texture UV bounds
    // Note: V coordinate is flipped because texture V goes from top to bottom
    fragTexCoord = vec2(
        mix(inUVBounds.x, inUVBounds.z, inTexCoord.x),
        mix(inUVBounds.w, inUVBounds.y, inTexCoord.y)
    );
    fragColor = inColor;
    fragUVBounds = inUVBounds;
}
