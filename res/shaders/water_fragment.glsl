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
    // Splash ripple data (4 ripples x 3 values = 12 floats)
    float ripple0_x;
    float ripple0_time;
    float ripple0_amplitude;
    float ripple1_x;
    float ripple1_time;
    float ripple1_amplitude;
    float ripple2_x;
    float ripple2_time;
    float ripple2_amplitude;
    float ripple3_x;
    float ripple3_time;
    float ripple3_amplitude;
    // Unused animation slots
    float unused0;
    float unused1;
    float unused2;
    float unused3;
    float unused4;
    float unused5;
    float unused6;
    float unused7;
} pc;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragUVBounds;
layout(location = 2) in vec2 fragWorldPos;
layout(location = 3) in vec2 fragWaterBounds;
layout(location = 4) in vec2 fragWaterBoundsMax;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;        // Primary texture (unused for water)
layout(binding = 1) uniform sampler2D reflectionSampler; // Reflection render target

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

    // Primary wave - medium movement
    wave += sin(x * 4.0 + time * speed * 1.0) * 0.4;

    // Secondary wave - slightly faster
    wave += sin(x * 7.0 - time * speed * 0.7) * 0.3;

    // Tertiary wave - small ripples
    wave += sin(x * 12.0 + time * speed * 1.4) * 0.2;

    // Tiny detail ripples
    wave += sin(x * 20.0 - time * speed * 2.0) * 0.1;

    // Add some noise for organic feel
    wave += (noise(vec2(x * 3.0 + time * speed * 0.4, time * 0.08)) - 0.5) * 0.15;

    return wave * amplitude;
}

// Calculate splash ripple contribution at a given X position
// Each ripple spreads outward from its origin point
float getSplashRippleHeight(float x, float rippleX, float rippleTime, float rippleAmplitude) {
    if (rippleAmplitude <= 0.0 || rippleTime < 0.0) {
        return 0.0;
    }

    // Ripple parameters
    float rippleSpeed = 2.0;       // Speed of ripple propagation
    float rippleDecay = 0.8;       // How fast the ripple fades
    float rippleFrequency = 25.0;  // Frequency of the ripple waves

    // Distance from ripple origin
    float dist = abs(x - rippleX);

    // How far the ripple has traveled
    float rippleRadius = rippleTime * rippleSpeed;

    // Only show ripple where it has reached
    if (dist > rippleRadius + 0.05) {
        return 0.0;
    }

    // Decay over time and distance
    float timeDecay = exp(-rippleTime * rippleDecay);
    float distDecay = exp(-dist * 3.0);

    // Calculate wave at this distance from the ripple center
    float phase = dist * rippleFrequency - rippleTime * rippleSpeed * 8.0;
    float wave = sin(phase) * timeDecay * distDecay * rippleAmplitude;

    // Fade in at the wavefront
    float wavefrontDist = abs(dist - rippleRadius);
    float wavefrontFade = smoothstep(0.1, 0.0, wavefrontDist);

    return wave * wavefrontFade;
}

// Calculate total splash ripple contribution from all active ripples
float getTotalSplashHeight(float x) {
    float totalSplash = 0.0;

    // Add contribution from each ripple
    totalSplash += getSplashRippleHeight(x, pc.ripple0_x, pc.ripple0_time, pc.ripple0_amplitude);
    totalSplash += getSplashRippleHeight(x, pc.ripple1_x, pc.ripple1_time, pc.ripple1_amplitude);
    totalSplash += getSplashRippleHeight(x, pc.ripple2_x, pc.ripple2_time, pc.ripple2_amplitude);
    totalSplash += getSplashRippleHeight(x, pc.ripple3_x, pc.ripple3_time, pc.ripple3_amplitude);

    return totalSplash;
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

    // Calculate animated surface height at this X position (ambient waves)
    float surfaceWaveOffset = getWaterSurfaceHeight(fragWorldPos.x, pc.time, rippleAmplitude, rippleSpeed);

    // Add splash ripple contributions from physics objects
    float splashOffset = getTotalSplashHeight(fragWorldPos.x);

    // Adjusted surface Y with combined wave and splash effects
    float animatedSurfaceY = surfaceY + surfaceWaveOffset + splashOffset;

    // Distance from the animated surface (positive = below surface, negative = above)
    float distFromSurface = animatedSurfaceY - fragWorldPos.y;

    // Discard pixels above the animated water surface
    if (distFromSurface < 0.0) {
        discard;
    }

    // Normalized depth (0 = at surface, 1 = at bottom)
    float normalizedDepth = clamp(distFromSurface / waterHeight, 0.0, 1.0);

    // Surface band thickness (for highlight effects)
    float surfaceBandThickness = 0.02;
    float inSurfaceBand = smoothstep(surfaceBandThickness, 0.0, distFromSurface);

    // === WATER COLOR ===
    // Base water color gradient (lighter at surface, darker at depth)
    vec3 surfaceColor = vec3(0.15, 0.45, 0.75);   // Light blue at surface
    vec3 deepColor = vec3(0.02, 0.10, 0.25);      // Dark blue at depth
    vec3 waterColor = mix(surfaceColor, deepColor, pow(normalizedDepth, 0.5));

    // === SURFACE HIGHLIGHT ===
    // Bright highlight at the very top edge of the water
    float surfaceHighlight = pow(inSurfaceBand, 1.5);

    // Add wave-based variation to highlight
    float highlightWave = sin(fragWorldPos.x * 12.0 + pc.time * rippleSpeed * 2.5) * 0.25 + 0.75;
    surfaceHighlight *= highlightWave;

    // Surface is brighter white/cyan
    vec3 highlightColor = vec3(0.85, 0.92, 1.0);
    waterColor = mix(waterColor, highlightColor, surfaceHighlight * 0.7);

    // === REFLECTION (using render-to-texture) ===
    // Reflection is strongest near surface
    float reflectionZone = smoothstep(0.4, 0.0, normalizedDepth);

    // Add wave distortion to the reflection
    float reflectDistort = surfaceWaveOffset * 2.0;

    // Calculate UV for sampling the reflection texture
    // The reflection texture contains the scene rendered with flipped Y
    vec2 reflectUV;
    float aspect = pc.height > 0.0 ? pc.width / pc.height : 1.0;

    // Map world position to screen UV, with wave distortion
    // Flip X to correct for the horizontal mirror caused by negative zoom in reflection pass
    reflectUV.x = 1.0 - ((fragWorldPos.x + reflectDistort * 0.1 - pc.cameraX) * pc.cameraZoom / aspect + 1.0) * 0.5;

    // For reflection, calculate the mirrored Y position relative to water surface
    // The reflected position is: reflectedY = 2 * surfaceY - fragWorldPos.y
    // This ensures reflection stays fixed relative to water surface, not camera
    float reflectedWorldY = 2.0 * surfaceY - fragWorldPos.y + reflectDistort * 0.15;
    reflectUV.y = ((reflectedWorldY + pc.cameraY) * pc.cameraZoom + 1.0) * 0.5;

    // Clamp to valid UV range
    reflectUV = clamp(reflectUV, 0.01, 0.99);

    // Sample the reflection render target
    vec4 reflectedColor = texture(reflectionSampler, reflectUV);

    // Fresnel effect - more reflection at grazing angles (near surface)
    float fresnelFactor = pow(1.0 - normalizedDepth, 3.0) * 0.7;

    // Blend reflection with water color (stronger near surface, weaker at depth)
    // Use the reflection's alpha to weight how much of the reflected color to use
    // This prevents low-alpha areas (transparent objects) from contributing black
    float reflectionStrength = reflectionZone * fresnelFactor * reflectedColor.a;
    waterColor = mix(waterColor, reflectedColor.rgb, reflectionStrength);

    // === CAUSTICS (light patterns underwater) ===
    float causticDepth = smoothstep(0.08, 0.25, normalizedDepth) * smoothstep(1.0, 0.5, normalizedDepth);
    float caustic1 = sin(fragWorldPos.x * 15.0 + pc.time * 1.8 + surfaceWaveOffset * 4.0) *
                     sin(fragWorldPos.y * 12.0 - pc.time * 1.4);
    float caustic2 = sin(fragWorldPos.x * 10.0 - pc.time * 1.2) *
                     sin(fragWorldPos.y * 18.0 + pc.time * 1.6);
    float caustics = max(0.0, caustic1 + caustic2) * 0.5;
    caustics = pow(caustics, 1.5) * causticDepth * 0.2;
    waterColor += vec3(0.25, 0.45, 0.55) * caustics;

    // === FOAM/BUBBLES near surface ===
    float foamNoise = noise(fragWorldPos * 30.0 + vec2(pc.time * 0.6, pc.time * 0.4));
    float foamMask = pow(inSurfaceBand, 1.3) * step(0.68, foamNoise);
    waterColor = mix(waterColor, vec3(0.92, 0.96, 1.0), foamMask * 0.55);

    // === ALPHA ===
    // More transparent at surface, more opaque at depth
    float depthAlpha = mix(0.55, 0.90, pow(normalizedDepth, 0.45));
    float finalAlpha = waterAlpha * depthAlpha;

    // Slight transparency boost right at the surface edge for softer look
    finalAlpha *= mix(0.82, 1.0, smoothstep(0.0, 0.015, distFromSurface));

    outColor = vec4(waterColor, finalAlpha);
}
