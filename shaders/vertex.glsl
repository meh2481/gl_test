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
    // Apply parallax effect based on camera offset, zoom, and depth (z-index)
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
    // 0.25 scaling converts world coords to texture space (1 world unit = 0.25 texture units)
    // Multiplying by zoom ensures panning feels proportional at all zoom levels:
    //   - When zoomed out (zoom < 1), we see more of the world, so pan moves background less
    //   - When zoomed in (zoom > 1), we see less of the world, so pan moves background more
    float aspect = pc.iResolution.x / pc.iResolution.y;
    vec2 parallaxOffset = vec2(pc.cameraX / aspect, -pc.cameraY) * parallaxFactor * 0.25 * pc.cameraZoom;

    // Apply zoom to background: scale texture coordinates around center
    // When zoomed in, the background should also zoom in (show less of the texture)
    // When zoomed out, the background should also zoom out (show more of the texture)
    // The coefficient (1.0 - abs(parallaxFactor)) creates depth-based zoom attenuation:
    //   - At parallaxFactor=0: full zoom effect (1.0), layer moves with camera
    //   - At parallaxFactor=±0.5: half zoom effect (0.5), background zooms slower
    //   - At parallaxFactor=±1: no zoom effect (0.0), infinitely distant layer stays fixed
    float zoomFactor = 1.0 + (pc.cameraZoom - 1.0) * (1.0 - abs(parallaxFactor));
    vec2 centeredTexCoord = inTexCoord - 0.5;
    vec2 scaledTexCoord = centeredTexCoord / zoomFactor + 0.5;

    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragTexCoord = scaledTexCoord + parallaxOffset;
}