#version 450

// Push constants: 17 floats
// [0..5]   : width, height, time, cameraX, cameraY, cameraZoom
// [6..8]   : modelX, modelY, modelScale
// [9..12]  : r, g, b, a
// [13..16] : bboxMinX, bboxMinY, bboxMaxX, bboxMaxY  (normalised shape space)
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
    float bboxMinX;
    float bboxMinY;
    float bboxMaxX;
    float bboxMaxY;
} pc;

// Shape-local UV passed to the fragment shader for SDF evaluation.
layout(location = 0) out vec2 fragLocalPos;

void main() {
    // Generate a quad (two triangles) covering the shape bbox without a vertex buffer.
    // gl_VertexIndex: 0=BL, 1=BR, 2=TL, 3=BR, 4=TR, 5=TL  (triangle list, 6 vertices)
    vec2 bboxCorners[4] = vec2[4](
        vec2(pc.bboxMinX, pc.bboxMinY),  // 0 BL
        vec2(pc.bboxMaxX, pc.bboxMinY),  // 1 BR
        vec2(pc.bboxMinX, pc.bboxMaxY),  // 2 TL
        vec2(pc.bboxMaxX, pc.bboxMaxY)   // 3 TR
    );
    int remapIdx[6] = int[6](0, 1, 2, 1, 3, 2);
    vec2 localPos = bboxCorners[remapIdx[gl_VertexIndex]];

    // Pass to fragment shader for SDF evaluation.
    fragLocalPos = localPos;

    // Apply model transform (scale then translate), then camera transform.
    vec2 worldPos = localPos * pc.modelScale + vec2(pc.modelX, pc.modelY);
    float aspect  = pc.width / pc.height;
    vec2  pos     = (worldPos - vec2(pc.cameraX, pc.cameraY)) * pc.cameraZoom;
    // Negate Y: Vulkan clip space has +Y downward; world +Y is upward.
    gl_Position = vec4(pos.x / aspect, -pos.y, 0.0, 1.0);
}
