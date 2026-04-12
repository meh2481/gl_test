#version 450

// Push constants: 6 floats (width, height, time, cameraX, cameraY, cameraZoom)
layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float cameraX;
    float cameraY;
    float cameraZoom;
} pc;

// Per-vertex: world-space quad corner (base), shape-local position (for SDF),
// glyph descriptor index, vertex colour, and per-character effect offsets.
layout(location = 0) in vec2  inPos;      // world-space quad corner (base, no offset)
layout(location = 1) in vec2  inLocalPos; // shape-local position (normalised glyph space)
layout(location = 2) in float inGlyphIdx; // index into GlyphDescBuffer (stored as bits)
layout(location = 3) in vec4  inColor;    // r, g, b, a
layout(location = 4) in vec2  inOffset;   // per-character effect offsets (xOffset, yOffset)

layout(location = 0) out vec2      fragLocalPos;
layout(location = 1) out flat uint fragGlyphIdx;
layout(location = 2) out vec4      fragColor;

void main() {
    // Apply effect offset to get final world position.
    vec2 world = inPos + inOffset;

    // Camera transform + NDC conversion.
    float aspect = pc.width / pc.height;
    vec2 pos = (world - vec2(pc.cameraX, pc.cameraY)) * pc.cameraZoom;
    // Negate Y: Vulkan clip space has +Y downward; world +Y is upward.
    gl_Position = vec4(pos.x / aspect, -pos.y, 0.0, 1.0);

    fragLocalPos = inLocalPos;
    fragGlyphIdx = floatBitsToUint(inGlyphIdx);
    fragColor    = inColor;
}
