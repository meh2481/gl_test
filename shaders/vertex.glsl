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
    // Apply parallax effect based on camera offset and depth
    // parallaxDepth controls how much the layer moves relative to camera:
    // depth = 0.0: no parallax (layer doesn't move with camera pan)
    // depth > 0.0: background layer moves slower than foreground
    // The parallax factor: depth / (1.0 + depth) scales camera offset
    // At depth=0: factor=0 (no movement), at depth=1: factor=0.5 (half speed)
    // At depth=2: factor=0.667 (2/3 speed), at infinity: factor=1 (follows camera)

    // Guard against invalid depth values (must be >= 0)
    float safeDepth = max(0.0, pc.parallaxDepth);
    float parallaxFactor = safeDepth / (1.0 + safeDepth);

    // Apply parallax offset to texture coordinates
    // Multiply by camera offset to create the parallax pan effect
    // Scale factor 0.25 converts world coordinates to appropriate texture offset
    vec2 parallaxOffset = vec2(pc.cameraX, pc.cameraY) * parallaxFactor * 0.25;

    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragTexCoord = inTexCoord + parallaxOffset;
}