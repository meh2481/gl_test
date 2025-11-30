#version 450

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);

    // Edge antialiasing: compute smooth alpha falloff at geometry edges
    // This works when UV coordinates span 0-1 across the geometry
    // Using fwidth to get the rate of change of UV coordinates
    vec2 fw = fwidth(fragTexCoord);
    float edgeWidth = max(fw.x, fw.y);

    // Distance from UV edges (0 and 1 in both U and V)
    float distU = min(fragTexCoord.x, 1.0 - fragTexCoord.x);
    float distV = min(fragTexCoord.y, 1.0 - fragTexCoord.y);
    float edgeDist = min(distU, distV);

    // Smooth alpha at edges using smoothstep
    // Only apply smoothing within one pixel of the edge
    float edgeAlpha = smoothstep(0.0, edgeWidth, edgeDist);

    outColor = vec4(texColor.rgb, texColor.a * edgeAlpha);
}
