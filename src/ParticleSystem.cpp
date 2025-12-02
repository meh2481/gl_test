#include "ParticleSystem.h"
#include <cmath>
#include <cstring>

// Simple linear congruential generator for fast random numbers
static unsigned int s_randomSeed = 12345;

static unsigned int fastRandom() {
    s_randomSeed = s_randomSeed * 1103515245 + 12345;
    return (s_randomSeed >> 16) & 0x7FFF;
}

static float fastRandomFloat() {
    return (float)fastRandom() / 32767.0f;
}

ParticleSystemManager::ParticleSystemManager()
    : systems_(nullptr), systemIds_(nullptr), systemCount_(0), systemCapacity_(0), nextSystemId_(1) {
}

ParticleSystemManager::~ParticleSystemManager() {
    // Free all particle systems
    for (int i = 0; i < systemCount_; ++i) {
        freeParticleArrays(systems_[i]);
    }
    free(systems_);
    free(systemIds_);
}

float ParticleSystemManager::randomRange(float minVal, float maxVal) {
    return minVal + fastRandomFloat() * (maxVal - minVal);
}

void ParticleSystemManager::calculatePolygonCentroid(const float* vertices, int vertexCount,
                                                      float* outX, float* outY) {
    if (vertexCount <= 0) {
        *outX = 0.0f;
        *outY = 0.0f;
        return;
    }

    float cx = 0.0f;
    float cy = 0.0f;
    for (int i = 0; i < vertexCount; ++i) {
        cx += vertices[i * 2];
        cy += vertices[i * 2 + 1];
    }
    *outX = cx / (float)vertexCount;
    *outY = cy / (float)vertexCount;
}

void ParticleSystemManager::randomPointInPolygon(const float* vertices, int vertexCount,
                                                  float* outX, float* outY) {
    if (vertexCount <= 0) {
        *outX = 0.0f;
        *outY = 0.0f;
        return;
    }

    if (vertexCount == 1) {
        *outX = vertices[0];
        *outY = vertices[1];
        return;
    }

    if (vertexCount == 2) {
        // Line segment - random point on the line
        float t = fastRandomFloat();
        *outX = vertices[0] + t * (vertices[2] - vertices[0]);
        *outY = vertices[1] + t * (vertices[3] - vertices[1]);
        return;
    }

    // For polygon with 3+ vertices, use triangle fan approach
    // Pick a random triangle from the fan, then random point in that triangle
    int triIndex = (int)(fastRandomFloat() * (float)(vertexCount - 2));
    if (triIndex >= vertexCount - 2) triIndex = vertexCount - 3;

    // Triangle vertices: first vertex + two consecutive vertices
    float x0 = vertices[0];
    float y0 = vertices[1];
    float x1 = vertices[(triIndex + 1) * 2];
    float y1 = vertices[(triIndex + 1) * 2 + 1];
    float x2 = vertices[(triIndex + 2) * 2];
    float y2 = vertices[(triIndex + 2) * 2 + 1];

    // Random point in triangle using barycentric coordinates
    float u = fastRandomFloat();
    float v = fastRandomFloat();
    if (u + v > 1.0f) {
        u = 1.0f - u;
        v = 1.0f - v;
    }
    float w = 1.0f - u - v;

    *outX = w * x0 + u * x1 + v * x2;
    *outY = w * y0 + u * y1 + v * y2;
}

void ParticleSystemManager::allocateParticleArrays(ParticleSystem& system, int maxParticles) {
    system.maxParticles = maxParticles;
    system.liveParticleCount = 0;

    // Allocate all arrays
    system.posX = (float*)malloc(maxParticles * sizeof(float));
    system.posY = (float*)malloc(maxParticles * sizeof(float));
    system.velX = (float*)malloc(maxParticles * sizeof(float));
    system.velY = (float*)malloc(maxParticles * sizeof(float));
    system.accelX = (float*)malloc(maxParticles * sizeof(float));
    system.accelY = (float*)malloc(maxParticles * sizeof(float));
    system.radialAccel = (float*)malloc(maxParticles * sizeof(float));

    system.size = (float*)malloc(maxParticles * sizeof(float));
    system.startSize = (float*)malloc(maxParticles * sizeof(float));
    system.endSize = (float*)malloc(maxParticles * sizeof(float));

    system.colorR = (float*)malloc(maxParticles * sizeof(float));
    system.colorG = (float*)malloc(maxParticles * sizeof(float));
    system.colorB = (float*)malloc(maxParticles * sizeof(float));
    system.colorA = (float*)malloc(maxParticles * sizeof(float));

    system.endColorR = (float*)malloc(maxParticles * sizeof(float));
    system.endColorG = (float*)malloc(maxParticles * sizeof(float));
    system.endColorB = (float*)malloc(maxParticles * sizeof(float));
    system.endColorA = (float*)malloc(maxParticles * sizeof(float));

    system.lifetime = (float*)malloc(maxParticles * sizeof(float));
    system.totalLifetime = (float*)malloc(maxParticles * sizeof(float));

    system.rotX = (float*)malloc(maxParticles * sizeof(float));
    system.rotY = (float*)malloc(maxParticles * sizeof(float));
    system.rotZ = (float*)malloc(maxParticles * sizeof(float));

    system.rotVelX = (float*)malloc(maxParticles * sizeof(float));
    system.rotVelY = (float*)malloc(maxParticles * sizeof(float));
    system.rotVelZ = (float*)malloc(maxParticles * sizeof(float));

    system.rotAccelX = (float*)malloc(maxParticles * sizeof(float));
    system.rotAccelY = (float*)malloc(maxParticles * sizeof(float));
    system.rotAccelZ = (float*)malloc(maxParticles * sizeof(float));

    system.textureIndex = (int*)malloc(maxParticles * sizeof(int));

    // Verify allocations
    assert(system.posX && system.posY && system.velX && system.velY);
    assert(system.accelX && system.accelY && system.radialAccel);
    assert(system.size && system.startSize && system.endSize);
    assert(system.colorR && system.colorG && system.colorB && system.colorA);
    assert(system.endColorR && system.endColorG && system.endColorB && system.endColorA);
    assert(system.lifetime && system.totalLifetime);
    assert(system.rotX && system.rotY && system.rotZ);
    assert(system.rotVelX && system.rotVelY && system.rotVelZ);
    assert(system.rotAccelX && system.rotAccelY && system.rotAccelZ);
    assert(system.textureIndex);
}

void ParticleSystemManager::freeParticleArrays(ParticleSystem& system) {
    free(system.posX);
    free(system.posY);
    free(system.velX);
    free(system.velY);
    free(system.accelX);
    free(system.accelY);
    free(system.radialAccel);

    free(system.size);
    free(system.startSize);
    free(system.endSize);

    free(system.colorR);
    free(system.colorG);
    free(system.colorB);
    free(system.colorA);

    free(system.endColorR);
    free(system.endColorG);
    free(system.endColorB);
    free(system.endColorA);

    free(system.lifetime);
    free(system.totalLifetime);

    free(system.rotX);
    free(system.rotY);
    free(system.rotZ);

    free(system.rotVelX);
    free(system.rotVelY);
    free(system.rotVelZ);

    free(system.rotAccelX);
    free(system.rotAccelY);
    free(system.rotAccelZ);

    free(system.textureIndex);

    system.maxParticles = 0;
    system.liveParticleCount = 0;
}

int ParticleSystemManager::createSystem(const ParticleEmitterConfig& config, int pipelineId) {
    // Grow arrays if needed
    if (systemCount_ >= systemCapacity_) {
        int newCapacity = systemCapacity_ == 0 ? 8 : systemCapacity_ * 2;
        ParticleSystem* newSystems = (ParticleSystem*)realloc(systems_, newCapacity * sizeof(ParticleSystem));
        int* newIds = (int*)realloc(systemIds_, newCapacity * sizeof(int));
        assert(newSystems && newIds);
        systems_ = newSystems;
        systemIds_ = newIds;
        systemCapacity_ = newCapacity;
    }

    int id = nextSystemId_++;
    int index = systemCount_;
    systemIds_[index] = id;

    ParticleSystem& system = systems_[index];
    memset(&system, 0, sizeof(ParticleSystem));

    // Copy configuration
    system.config = config;
    system.pipelineId = pipelineId;
    system.parallaxDepth = 0.0f;  // Default: inline with sprites at depth 0
    system.emitterX = 0.0f;
    system.emitterY = 0.0f;
    system.emissionAccumulator = 0.0f;

    // Calculate emission center
    calculatePolygonCentroid(config.emissionVertices, config.emissionVertexCount,
                             &system.emissionCenterX, &system.emissionCenterY);

    // Allocate particle arrays
    allocateParticleArrays(system, config.maxParticles);

    systemCount_++;
    return id;
}

void ParticleSystemManager::destroySystem(int systemId) {
    // Find system by ID
    for (int i = 0; i < systemCount_; ++i) {
        if (systemIds_[i] == systemId) {
            freeParticleArrays(systems_[i]);

            // Swap with last element
            if (i < systemCount_ - 1) {
                systems_[i] = systems_[systemCount_ - 1];
                systemIds_[i] = systemIds_[systemCount_ - 1];
            }
            systemCount_--;
            return;
        }
    }
}

void ParticleSystemManager::setSystemPosition(int systemId, float x, float y) {
    ParticleSystem* system = getSystem(systemId);
    if (system) {
        system->emitterX = x;
        system->emitterY = y;
    }
}

void ParticleSystemManager::setSystemEmissionRate(int systemId, float rate) {
    ParticleSystem* system = getSystem(systemId);
    if (system) {
        system->config.emissionRate = rate;
    }
}

void ParticleSystemManager::setSystemParallaxDepth(int systemId, float depth) {
    ParticleSystem* system = getSystem(systemId);
    if (system) {
        system->parallaxDepth = depth;
    }
}

ParticleSystem* ParticleSystemManager::getSystem(int systemId) {
    for (int i = 0; i < systemCount_; ++i) {
        if (systemIds_[i] == systemId) {
            return &systems_[i];
        }
    }
    return nullptr;
}

void ParticleSystemManager::spawnParticle(ParticleSystem& system) {
    if (system.liveParticleCount >= system.maxParticles) {
        return;  // No room for more particles
    }

    int i = system.liveParticleCount;
    const ParticleEmitterConfig& cfg = system.config;

    // Get base position from emission area
    float baseX, baseY;
    if (cfg.emissionVertexCount > 0) {
        randomPointInPolygon(cfg.emissionVertices, cfg.emissionVertexCount, &baseX, &baseY);
    } else {
        baseX = 0.0f;
        baseY = 0.0f;
    }

    // Add position variance and emitter position
    float variance = cfg.positionVariance;
    system.posX[i] = system.emitterX + baseX + randomRange(-variance, variance);
    system.posY[i] = system.emitterY + baseY + randomRange(-variance, variance);

    // Velocity
    system.velX[i] = randomRange(cfg.velocityMinX, cfg.velocityMaxX);
    system.velY[i] = randomRange(cfg.velocityMinY, cfg.velocityMaxY);

    // Apply initial radial velocity (towards/away from emission center)
    float radialVel = randomRange(cfg.radialVelocityMin, cfg.radialVelocityMax);
    if (radialVel != 0.0f) {
        float emitterWorldCenterX = system.emitterX + system.emissionCenterX;
        float emitterWorldCenterY = system.emitterY + system.emissionCenterY;
        float dx = system.posX[i] - emitterWorldCenterX;
        float dy = system.posY[i] - emitterWorldCenterY;
        float dist = sqrtf(dx * dx + dy * dy);
        float dirX, dirY;
        if (dist > 0.001f) {
            // Use direction from center to particle
            dirX = dx / dist;
            dirY = dy / dist;
        } else {
            // Particle is at center (point emitter or coincidence) - use random direction
            float angle = randomRange(0.0f, 2.0f * 3.14159265359f);
            dirX = cosf(angle);
            dirY = sinf(angle);
        }
        // Add radial velocity component (positive = away from center, negative = towards)
        system.velX[i] += dirX * radialVel;
        system.velY[i] += dirY * radialVel;
    }

    // Acceleration
    system.accelX[i] = randomRange(cfg.accelerationMinX, cfg.accelerationMaxX);
    system.accelY[i] = randomRange(cfg.accelerationMinY, cfg.accelerationMaxY);
    system.radialAccel[i] = randomRange(cfg.radialAccelerationMin, cfg.radialAccelerationMax);

    // Size
    system.startSize[i] = randomRange(cfg.startSizeMin, cfg.startSizeMax);
    system.endSize[i] = randomRange(cfg.endSizeMin, cfg.endSizeMax);
    system.size[i] = system.startSize[i];

    // Color
    system.colorR[i] = randomRange(cfg.colorMinR, cfg.colorMaxR);
    system.colorG[i] = randomRange(cfg.colorMinG, cfg.colorMaxG);
    system.colorB[i] = randomRange(cfg.colorMinB, cfg.colorMaxB);
    system.colorA[i] = randomRange(cfg.colorMinA, cfg.colorMaxA);

    // End color
    system.endColorR[i] = randomRange(cfg.endColorMinR, cfg.endColorMaxR);
    system.endColorG[i] = randomRange(cfg.endColorMinG, cfg.endColorMaxG);
    system.endColorB[i] = randomRange(cfg.endColorMinB, cfg.endColorMaxB);
    system.endColorA[i] = randomRange(cfg.endColorMinA, cfg.endColorMaxA);

    // Lifetime
    float lt = randomRange(cfg.lifetimeMin, cfg.lifetimeMax);
    system.lifetime[i] = lt;
    system.totalLifetime[i] = lt;

    // Rotation
    system.rotX[i] = randomRange(cfg.rotationMinX, cfg.rotationMaxX);
    system.rotY[i] = randomRange(cfg.rotationMinY, cfg.rotationMaxY);
    system.rotZ[i] = randomRange(cfg.rotationMinZ, cfg.rotationMaxZ);

    // Rotational velocity
    system.rotVelX[i] = randomRange(cfg.rotVelocityMinX, cfg.rotVelocityMaxX);
    system.rotVelY[i] = randomRange(cfg.rotVelocityMinY, cfg.rotVelocityMaxY);
    system.rotVelZ[i] = randomRange(cfg.rotVelocityMinZ, cfg.rotVelocityMaxZ);

    // Rotational acceleration
    system.rotAccelX[i] = randomRange(cfg.rotAccelerationMinX, cfg.rotAccelerationMaxX);
    system.rotAccelY[i] = randomRange(cfg.rotAccelerationMinY, cfg.rotAccelerationMaxY);
    system.rotAccelZ[i] = randomRange(cfg.rotAccelerationMinZ, cfg.rotAccelerationMaxZ);

    // Texture selection
    if (cfg.textureCount > 0) {
        system.textureIndex[i] = (int)(fastRandomFloat() * (float)cfg.textureCount);
        if (system.textureIndex[i] >= cfg.textureCount) {
            system.textureIndex[i] = cfg.textureCount - 1;
        }
    } else {
        system.textureIndex[i] = 0;
    }

    system.liveParticleCount++;
}

bool ParticleSystemManager::updateParticle(ParticleSystem& system, int i, float dt) {
    // Update lifetime
    system.lifetime[i] -= dt;
    if (system.lifetime[i] <= 0.0f) {
        return false;  // Particle is dead
    }

    // Calculate life ratio (0 = just born, 1 = about to die)
    float lifeRatio = 1.0f - (system.lifetime[i] / system.totalLifetime[i]);

    // Apply radial acceleration (towards emission center)
    float emitterWorldCenterX = system.emitterX + system.emissionCenterX;
    float emitterWorldCenterY = system.emitterY + system.emissionCenterY;
    float dx = emitterWorldCenterX - system.posX[i];
    float dy = emitterWorldCenterY - system.posY[i];
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist > 0.001f) {
        float radialX = (dx / dist) * system.radialAccel[i];
        float radialY = (dy / dist) * system.radialAccel[i];
        system.velX[i] += radialX * dt;
        system.velY[i] += radialY * dt;
    }

    // Apply linear acceleration
    system.velX[i] += system.accelX[i] * dt;
    system.velY[i] += system.accelY[i] * dt;

    // Update position
    system.posX[i] += system.velX[i] * dt;
    system.posY[i] += system.velY[i] * dt;

    // Interpolate size
    system.size[i] = system.startSize[i] + (system.endSize[i] - system.startSize[i]) * lifeRatio;

    // Interpolate color
    // Note: Color interpolation is handled in SceneManager during vertex generation
    // colorR/G/B/A store the START color, endColorR/G/B/A store the END color
    // The actual interpolation uses lifeRatio and is done per-frame in the renderer

    // Apply rotational acceleration
    system.rotVelX[i] += system.rotAccelX[i] * dt;
    system.rotVelY[i] += system.rotAccelY[i] * dt;
    system.rotVelZ[i] += system.rotAccelZ[i] * dt;

    // Update rotation
    system.rotX[i] += system.rotVelX[i] * dt;
    system.rotY[i] += system.rotVelY[i] * dt;
    system.rotZ[i] += system.rotVelZ[i] * dt;

    return true;  // Particle is still alive
}

void ParticleSystemManager::removeParticle(ParticleSystem& system, int index) {
    int last = system.liveParticleCount - 1;
    if (index < last) {
        // Swap with last particle
        system.posX[index] = system.posX[last];
        system.posY[index] = system.posY[last];
        system.velX[index] = system.velX[last];
        system.velY[index] = system.velY[last];
        system.accelX[index] = system.accelX[last];
        system.accelY[index] = system.accelY[last];
        system.radialAccel[index] = system.radialAccel[last];

        system.size[index] = system.size[last];
        system.startSize[index] = system.startSize[last];
        system.endSize[index] = system.endSize[last];

        system.colorR[index] = system.colorR[last];
        system.colorG[index] = system.colorG[last];
        system.colorB[index] = system.colorB[last];
        system.colorA[index] = system.colorA[last];

        system.endColorR[index] = system.endColorR[last];
        system.endColorG[index] = system.endColorG[last];
        system.endColorB[index] = system.endColorB[last];
        system.endColorA[index] = system.endColorA[last];

        system.lifetime[index] = system.lifetime[last];
        system.totalLifetime[index] = system.totalLifetime[last];

        system.rotX[index] = system.rotX[last];
        system.rotY[index] = system.rotY[last];
        system.rotZ[index] = system.rotZ[last];

        system.rotVelX[index] = system.rotVelX[last];
        system.rotVelY[index] = system.rotVelY[last];
        system.rotVelZ[index] = system.rotVelZ[last];

        system.rotAccelX[index] = system.rotAccelX[last];
        system.rotAccelY[index] = system.rotAccelY[last];
        system.rotAccelZ[index] = system.rotAccelZ[last];

        system.textureIndex[index] = system.textureIndex[last];
    }
    system.liveParticleCount--;
}

void ParticleSystemManager::update(float deltaTime) {
    for (int s = 0; s < systemCount_; ++s) {
        ParticleSystem& system = systems_[s];

        // Update emission accumulator
        system.emissionAccumulator += system.config.emissionRate * deltaTime;

        // Spawn new particles
        while (system.emissionAccumulator >= 1.0f) {
            spawnParticle(system);
            system.emissionAccumulator -= 1.0f;
        }

        // Update existing particles
        for (int i = 0; i < system.liveParticleCount; ) {
            if (!updateParticle(system, i, deltaTime)) {
                removeParticle(system, i);
                // Don't increment i - we need to check the swapped particle
            } else {
                ++i;
            }
        }
    }
}

void ParticleSystemManager::clearAllSystems() {
    for (int i = 0; i < systemCount_; ++i) {
        freeParticleArrays(systems_[i]);
    }
    systemCount_ = 0;
}
