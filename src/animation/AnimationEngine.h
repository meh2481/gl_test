#pragma once

#include "../core/Vector.h"
#include "../core/HashTable.h"
#include "../memory/MemoryAllocator.h"
#include <cstdint>

class SceneLayerManager;

// Interpolation types supported by the animation engine
enum InterpolationType {
    INTERPOLATION_LINEAR,        // Linear interpolation
    INTERPOLATION_EASE_IN,       // Cubic ease-in (slow start)
    INTERPOLATION_EASE_OUT,      // Cubic ease-out (slow end)
    INTERPOLATION_EASE_IN_OUT,   // Cubic ease-in-out (slow start and end)
    INTERPOLATION_SMOOTH_STEP,   // Smoothstep interpolation
    INTERPOLATION_CATMULL_ROM    // Catmull-Rom spline (requires 4 control points)
};

// Property types that can be animated
enum AnimationPropertyType {
    PROPERTY_LAYER_SCALE,        // Scale of a layer (2 floats: x, y)
    PROPERTY_LAYER_POSITION,     // Position of a layer (2 floats: x, y)
    PROPERTY_LAYER_ROTATION,     // Rotation of a layer (1 float: angle)
    PROPERTY_LAYER_COLOR,        // Color of a layer (4 floats: r, g, b, a)
    PROPERTY_LAYER_OFFSET        // Offset of a layer (2 floats: x, y)
};

// Animation definition
struct Animation {
    Animation() : targetId(-1), propertyType(PROPERTY_LAYER_SCALE), interpolationType(INTERPOLATION_LINEAR),
                  elapsedTime(0.0f), duration(0.0f), valueCount(0) {
        for (int i = 0; i < 8; ++i) {
            startValues[i] = 0.0f;
            endValues[i] = 0.0f;
            controlPoints[i] = 0.0f;
        }
    }

    int targetId;                       // Target object ID (e.g., layer ID)
    AnimationPropertyType propertyType; // Property being animated
    InterpolationType interpolationType; // Interpolation method
    float elapsedTime;                  // Current elapsed time
    float duration;                     // Total duration of animation
    float startValues[8];               // Starting values for the property (up to 8 floats for splines)
    float endValues[8];                 // Ending values for the property (up to 8 floats for splines)
    float controlPoints[8];             // Control points for spline interpolation
    int valueCount;                     // Number of values being animated (1-4 typically)
};

// Animation engine manages all active animations
class AnimationEngine {
public:
    AnimationEngine(MemoryAllocator* allocator, SceneLayerManager* layerManager);
    ~AnimationEngine();

    // Start a new animation and return its ID
    // Fire-and-forget: the animation will run automatically until complete
    int startAnimation(int targetId, AnimationPropertyType propertyType, InterpolationType interpolationType,
                       const float* startValues, const float* endValues, int valueCount, float duration);

    // Start a Catmull-Rom spline animation with control points
    int startSplineAnimation(int targetId, AnimationPropertyType propertyType,
                             const float* p0, const float* p1, const float* p2, const float* p3,
                             int valueCount, float duration);

    // Stop a specific animation by ID
    void stopAnimation(int animationId);

    // Stop all animations for a specific target
    void stopAnimationsForTarget(int targetId, AnimationPropertyType propertyType);

    // Update all active animations
    void update(float deltaTime);

    // Clear all animations (e.g., on scene cleanup)
    void clear();

    // Get active animation count for debugging
    int getActiveAnimationCount() const { return animations_.size(); }

private:
    // Apply interpolation function
    float interpolate(float t, InterpolationType type) const;

    // Apply animation to target
    void applyAnimation(const Animation& anim, float t);

    // Catmull-Rom spline interpolation for a single value
    float catmullRomInterpolate(float t, float p0, float p1, float p2, float p3) const;

    MemoryAllocator* allocator_;
    SceneLayerManager* layerManager_;
    HashTable<int, Animation> animations_;  // animationId -> Animation
    int nextAnimationId_;
};
