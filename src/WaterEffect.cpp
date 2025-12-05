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

        // Update ripple times
        for (int j = 0; j < field.rippleCount; ++j) {
            field.ripples[j].time += deltaTime;

            // Mark expired ripples with zero amplitude so they can be reused
            if (field.ripples[j].time >= 3.0f) {
                field.ripples[j].amplitude = 0.0f;
            }
        }

        // Compact the ripple array by removing expired ripples from the end
        while (field.rippleCount > 0 && field.ripples[field.rippleCount - 1].amplitude <= 0.0f) {
            --field.rippleCount;
        }
    }
}

void WaterEffectManager::addSplash(int waterFieldId, float x, float y, float amplitude) {
    for (int i = 0; i < MAX_WATER_FORCE_FIELDS; ++i) {
        if (!fields_[i].active || fields_[i].waterFieldId != waterFieldId) continue;

        WaterForceField& field = fields_[i];

        // Find a slot for the new ripple
        // First, try to use an expired/empty slot
        int targetSlot = -1;
        for (int j = 0; j < MAX_WATER_RIPPLES; ++j) {
            // Consider a slot empty if it has zero amplitude or time > 3.0
            if (j >= field.rippleCount || field.ripples[j].amplitude <= 0.0f || field.ripples[j].time > 3.0f) {
                targetSlot = j;
                break;
            }
        }

        // If no empty slot, replace the oldest ripple (highest time value)
        if (targetSlot < 0) {
            float maxTime = -1.0f;
            for (int j = 0; j < MAX_WATER_RIPPLES; ++j) {
                if (field.ripples[j].time > maxTime) {
                    maxTime = field.ripples[j].time;
                    targetSlot = j;
                }
            }
        }

        // Add the new ripple
        if (targetSlot >= 0) {
            field.ripples[targetSlot].x = x;
            field.ripples[targetSlot].y = y;
            field.ripples[targetSlot].time = 0.0f;
            field.ripples[targetSlot].amplitude = amplitude;

            // Update ripple count if needed
            if (targetSlot >= field.rippleCount) {
                field.rippleCount = targetSlot + 1;
            }
        }
        return;
    }
}

void WaterEffectManager::onBodyEnterWater(int waterFieldId, int bodyId, float x, float y, float velocity) {
    for (int i = 0; i < MAX_WATER_FORCE_FIELDS; ++i) {
        if (!fields_[i].active || fields_[i].waterFieldId != waterFieldId) continue;

        WaterForceField& field = fields_[i];

        // Add body to tracked list
        if (field.trackedBodyCount < MAX_TRACKED_BODIES) {
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
        float surfaceY = field.config.surfaceY;

        // Check if body is already tracked
        for (int j = 0; j < field.trackedBodyCount; ++j) {
            if (field.trackedBodies[j] == bodyId) {
                float lastY = field.trackedBodyLastY[j];

                // Check for surface crossing
                bool wasAboveSurface = lastY > surfaceY;
                bool isAboveSurface = y > surfaceY;

                if (wasAboveSurface != isAboveSurface) {
                    float velocity = (y - lastY) / PHYSICS_TIMESTEP;
                    float splashAmplitude = fabsf(velocity) * 0.15f;
                    if (splashAmplitude > 0.01f) {
                        // Clamp splash amplitude to reasonable range
                        if (splashAmplitude > 0.05f) splashAmplitude = 0.05f;
                        addSplash(waterFieldId, x, surfaceY, splashAmplitude);
                    }
                }

                field.trackedBodyLastY[j] = y;
                return;
            }
        }

        // Body not tracked yet - add it
        if (field.trackedBodyCount < MAX_TRACKED_BODIES) {
            field.trackedBodies[field.trackedBodyCount] = bodyId;
            field.trackedBodyLastY[field.trackedBodyCount] = y;
            ++field.trackedBodyCount;

            // If body is entering water (below surface), trigger a splash
            if (y < surfaceY) {
                addSplash(waterFieldId, x, surfaceY, 0.02f);
            }
        } else {
            // Remove oldest tracked body to make room for new one
            for (int k = 0; k < field.trackedBodyCount - 1; ++k) {
                field.trackedBodies[k] = field.trackedBodies[k + 1];
                field.trackedBodyLastY[k] = field.trackedBodyLastY[k + 1];
            }
            field.trackedBodies[field.trackedBodyCount - 1] = bodyId;
            field.trackedBodyLastY[field.trackedBodyCount - 1] = y;
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
