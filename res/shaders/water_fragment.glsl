#version 450

layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float cameraX;
    float cameraY;
    float cameraZoom;
    float param0;           // Water alpha
    float param1;           // Ripple amplitude
    float param2;           // Ripple speed
    float param3;           // Surface Y (normalized 0-1)
    float param4;           // Min X in world space
    float param5;           // Min Y in world space
    float param6;           // Width in world space
} pc;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragUVBounds;
layout(location = 2) in vec2 fragWorldPos;
layout(location = 3) in vec2 fragWaterBounds;
layout(location = 4) in vec2 fragWaterBoundsMax;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

const float PI = 3.14159265359;

// Simple noise function for ambient ripples
float noise(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// Smooth noise
float smoothNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = noise(i);
    float b = noise(i + vec2(1.0, 0.0));
    float c = noise(i + vec2(0.0, 1.0));
    float d = noise(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec2 waterBoundsMin = fragWaterBounds;
    vec2 waterBoundsMax = fragWaterBoundsMax;

    // Calculate position within water bounds (0-1)
    float normalizedX = (fragWorldPos.x - waterBoundsMin.x) / (waterBoundsMax.x - waterBoundsMin.x);
    float normalizedY = (fragWorldPos.y - waterBoundsMin.y) / (waterBoundsMax.y - waterBoundsMin.y);

    // Distance from surface (top of water)
    float distFromSurface = 1.0 - normalizedY;

    // Get water parameters
    float waterAlpha = pc.param0;
    float rippleAmplitude = pc.param1;
    float rippleSpeed = pc.param2;

    // Calculate ripple displacement for surface distortion
    float displacement = 0.0;

    // Ambient ripples using sine waves
    float ambientFreq1 = 15.0;
    float ambientFreq2 = 23.0;
    float ambientSpeed1 = rippleSpeed;
    float ambientSpeed2 = rippleSpeed * 0.7;

    displacement += sin(fragWorldPos.x * ambientFreq1 + pc.time * ambientSpeed1) * rippleAmplitude * 0.5;
    displacement += sin(fragWorldPos.x * ambientFreq2 - pc.time * ambientSpeed2) * rippleAmplitude * 0.3;

    // Add some noise-based variation
    displacement += smoothNoise(fragWorldPos * 10.0 + vec2(pc.time * 0.5, 0.0)) * rippleAmplitude * 0.2;

    // Water base color (blue-ish with slight green tint)
    vec3 waterColor = vec3(0.1, 0.3, 0.6);

    // Calculate reflection UV - sample from above the water
    vec2 reflectUV = fragTexCoord;

    // Apply ripple distortion to reflection
    float distortX = displacement * 0.02;
    float distortY = displacement * 0.01;
    reflectUV.x += distortX;
    reflectUV.y += distortY;

    // Clamp reflection UV to valid bounds
    reflectUV = clamp(reflectUV, fragUVBounds.xy, fragUVBounds.zw);

    // Sample the texture for reflection (could be scene reflection in more complex setup)
    vec4 reflectionColor = texture(texSampler, reflectUV);

    // Fresnel effect - more reflection at grazing angles (near surface)
    float fresnel = 0.3 + 0.7 * pow(normalizedY, 2.0);

    // Depth-based alpha - more transparent near surface, more opaque deeper
    float depthAlpha = mix(0.4, 0.9, distFromSurface);

    // Combine water color with subtle reflection
    vec3 finalColor = waterColor;

    // Add reflection based on fresnel (if texture provides meaningful reflection data)
    if (reflectionColor.a > 0.1) {
        finalColor = mix(waterColor, reflectionColor.rgb * 0.5, fresnel * 0.3);
    }

    // Add surface highlights at the top
    float surfaceHighlight = pow(max(0.0, 1.0 - distFromSurface * 8.0), 2.0);
    surfaceHighlight *= (1.0 + displacement * 3.0);
    finalColor += vec3(1.0, 1.0, 1.0) * surfaceHighlight * 0.4;

    // Add caustics effect (light patterns on water)
    float caustic1 = sin(fragWorldPos.x * 20.0 + pc.time * 2.0) * sin(fragWorldPos.y * 25.0 - pc.time * 1.5);
    float caustic2 = sin(fragWorldPos.x * 25.0 - pc.time * 1.8) * sin(fragWorldPos.y * 20.0 + pc.time * 2.2);
    float caustics = (caustic1 + caustic2) * 0.5 + 0.5;
    caustics = pow(caustics, 3.0) * 0.15;
    finalColor += vec3(0.4, 0.6, 0.8) * caustics * (1.0 - distFromSurface * 0.5);

    // Add edge foam/bubbles near the surface
    float foam = smoothNoise(fragWorldPos * 30.0 + vec2(pc.time * 0.3, pc.time * 0.2));
    foam *= pow(max(0.0, 1.0 - distFromSurface * 5.0), 3.0);
    finalColor += vec3(0.9, 0.95, 1.0) * foam * 0.2;

    // Apply final alpha
    float finalAlpha = waterAlpha * depthAlpha;

    outColor = vec4(finalColor, finalAlpha);
}
