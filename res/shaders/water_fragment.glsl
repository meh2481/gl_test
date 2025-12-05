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
float getWaterSurfaceHeight(float x, float time, float amplitude, float speed) {
    float wave = 0.0;

    // Primary wave - slow, large movement
    wave += sin(x * 6.0 + time * speed * 1.2) * 0.4;

    // Secondary wave - faster, medium
    wave += sin(x * 10.0 - time * speed * 0.9) * 0.25;

    // Tertiary wave - even faster, small ripples
    wave += sin(x * 18.0 + time * speed * 1.8) * 0.15;

    // Small high-frequency detail
    wave += sin(x * 35.0 - time * speed * 2.5) * 0.08;

    // Add some noise for organic feel
    wave += (noise(vec2(x * 8.0 + time * speed * 0.5, time * 0.1)) - 0.5) * 0.12;

    return wave * amplitude;
}

void main() {
    vec2 waterBoundsMin = fragWaterBounds;
    vec2 waterBoundsMax = fragWaterBoundsMax;

    float waterWidth = waterBoundsMax.x - waterBoundsMin.x;
    float waterHeight = waterBoundsMax.y - waterBoundsMin.y;

    // Normalized position within water bounds
    float normalizedX = (fragWorldPos.x - waterBoundsMin.x) / waterWidth;
    float normalizedY = (fragWorldPos.y - waterBoundsMin.y) / waterHeight;

    // Get water parameters
    float waterAlpha = pc.param0;
    float rippleAmplitude = pc.param1;
    float rippleSpeed = pc.param2;
    float surfaceY = waterBoundsMax.y;

    // Calculate animated surface height at this X position
    float surfaceWaveOffset = getWaterSurfaceHeight(fragWorldPos.x * 10.0, pc.time, rippleAmplitude, rippleSpeed);

    // Adjusted surface Y with wave
    float animatedSurfaceY = surfaceY + surfaceWaveOffset;

    // Distance from the animated surface (positive = below surface, negative = above)
    float distFromSurface = animatedSurfaceY - fragWorldPos.y;

    // Discard pixels above the animated water surface
    if (distFromSurface < 0.0) {
        discard;
    }

    // Normalized depth (0 = at surface, 1 = at bottom)
    float normalizedDepth = clamp(distFromSurface / waterHeight, 0.0, 1.0);

    // Surface band thickness (for highlight effects)
    float surfaceBandThickness = 0.08;
    float inSurfaceBand = smoothstep(surfaceBandThickness, 0.0, distFromSurface);

    // === WATER COLOR ===
    // Base water color gradient (lighter at surface, darker at depth)
    vec3 surfaceColor = vec3(0.15, 0.45, 0.75);   // Light blue at surface
    vec3 midColor = vec3(0.08, 0.30, 0.55);       // Medium blue
    vec3 deepColor = vec3(0.02, 0.12, 0.30);      // Dark blue at depth
    vec3 waterColor = mix(surfaceColor, deepColor, pow(normalizedDepth, 0.6));

    // === SURFACE HIGHLIGHT ===
    // Bright highlight at the very top edge of the water
    float surfaceHighlight = pow(inSurfaceBand, 2.0);

    // Add wave-based variation to highlight
    float highlightWave = sin(fragWorldPos.x * 25.0 + pc.time * rippleSpeed * 3.0) * 0.3 + 0.7;
    surfaceHighlight *= highlightWave;

    // Surface is brighter white/cyan
    vec3 highlightColor = vec3(0.85, 0.95, 1.0);
    waterColor = mix(waterColor, highlightColor, surfaceHighlight * 0.7);

    // === REFLECTION (fake) ===
    // Near the surface, add a reflection effect
    // This simulates reflecting the sky/scene above by using procedural patterns
    float reflectionZone = smoothstep(0.3, 0.0, normalizedDepth);

    // Animated reflection pattern
    float reflectX = fragWorldPos.x + surfaceWaveOffset * 2.0;
    float reflectPattern = noise(vec2(reflectX * 5.0, pc.time * 0.3)) * 0.5 +
                           noise(vec2(reflectX * 12.0, pc.time * 0.5)) * 0.3;

    // Reflection color (sky-like colors)
    vec3 reflectionColor = mix(vec3(0.6, 0.75, 0.9), vec3(0.8, 0.85, 0.95), reflectPattern);
    waterColor = mix(waterColor, reflectionColor, reflectionZone * 0.4);

    // === CAUSTICS (light patterns underwater) ===
    // More visible in the middle depths
    float causticDepth = smoothstep(0.0, 0.3, normalizedDepth) * smoothstep(1.0, 0.5, normalizedDepth);
    float caustic1 = sin(fragWorldPos.x * 20.0 + pc.time * 2.0 + surfaceWaveOffset * 5.0) *
                     sin(fragWorldPos.y * 15.0 - pc.time * 1.5);
    float caustic2 = sin(fragWorldPos.x * 15.0 - pc.time * 1.3) *
                     sin(fragWorldPos.y * 22.0 + pc.time * 1.8);
    float caustics = max(0.0, caustic1 + caustic2) * 0.5;
    caustics = pow(caustics, 1.5) * causticDepth * 0.25;
    waterColor += vec3(0.3, 0.5, 0.6) * caustics;

    // === FOAM/BUBBLES near surface ===
    float foamNoise = noise(fragWorldPos * 40.0 + vec2(pc.time * 0.8, pc.time * 0.5));
    float foamMask = pow(inSurfaceBand, 1.5) * step(0.65, foamNoise);
    waterColor = mix(waterColor, vec3(0.95, 0.98, 1.0), foamMask * 0.6);

    // === ALPHA ===
    // More transparent at surface, more opaque at depth
    float depthAlpha = mix(0.55, 0.90, pow(normalizedDepth, 0.5));
    float finalAlpha = waterAlpha * depthAlpha;

    // Slight transparency boost right at the surface edge for softer look
    finalAlpha *= mix(0.8, 1.0, smoothstep(0.0, 0.02, distFromSurface));

    outColor = vec4(waterColor, finalAlpha);
}
