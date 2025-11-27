#version 450

layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float cameraX;
    float cameraY;
    float cameraZoom;
    float glowColor_r;
    float glowColor_g;
    float glowColor_b;
    float glowIntensity;
} pc;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    // Create a blade glow effect based on UV coordinates
    // The blade runs along the Y axis (0 = hilt end, 1 = tip)
    // X = 0.5 is the center of the blade

    // Distance from center of blade (X = 0.5)
    float distFromCenter = abs(fragTexCoord.x - 0.5) * 2.0;

    // Core of the blade is bright white, edges are colored
    float coreWidth = 0.2;
    float glowWidth = 0.8;

    // Core intensity (hot white center)
    float coreIntensity = 1.0 - smoothstep(0.0, coreWidth, distFromCenter);

    // Glow intensity (colored outer glow)
    float glowIntensity = 1.0 - smoothstep(coreWidth, glowWidth, distFromCenter);

    // Fade at the tip
    float tipFade = smoothstep(0.0, 0.1, fragTexCoord.y) * (1.0 - smoothstep(0.9, 1.0, fragTexCoord.y));

    // Pulsing effect
    float pulse = 0.9 + 0.1 * sin(pc.time * 8.0);

    // Core is white-hot
    vec3 coreColor = vec3(1.0, 1.0, 1.0);
    // Glow is the saber color
    vec3 glowColor = vec3(pc.glowColor_r, pc.glowColor_g, pc.glowColor_b);

    // Blend core and glow
    vec3 finalColor = mix(glowColor * glowIntensity, coreColor, coreIntensity) * pulse * pc.glowIntensity;

    // Apply tip fade and edge fade
    float alpha = max(coreIntensity, glowIntensity) * tipFade;

    // Cut off if too faint
    if (alpha < 0.01) discard;

    outColor = vec4(finalColor, alpha);
}
