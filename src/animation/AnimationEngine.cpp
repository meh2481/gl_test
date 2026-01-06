#include "AnimationEngine.h"
#include "../scene/SceneLayer.h"
#include "../debug/ConsoleBuffer.h"
#include <cassert>

AnimationEngine::AnimationEngine(MemoryAllocator* allocator, SceneLayerManager* layerManager, ConsoleBuffer* consoleBuffer)
    : allocator_(allocator)
    , layerManager_(layerManager)
    , consoleBuffer_(consoleBuffer)
    , animations_(*allocator, "AnimationEngine::animations_")
    , nextAnimationId_(1) {
    assert(allocator != nullptr);
    assert(layerManager != nullptr);
    assert(consoleBuffer != nullptr);
    consoleBuffer_->log(SDL_LOG_PRIORITY_VERBOSE, "AnimationEngine: Created");
}

AnimationEngine::~AnimationEngine() {
    clear();
    consoleBuffer_->log(SDL_LOG_PRIORITY_VERBOSE, "AnimationEngine: Destroyed");
}

int AnimationEngine::startAnimation(int targetId, AnimationPropertyType propertyType,
                                    InterpolationType interpolationType,
                                    const float* startValues, const float* endValues,
                                    int valueCount, float duration) {
    assert(startValues != nullptr);
    assert(endValues != nullptr);
    assert(valueCount > 0 && valueCount <= 8);
    assert(duration > 0.0f);

    Animation* anim = static_cast<Animation*>(allocator_->allocate(sizeof(Animation), "AnimationEngine::startAnimation"));
    assert(anim != nullptr);
    new (anim) Animation();

    anim->targetId = targetId;
    anim->propertyType = propertyType;
    anim->interpolationType = interpolationType;
    anim->elapsedTime = 0.0f;
    anim->duration = duration;
    anim->valueCount = valueCount;

    for (int i = 0; i < valueCount; ++i) {
        anim->startValues[i] = startValues[i];
        anim->endValues[i] = endValues[i];
    }

    int animationId = nextAnimationId_++;
    animations_.insert(animationId, anim);

    consoleBuffer_->log(SDL_LOG_PRIORITY_VERBOSE,
                 "AnimationEngine: Started animation %d for target %d, property %d, duration %.2f",
                 animationId, targetId, propertyType, duration);

    return animationId;
}

int AnimationEngine::startSplineAnimation(int targetId, AnimationPropertyType propertyType,
                                          const float* p0, const float* p1,
                                          const float* p2, const float* p3,
                                          int valueCount, float duration) {
    assert(p0 != nullptr && p1 != nullptr && p2 != nullptr && p3 != nullptr);
    assert(valueCount > 0 && valueCount <= 8);
    assert(duration > 0.0f);

    Animation* anim = static_cast<Animation*>(allocator_->allocate(sizeof(Animation), "AnimationEngine::startSplineAnimation"));
    assert(anim != nullptr);
    new (anim) Animation();

    anim->targetId = targetId;
    anim->propertyType = propertyType;
    anim->interpolationType = INTERPOLATION_CATMULL_ROM;
    anim->elapsedTime = 0.0f;
    anim->duration = duration;
    anim->valueCount = valueCount;

    for (int i = 0; i < valueCount; ++i) {
        anim->startValues[i] = p1[i];  // Actual start is p1
        anim->endValues[i] = p2[i];    // Actual end is p2
        // Store all 4 control points for Catmull-Rom
        anim->controlPoints[i * 2] = p0[i];
        anim->controlPoints[i * 2 + 1] = p3[i];
    }

    int animationId = nextAnimationId_++;
    animations_.insert(animationId, anim);

    consoleBuffer_->log(SDL_LOG_PRIORITY_VERBOSE,
                 "AnimationEngine: Started spline animation %d for target %d, property %d, duration %.2f",
                 animationId, targetId, propertyType, duration);

    return animationId;
}

void AnimationEngine::stopAnimation(int animationId) {
    if (animations_.contains(animationId)) {
        Animation** animPtr = animations_.find(animationId);
        assert(animPtr != nullptr);
        Animation* anim = *animPtr;
        assert(anim != nullptr);

        anim->~Animation();
        allocator_->free(anim);

        animations_.remove(animationId);
        consoleBuffer_->log(SDL_LOG_PRIORITY_VERBOSE,
                     "AnimationEngine: Stopped animation %d", animationId);
    }
}

void AnimationEngine::stopAnimationsForTarget(int targetId, AnimationPropertyType propertyType) {
    Vector<int> toRemove(*allocator_, "AnimationEngine::stopAnimationsForTarget");

    for (auto it = animations_.begin(); it != animations_.end(); ++it) {
        Animation* anim = it.value();
        assert(anim != nullptr);
        if (anim->targetId == targetId && anim->propertyType == propertyType) {
            toRemove.push_back(it.key());
        }
    }

    for (size_t i = 0; i < toRemove.size(); ++i) {
        int animId = toRemove[i];
        Animation** animPtr = animations_.find(animId);
        assert(animPtr != nullptr);
        Animation* anim = *animPtr;
        assert(anim != nullptr);

        anim->~Animation();
        allocator_->free(anim);

        animations_.remove(animId);
    }

    if (toRemove.size() > 0) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_VERBOSE,
                     "AnimationEngine: Stopped %zu animations for target %d, property %d",
                     toRemove.size(), targetId, propertyType);
    }
}

void AnimationEngine::update(float deltaTime) {
    if (animations_.empty()) {
        return;
    }

    Vector<int> completedAnimations(*allocator_, "AnimationEngine::completedAnimations");

    for (auto it = animations_.begin(); it != animations_.end(); ++it) {
        Animation* anim = it.value();
        assert(anim != nullptr);
        anim->elapsedTime += deltaTime;

        float t = anim->elapsedTime / anim->duration;

        if (t >= 1.0f) {
            // Animation complete - apply final values
            t = 1.0f;
            applyAnimation(*anim, t);
            completedAnimations.push_back(it.key());
        } else {
            // Animation in progress
            applyAnimation(*anim, t);
        }
    }

    // Remove completed animations
    for (size_t i = 0; i < completedAnimations.size(); ++i) {
        int animId = completedAnimations[i];
        consoleBuffer_->log(SDL_LOG_PRIORITY_VERBOSE,
                     "AnimationEngine: Animation %d completed", animId);

        Animation** animPtr = animations_.find(animId);
        assert(animPtr != nullptr);
        Animation* anim = *animPtr;
        assert(anim != nullptr);

        anim->~Animation();
        allocator_->free(anim);

        animations_.remove(animId);
    }
}

void AnimationEngine::clear() {
    int count = animations_.size();

    // Clean up all allocated animations
    for (auto it = animations_.begin(); it != animations_.end(); ++it) {
        Animation* anim = it.value();
        assert(anim != nullptr);
        anim->~Animation();
        allocator_->free(anim);
    }

    animations_.clear();

    if (count > 0) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_VERBOSE,
                     "AnimationEngine: Cleared %d animations", count);
    }
}

float AnimationEngine::interpolate(float t, InterpolationType type) const {
    assert(t >= 0.0f && t <= 1.0f);

    switch (type) {
        case INTERPOLATION_LINEAR:
            return t;

        case INTERPOLATION_EASE_IN:
            // Cubic ease-in: t^3
            return t * t * t;

        case INTERPOLATION_EASE_OUT:
            // Cubic ease-out: 1 - (1-t)^3
            {
                float oneMinusT = 1.0f - t;
                return 1.0f - (oneMinusT * oneMinusT * oneMinusT);
            }

        case INTERPOLATION_EASE_IN_OUT:
            // Cubic ease-in-out
            if (t < 0.5f) {
                return 4.0f * t * t * t;
            } else {
                float f = ((2.0f * t) - 2.0f);
                return 0.5f * f * f * f + 1.0f;
            }

        case INTERPOLATION_SMOOTH_STEP:
            // Smoothstep: 3t^2 - 2t^3
            return t * t * (3.0f - 2.0f * t);

        case INTERPOLATION_CATMULL_ROM:
            // Not used directly; Catmull-Rom is handled separately
            return t;

        default:
            assert(false);
            return t;
    }
}

float AnimationEngine::catmullRomInterpolate(float t, float p0, float p1, float p2, float p3) const {
    assert(t >= 0.0f && t <= 1.0f);

    // Catmull-Rom spline matrix
    float t2 = t * t;
    float t3 = t2 * t;

    // Standard Catmull-Rom formula with tau = 0.5
    float v0 = -0.5f * t3 + t2 - 0.5f * t;
    float v1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
    float v2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
    float v3 = 0.5f * t3 - 0.5f * t2;

    return p0 * v0 + p1 * v1 + p2 * v2 + p3 * v3;
}

void AnimationEngine::applyAnimation(const Animation& anim, float t) {
    assert(t >= 0.0f && t <= 1.0f);

    float interpolatedValues[8];

    if (anim.interpolationType == INTERPOLATION_CATMULL_ROM) {
        // Use Catmull-Rom spline interpolation
        for (int i = 0; i < anim.valueCount; ++i) {
            float p0 = anim.controlPoints[i * 2];
            float p1 = anim.startValues[i];
            float p2 = anim.endValues[i];
            float p3 = anim.controlPoints[i * 2 + 1];
            interpolatedValues[i] = catmullRomInterpolate(t, p0, p1, p2, p3);
        }
    } else {
        // Use standard interpolation
        float interpolatedT = interpolate(t, anim.interpolationType);
        for (int i = 0; i < anim.valueCount; ++i) {
            interpolatedValues[i] = anim.startValues[i] +
                                   (anim.endValues[i] - anim.startValues[i]) * interpolatedT;
        }
    }

    // Apply the interpolated values to the target
    switch (anim.propertyType) {
        case PROPERTY_LAYER_SCALE:
            assert(anim.valueCount >= 2);
            layerManager_->setLayerScale(anim.targetId, interpolatedValues[0], interpolatedValues[1]);
            break;

        case PROPERTY_LAYER_POSITION:
            assert(anim.valueCount >= 2);
            layerManager_->setLayerPosition(anim.targetId, interpolatedValues[0], interpolatedValues[1]);
            break;

        case PROPERTY_LAYER_ROTATION:
            assert(anim.valueCount >= 1);
            layerManager_->setLayerPosition(anim.targetId, 0.0f, 0.0f, interpolatedValues[0]);
            break;

        case PROPERTY_LAYER_COLOR:
            assert(anim.valueCount >= 4);
            layerManager_->setLayerColor(anim.targetId,
                                        interpolatedValues[0], interpolatedValues[1],
                                        interpolatedValues[2], interpolatedValues[3]);
            break;

        case PROPERTY_LAYER_OFFSET:
            assert(anim.valueCount >= 2);
            layerManager_->setLayerOffset(anim.targetId, interpolatedValues[0], interpolatedValues[1]);
            break;

        default:
            assert(false);
            break;
    }
}
