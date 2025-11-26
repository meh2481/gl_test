#version 450
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 0) out vec2 fragTexCoord;

layout(push_constant) uniform PushConstants {
    vec2 iResolution;
    float iTime;
    float cameraX;
    float cameraY;
    float cameraZoom;
    float parallaxDepth;
} pc;

void main() {
    // Apply parallax effect based on camera offset and depth (z-index)
    // parallaxDepth controls how much the layer moves relative to camera:
    // depth < 0: foreground layer, moves faster than objects (pans more)
    // depth = 0: moves with objects (no parallax offset)
    // depth > 0: background layer, moves slower than objects (pans less)
    // The parallax factor: depth / (1.0 + abs(depth)) scales camera offset
    // At depth=0: factor=0 (moves with objects)
    // At depth=1: factor=0.5 (background, half speed)
    // At depth=-1: factor=-0.5 (foreground, 1.5x speed in opposite direction)

    float absDepth = abs(pc.parallaxDepth);
    float parallaxFactor = pc.parallaxDepth / (1.0 + absDepth);

    // Apply parallax offset to texture coordinates
    // Multiply by camera offset to create the parallax pan effect
    // Account for aspect ratio so X and Y panning feel consistent
    // Scale factor 0.25 converts world coordinates to appropriate texture offset
    float aspect = pc.iResolution.x / pc.iResolution.y;
    vec2 parallaxOffset = vec2(pc.cameraX / aspect, pc.cameraY) * parallaxFactor * 0.25;

    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragTexCoord = inTexCoord + parallaxOffset;
}