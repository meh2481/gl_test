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
    float param3;           // Max Y in world space (surface)
    float param4;           // Min X in world space
    float param5;           // Min Y in world space
    float param6;           // Max X in world space
} pc;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragUVBounds;
layout(location = 2) in vec2 fragWorldPos;
layout(location = 3) in vec2 fragWaterBounds;
layout(location = 4) in vec2 fragWaterBoundsMax;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

const float PI = 3.14159265359;

// Hash function for pseudo-random values
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Smooth noise function
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Calculate animated water surface height at a given X position
// Returns offset from the base surface Y
float getWaterSurfaceHeight(float x, float time, float amplitude, float speed) {
    float wave = 0.0;

    // Primary wave - slow, gentle movement (reduced frequency for wider waves)
    wave += sin(x * 2.0 + time * speed * 0.8) * 0.5;

    // Secondary wave - slightly faster
    wave += sin(x * 3.5 - time * speed * 0.6) * 0.3;

    // Tertiary wave - small ripples
    wave += sin(x * 6.0 + time * speed * 1.2) * 0.15;

    // Tiny detail ripples
    wave += sin(x * 10.0 - time * speed * 1.5) * 0.05;

    // Add some noise for organic feel
    wave += (noise(vec2(x * 2.0 + time * speed * 0.3, time * 0.05)) - 0.5) * 0.1;

    return wave * amplitude;
}

void main() {
    vec2 waterBoundsMin = fragWaterBounds;
    vec2 waterBoundsMax = fragWaterBoundsMax;

    float waterWidth = waterBoundsMax.x - waterBoundsMin.x;
    float waterHeight = waterBoundsMax.y - waterBoundsMin.y;

    // Get water parameters
    float waterAlpha = pc.param0;
    float rippleAmplitude = pc.param1;
    float rippleSpeed = pc.param2;
    float surfaceY = waterBoundsMax.y;

    // Calculate animated surface height at this X position
    // Use world X directly for consistent wave pattern across the water
    float surfaceWaveOffset = getWaterSurfaceHeight(fragWorldPos.x, pc.time, rippleAmplitude, rippleSpeed);

    // Adjusted surface Y with wave (waves can go above the base surface)
    float animatedSurfaceY = surfaceY + surfaceWaveOffset;

    // Distance from the animated surface (positive = below surface, negative = above)
    float distFromSurface = animatedSurfaceY - fragWorldPos.y;

    // Discard pixels above the animated water surface
    if (distFromSurface < 0.0) {
        discard;
    }

    // Normalized depth (0 = at surface, 1 = at bottom)
    float normalizedDepth = clamp(distFromSurface / waterHeight, 0.0, 1.0);

    // Surface band thickness (for highlight effects) - thinner band
    float surfaceBandThickness = 0.015;
    float inSurfaceBand = smoothstep(surfaceBandThickness, 0.0, distFromSurface);

    // === WATER COLOR ===
    // Base water color gradient (lighter at surface, darker at depth)
    vec3 surfaceColor = vec3(0.2, 0.5, 0.8);     // Light blue at surface
    vec3 deepColor = vec3(0.02, 0.1, 0.25);      // Dark blue at depth
    vec3 waterColor = mix(surfaceColor, deepColor, pow(normalizedDepth, 0.5));

    // === SURFACE HIGHLIGHT ===
    // Bright highlight at the very top edge of the water
    float surfaceHighlight = pow(inSurfaceBand, 1.5);

    // Add wave-based variation to highlight (gentler variation)
    float highlightWave = sin(fragWorldPos.x * 8.0 + pc.time * rippleSpeed * 2.0) * 0.2 + 0.8;
    surfaceHighlight *= highlightWave;

    // Surface is brighter white/cyan
    vec3 highlightColor = vec3(0.9, 0.95, 1.0);
    waterColor = mix(waterColor, highlightColor, surfaceHighlight * 0.8);

    // === REFLECTION ===
    // Near the surface, add a reflection effect
    // Sample from the scene texture above the water to create reflection
    float reflectionZone = smoothstep(0.25, 0.0, normalizedDepth);

    // Calculate reflection UV - flip Y to sample from above water
    // The reflected position is mirrored across the animated surface
    float reflectedY = animatedSurfaceY + (animatedSurfaceY - fragWorldPos.y);

    // Add some wave distortion to the reflection
    float reflectDistort = surfaceWaveOffset * 0.5;

    // Convert world position to UV for texture sampling
    // Map from world space to 0-1 UV space based on screen
    vec2 reflectUV;
    // Guard against division by zero
    float aspect = pc.height > 0.0 ? pc.width / pc.height : 1.0;
    reflectUV.x = ((fragWorldPos.x + reflectDistort - pc.cameraX) * pc.cameraZoom / aspect + 1.0) * 0.5;
    reflectUV.y = ((-reflectedY - pc.cameraY) * pc.cameraZoom + 1.0) * 0.5;

    // Clamp to valid UV range
    reflectUV = clamp(reflectUV, 0.01, 0.99);

    // Sample the scene texture for reflection
    // This samples from the rendered scene above the water to create realistic reflections
    vec4 reflectedColor = texture(texSampler, reflectUV);

    // Blend reflection with water color (stronger near surface)
    // Fresnel-like effect: more reflection at grazing angles
    float fresnelFactor = pow(1.0 - normalizedDepth, 2.0) * 0.6;
    waterColor = mix(waterColor, reflectedColor.rgb, reflectionZone * fresnelFactor * reflectedColor.a);

    // === CAUSTICS (light patterns underwater) ===
    // More visible in the middle depths
    float causticDepth = smoothstep(0.05, 0.2, normalizedDepth) * smoothstep(1.0, 0.4, normalizedDepth);
    float caustic1 = sin(fragWorldPos.x * 12.0 + pc.time * 1.5 + surfaceWaveOffset * 3.0) *
                     sin(fragWorldPos.y * 10.0 - pc.time * 1.2);
    float caustic2 = sin(fragWorldPos.x * 8.0 - pc.time * 1.0) *
                     sin(fragWorldPos.y * 14.0 + pc.time * 1.3);
    float caustics = max(0.0, caustic1 + caustic2) * 0.5;
    caustics = pow(caustics, 1.5) * causticDepth * 0.2;
    waterColor += vec3(0.3, 0.5, 0.6) * caustics;

    // === FOAM/BUBBLES near surface ===
    float foamNoise = noise(fragWorldPos * 25.0 + vec2(pc.time * 0.5, pc.time * 0.3));
    float foamMask = pow(inSurfaceBand, 1.2) * step(0.7, foamNoise);
    waterColor = mix(waterColor, vec3(0.95, 0.98, 1.0), foamMask * 0.5);

    // === ALPHA ===
    // More transparent at surface, more opaque at depth
    float depthAlpha = mix(0.6, 0.92, pow(normalizedDepth, 0.4));
    float finalAlpha = waterAlpha * depthAlpha;

    // Slight transparency boost right at the surface edge for softer look
    finalAlpha *= mix(0.85, 1.0, smoothstep(0.0, 0.01, distFromSurface));

    outColor = vec4(waterColor, finalAlpha);
}
