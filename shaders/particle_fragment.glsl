#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

void main() {
    vec4 texColor = texture(texSampler, fragTexCoord);

    // Edge antialiasing: compute smooth alpha falloff at geometry edges
    // Using fwidth to get the rate of change of UV coordinates
    vec2 fw = fwidth(fragTexCoord);
    float edgeWidth = max(fw.x, fw.y);

    // Distance from UV edges (0 and 1 in both U and V)
    float distU = min(fragTexCoord.x, 1.0 - fragTexCoord.x);
    float distV = min(fragTexCoord.y, 1.0 - fragTexCoord.y);
    float edgeDist = min(distU, distV);

    // Smooth alpha at edges using smoothstep
    float edgeAlpha = smoothstep(0.0, edgeWidth, edgeDist);

    vec4 finalColor = texColor * fragColor;
    outColor = vec4(finalColor.rgb, finalColor.a * edgeAlpha);
}
