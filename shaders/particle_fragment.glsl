#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec4 fragUVBounds;  // minX, minY, maxX, maxY

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

void main() {
    // Clamp texture coordinates to UV bounds to prevent MSAA bleeding outside sprite region in atlas
    vec2 clampedUV = clamp(fragTexCoord, fragUVBounds.xy, fragUVBounds.zw);
    vec4 texColor = texture(texSampler, clampedUV);
    outColor = texColor * fragColor;
}
