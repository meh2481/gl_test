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

// FBM noise for more organic patterns
float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; i++) {
        value += amplitude * noise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

// Surface wave function for animated ripples
float surfaceWave(vec2 pos, float time) {
    float wave = 0.0;

    // Primary wave
    wave += sin(pos.x * 8.0 + time * 2.5) * 0.4;
    wave += sin(pos.x * 12.0 - time * 1.8) * 0.25;

    // Cross waves
    wave += sin((pos.x + pos.y * 0.5) * 6.0 + time * 2.0) * 0.2;
    wave += sin((pos.x - pos.y * 0.3) * 10.0 - time * 1.5) * 0.15;

    // High frequency ripples
    wave += sin(pos.x * 25.0 + time * 4.0) * 0.1;
    wave += sin(pos.x * 30.0 - time * 3.5) * 0.08;

    // Add noise-based variation
    wave += fbm(pos * 5.0 + vec2(time * 0.3, 0.0)) * 0.2 - 0.1;

    return wave;
}

void main() {
    vec2 waterBoundsMin = fragWaterBounds;
    vec2 waterBoundsMax = fragWaterBoundsMax;

    // Calculate position within water bounds (0-1)
    float waterWidth = waterBoundsMax.x - waterBoundsMin.x;
    float waterHeight = waterBoundsMax.y - waterBoundsMin.y;

    float normalizedX = (fragWorldPos.x - waterBoundsMin.x) / waterWidth;
    float normalizedY = (fragWorldPos.y - waterBoundsMin.y) / waterHeight;

    // Clamp to valid range
    normalizedX = clamp(normalizedX, 0.0, 1.0);
    normalizedY = clamp(normalizedY, 0.0, 1.0);

    // Distance from surface (top of water, where normalizedY = 1)
    float distFromSurface = 1.0 - normalizedY;

    // Get water parameters
    float waterAlpha = pc.param0;
    float rippleAmplitude = pc.param1;
    float rippleSpeed = pc.param2;

    // Calculate surface wave displacement
    float waveHeight = surfaceWave(fragWorldPos * 3.0, pc.time * rippleSpeed) * rippleAmplitude * 20.0;

    // Water base color gradient (lighter at surface, darker at depth)
    vec3 surfaceColor = vec3(0.2, 0.5, 0.8);
    vec3 deepColor = vec3(0.05, 0.15, 0.35);
    vec3 waterColor = mix(surfaceColor, deepColor, distFromSurface);

    // Add animated caustic patterns
    float caustic1 = sin(fragWorldPos.x * 15.0 + pc.time * 1.5 + waveHeight * 2.0) *
                     sin(fragWorldPos.y * 18.0 - pc.time * 1.2);
    float caustic2 = sin(fragWorldPos.x * 22.0 - pc.time * 1.8 + waveHeight) *
                     sin(fragWorldPos.y * 20.0 + pc.time * 1.4);
    float caustics = (caustic1 + caustic2) * 0.5 + 0.5;
    caustics = pow(caustics, 2.0) * 0.3;

    // Caustics are more visible near surface
    waterColor += vec3(0.3, 0.5, 0.7) * caustics * (1.0 - distFromSurface * 0.7);

    // Surface highlight band at the very top
    float surfaceGlow = pow(max(0.0, 1.0 - distFromSurface * 4.0), 2.0);

    // Add animated ripple highlight at surface
    float surfaceRipple = surfaceWave(fragWorldPos * 5.0, pc.time * rippleSpeed * 1.5);
    surfaceGlow *= (1.0 + surfaceRipple * 0.5);
    waterColor += vec3(0.8, 0.9, 1.0) * surfaceGlow * 0.6;

    // Reflection simulation - fake reflection by sampling offset positions
    float reflectionStrength = pow(normalizedY, 2.0) * 0.4;
    vec2 reflectOffset = vec2(waveHeight * 0.1, 0.0);
    float reflectNoise = fbm((fragWorldPos + reflectOffset) * 3.0 + vec2(0.0, pc.time * 0.2));
    vec3 reflectionColor = vec3(0.6, 0.7, 0.9) * reflectNoise;
    waterColor = mix(waterColor, reflectionColor, reflectionStrength);

    // Add foam/bubbles near surface
    float foamNoise = fbm(fragWorldPos * 20.0 + vec2(pc.time * 0.5, pc.time * 0.3));
    float foamMask = pow(max(0.0, 1.0 - distFromSurface * 3.0), 3.0);
    foamMask *= step(0.6, foamNoise);
    waterColor = mix(waterColor, vec3(0.95, 0.98, 1.0), foamMask * 0.5);

    // Add subtle shimmer
    float shimmer = sin(fragWorldPos.x * 50.0 + pc.time * 5.0) *
                    sin(fragWorldPos.y * 45.0 - pc.time * 4.0);
    shimmer = pow(max(0.0, shimmer), 4.0) * 0.1;
    waterColor += vec3(1.0) * shimmer * (1.0 - distFromSurface * 0.5);

    // Depth-based alpha - more transparent near surface, more opaque deeper
    float depthAlpha = mix(0.5, 0.85, pow(distFromSurface, 0.7));

    // Final alpha
    float finalAlpha = waterAlpha * depthAlpha;

    outColor = vec4(waterColor, finalAlpha);
}
