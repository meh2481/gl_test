#include "WaterEffect.h"
#include <cstring>
#include <cassert>
#include <cmath>
#include <iostream>

static const float PHYSICS_TIMESTEP = 1.0f / 60.0f;

WaterEffectManager::WaterEffectManager() : activeFieldCount_(0), nextFieldId_(1) {
    memset(fields_, 0, sizeof(fields_));
    for (int i = 0; i < MAX_WATER_FORCE_FIELDS; ++i) {
        fields_[i].active = false;
        fields_[i].waterFieldId = -1;
    }
}

WaterEffectManager::~WaterEffectManager() {
}

int WaterEffectManager::createWaterForceField(int physicsForceFieldId,
                                               float minX, float minY, float maxX, float maxY,
                                               float alpha, float rippleAmplitude, float rippleSpeed) {
    // Find an empty slot
    int slot = -1;
    for (int i = 0; i < MAX_WATER_FORCE_FIELDS; ++i) {
        if (!fields_[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        std::cerr << "WaterEffectManager: Maximum water force fields reached" << std::endl;
        return -1;
    }

    int waterFieldId = nextFieldId_++;

    WaterForceField& field = fields_[slot];
    memset(&field, 0, sizeof(field));

    field.waterFieldId = waterFieldId;
    field.forceFieldId = physicsForceFieldId;
    field.config.minX = minX;
    field.config.minY = minY;
    field.config.maxX = maxX;
    field.config.maxY = maxY;
    field.config.alpha = alpha;
    field.config.rippleAmplitude = rippleAmplitude;
    field.config.rippleSpeed = rippleSpeed;
    field.config.surfaceY = maxY;
    field.rippleCount = 0;
    field.trackedBodyCount = 0;
    field.active = true;

    ++activeFieldCount_;

    std::cout << "Created water force field " << waterFieldId << " at slot " << slot << std::endl;

    return waterFieldId;
}

void WaterEffectManager::destroyWaterForceField(int waterFieldId) {
    if (waterFieldId < 0) {
        return;
    }

    for (int i = 0; i < MAX_WATER_FORCE_FIELDS; ++i) {
        if (fields_[i].active && fields_[i].waterFieldId == waterFieldId) {
            fields_[i].active = false;
            fields_[i].waterFieldId = -1;
            --activeFieldCount_;
            return;
        }
    }
}

void WaterEffectManager::update(float deltaTime) {
    for (int i = 0; i < MAX_WATER_FORCE_FIELDS; ++i) {
        if (!fields_[i].active) continue;

        WaterForceField& field = fields_[i];

        // Update ripple times and remove expired ripples
        int writeIdx = 0;
        for (int j = 0; j < field.rippleCount; ++j) {
            field.ripples[j].time += deltaTime;

            // Keep ripple if it's still visible (decay over ~3 seconds)
            if (field.ripples[j].time < 3.0f) {
                if (writeIdx != j) {
                    field.ripples[writeIdx] = field.ripples[j];
                }
                ++writeIdx;
            }
        }
        field.rippleCount = writeIdx;
    }
}

void WaterEffectManager::addSplash(int waterFieldId, float x, float y, float amplitude) {
    for (int i = 0; i < MAX_WATER_FORCE_FIELDS; ++i) {
        if (!fields_[i].active || fields_[i].waterFieldId != waterFieldId) continue;

        WaterForceField& field = fields_[i];

        if (field.rippleCount < MAX_WATER_RIPPLES) {
            WaterRipple& ripple = field.ripples[field.rippleCount];
            ripple.x = x;
            ripple.y = y;
            ripple.time = 0.0f;
            ripple.amplitude = amplitude;
            ++field.rippleCount;
        }
        return;
    }
}

void WaterEffectManager::onBodyEnterWater(int waterFieldId, int bodyId, float x, float y, float velocity) {
    for (int i = 0; i < MAX_WATER_FORCE_FIELDS; ++i) {
        if (!fields_[i].active || fields_[i].waterFieldId != waterFieldId) continue;

        WaterForceField& field = fields_[i];

        // Add body to tracked list
        if (field.trackedBodyCount < 64) {
            field.trackedBodies[field.trackedBodyCount] = bodyId;
            field.trackedBodyLastY[field.trackedBodyCount] = y;
            ++field.trackedBodyCount;
        }

        // Add splash based on entry velocity
        float splashAmplitude = fabsf(velocity) * 0.1f;
        if (splashAmplitude > 0.01f) {
            addSplash(waterFieldId, x, field.config.surfaceY, splashAmplitude);
        }
        return;
    }
}

void WaterEffectManager::onBodyExitWater(int waterFieldId, int bodyId, float x, float y, float velocity) {
    for (int i = 0; i < MAX_WATER_FORCE_FIELDS; ++i) {
        if (!fields_[i].active || fields_[i].waterFieldId != waterFieldId) continue;

        WaterForceField& field = fields_[i];

        // Remove body from tracked list
        for (int j = 0; j < field.trackedBodyCount; ++j) {
            if (field.trackedBodies[j] == bodyId) {
                // Swap with last element
                field.trackedBodies[j] = field.trackedBodies[field.trackedBodyCount - 1];
                field.trackedBodyLastY[j] = field.trackedBodyLastY[field.trackedBodyCount - 1];
                --field.trackedBodyCount;
                break;
            }
        }

        // Add splash based on exit velocity
        float splashAmplitude = fabsf(velocity) * 0.08f;
        if (splashAmplitude > 0.01f) {
            addSplash(waterFieldId, x, field.config.surfaceY, splashAmplitude);
        }
        return;
    }
}

void WaterEffectManager::updateTrackedBody(int waterFieldId, int bodyId, float x, float y) {
    for (int i = 0; i < MAX_WATER_FORCE_FIELDS; ++i) {
        if (!fields_[i].active || fields_[i].waterFieldId != waterFieldId) continue;

        WaterForceField& field = fields_[i];

        for (int j = 0; j < field.trackedBodyCount; ++j) {
            if (field.trackedBodies[j] == bodyId) {
                float lastY = field.trackedBodyLastY[j];
                float surfaceY = field.config.surfaceY;

                // Check for surface crossing
                bool wasAboveSurface = lastY > surfaceY;
                bool isAboveSurface = y > surfaceY;

                if (wasAboveSurface != isAboveSurface) {
                    float velocity = (y - lastY) / PHYSICS_TIMESTEP;
                    float splashAmplitude = fabsf(velocity) * 0.1f;
                    if (splashAmplitude > 0.02f) {
                        addSplash(waterFieldId, x, surfaceY, splashAmplitude);
                    }
                }

                field.trackedBodyLastY[j] = y;
                return;
            }
        }
        return;
    }
}

const WaterForceField* WaterEffectManager::getWaterForceField(int waterFieldId) const {
    for (int i = 0; i < MAX_WATER_FORCE_FIELDS; ++i) {
        if (fields_[i].active && fields_[i].waterFieldId == waterFieldId) {
            return &fields_[i];
        }
    }
    return nullptr;
}

bool WaterEffectManager::isBodyInWater(int bodyId, int* outWaterFieldId) const {
    for (int i = 0; i < MAX_WATER_FORCE_FIELDS; ++i) {
        if (!fields_[i].active) continue;

        const WaterForceField& field = fields_[i];
        for (int j = 0; j < field.trackedBodyCount; ++j) {
            if (field.trackedBodies[j] == bodyId) {
                if (outWaterFieldId) {
                    *outWaterFieldId = field.waterFieldId;
                }
                return true;
            }
        }
    }
    return false;
}

int WaterEffectManager::findByPhysicsForceField(int physicsForceFieldId) const {
    for (int i = 0; i < MAX_WATER_FORCE_FIELDS; ++i) {
        if (fields_[i].active && fields_[i].forceFieldId == physicsForceFieldId) {
            return fields_[i].waterFieldId;
        }
    }
    return -1;
}

void WaterEffectManager::clear() {
    for (int i = 0; i < MAX_WATER_FORCE_FIELDS; ++i) {
        fields_[i].active = false;
        fields_[i].waterFieldId = -1;
    }
    activeFieldCount_ = 0;
}
