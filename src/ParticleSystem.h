#pragma once

#include <cstdint>
#include <cstdlib>
#include <cassert>

// Blend modes for particle systems
enum ParticleBlendMode {
    PARTICLE_BLEND_ADDITIVE = 0,  // Default additive blending
    PARTICLE_BLEND_ALPHA = 1      // Standard alpha blending
};

// Configuration for a single particle emitter
struct ParticleEmitterConfig {
    // Emission area (polygon vertices in local space)
    float emissionVertices[16];  // Max 8 vertices (x,y pairs)
    int emissionVertexCount;     // Number of vertices (0 = point emitter)

    // Texture IDs (hashed from texture name)
    uint64_t textureIds[8];  // Up to 8 texture variants
    int textureCount;

    // Emission settings
    float emissionRate;      // Particles per second (average)
    int maxParticles;        // Maximum number of live particles

    // Blend mode
    ParticleBlendMode blendMode;

    // Position range (random offset from emission area)
    float positionVariance;

    // Velocity range
    float velocityMinX, velocityMaxX;
    float velocityMinY, velocityMaxY;

    // Acceleration (constant per particle, applied every frame)
    float accelerationMinX, accelerationMaxX;
    float accelerationMinY, accelerationMaxY;
    float radialAccelerationMin, radialAccelerationMax;  // Towards/away from emission center

    // Initial radial velocity (towards/away from emission center)
    float radialVelocityMin, radialVelocityMax;

    // Size
    float sizeMin, sizeMax;
    float startSizeMin, startSizeMax;
    float endSizeMin, endSizeMax;

    // Color (RGBA)
    float colorMinR, colorMaxR;
    float colorMinG, colorMaxG;
    float colorMinB, colorMaxB;
    float colorMinA, colorMaxA;

    // End color (interpolate over lifetime)
    float endColorMinR, endColorMaxR;
    float endColorMinG, endColorMaxG;
    float endColorMinB, endColorMaxB;
    float endColorMinA, endColorMaxA;

    // Lifetime (seconds)
    float lifetimeMin, lifetimeMax;

    // Rotation (radians) - x/y/z Euler angles
    float rotationMinX, rotationMaxX;
    float rotationMinY, rotationMaxY;
    float rotationMinZ, rotationMaxZ;

    // Rotational velocity (radians/second)
    float rotVelocityMinX, rotVelocityMaxX;
    float rotVelocityMinY, rotVelocityMaxY;
    float rotVelocityMinZ, rotVelocityMaxZ;

    // Rotational acceleration (radians/second^2)
    float rotAccelerationMinX, rotAccelerationMaxX;
    float rotAccelerationMinY, rotAccelerationMaxY;
    float rotAccelerationMinZ, rotAccelerationMaxZ;
};

// A single particle system instance
// Uses structure-of-arrays for cache optimization
struct ParticleSystem {
    // Particle attribute arrays - each index is a different particle
    float* posX;
    float* posY;
    float* velX;
    float* velY;
    float* accelX;  // Linear acceleration
    float* accelY;
    float* radialAccel;  // Radial acceleration (towards emission center)

    float* size;
    float* startSize;
    float* endSize;

    float* colorR;
    float* colorG;
    float* colorB;
    float* colorA;

    float* endColorR;
    float* endColorG;
    float* endColorB;
    float* endColorA;

    float* lifetime;      // Remaining lifetime
    float* totalLifetime; // Original lifetime (for interpolation)

    float* rotX;  // Current rotation
    float* rotY;
    float* rotZ;

    float* rotVelX;  // Rotational velocity
    float* rotVelY;
    float* rotVelZ;

    float* rotAccelX;  // Rotational acceleration
    float* rotAccelY;
    float* rotAccelZ;

    int* textureIndex;  // Which texture from the list to use

    // Emitter state
    int maxParticles;
    int liveParticleCount;
    float emissionAccumulator;  // Fractional particles to emit

    // Emitter position (world space)
    float emitterX;
    float emitterY;

    // Emission center (calculated from polygon centroid)
    float emissionCenterX;
    float emissionCenterY;

    // Configuration (copied from emitter config)
    ParticleEmitterConfig config;

    // Pipeline ID for rendering
    int pipelineId;

    // Parallax depth for draw ordering (0 = inline with sprites)
    float parallaxDepth;
};

// Particle system manager - manages all active particle systems
class ParticleSystemManager {
public:
    ParticleSystemManager();
    ~ParticleSystemManager();

    // Create a new particle system with the given configuration
    // Returns system ID
    int createSystem(const ParticleEmitterConfig& config, int pipelineId);

    // Destroy a particle system
    void destroySystem(int systemId);

    // Set emitter position
    void setSystemPosition(int systemId, float x, float y);

    // Update emission rate
    void setSystemEmissionRate(int systemId, float rate);

    // Set parallax depth for draw ordering
    void setSystemParallaxDepth(int systemId, float depth);

    // Update all particle systems
    void update(float deltaTime);

    // Get particle system for rendering
    ParticleSystem* getSystem(int systemId);

    // Get all systems for batch rendering
    int getSystemCount() const { return systemCount_; }
    ParticleSystem* getSystems() { return systems_; }

    // Clear all particle systems (for scene cleanup)
    void clearAllSystems();

private:
    // Allocate particle arrays for a system
    void allocateParticleArrays(ParticleSystem& system, int maxParticles);

    // Free particle arrays
    void freeParticleArrays(ParticleSystem& system);

    // Spawn a single particle
    void spawnParticle(ParticleSystem& system);

    // Update a single particle
    // Returns true if particle is still alive
    bool updateParticle(ParticleSystem& system, int index, float deltaTime);

    // Remove dead particle by swapping with last live particle
    void removeParticle(ParticleSystem& system, int index);

    // Random float in range [min, max]
    static float randomRange(float minVal, float maxVal);

    // Random point inside polygon (for emission area)
    static void randomPointInPolygon(const float* vertices, int vertexCount,
                                     float* outX, float* outY);

    // Calculate polygon centroid
    static void calculatePolygonCentroid(const float* vertices, int vertexCount,
                                         float* outX, float* outY);

    ParticleSystem* systems_;
    int* systemIds_;  // Maps system ID to array index
    int systemCount_;
    int systemCapacity_;
    int nextSystemId_;
};
