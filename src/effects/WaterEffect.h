#pragma once

#include <cstdint>

// Maximum number of active ripples (must match MAX_SHADER_RIPPLES in VulkanPipeline.h)
static const int MAX_WATER_RIPPLES = 4;

// Maximum number of water force fields
static const int MAX_WATER_FORCE_FIELDS = 16;

// Maximum number of tracked bodies per water field
static const int MAX_TRACKED_BODIES = 64;

// Ripple data for GPU
struct WaterRipple {
    float x, y;        // Position of ripple center
    float time;        // Time since ripple started
    float amplitude;   // Initial amplitude
    bool particlesCreated;  // Flag to prevent duplicate particle creation
};

// Maximum vertices per water polygon
static const int MAX_WATER_POLYGON_VERTICES = 8;

// Water force field visual configuration
struct WaterForceFieldConfig {
    float vertices[MAX_WATER_POLYGON_VERTICES * 2];  // Polygon vertices (x,y pairs)
    int vertexCount;                // Number of vertices in polygon
    float rotation;                 // Rotation angle in radians
    float alpha;                    // Base transparency (0-1)
    float rippleAmplitude;          // Amplitude of ambient ripples
    float rippleSpeed;              // Speed of ambient ripples
    float surfaceY;                 // Y position of water surface (calculated from percentageFull)
    float percentageFull;           // Percentage full (0.0-1.0, 1.0 = 100% full at maxY, 0.0 = empty at minY)
    float centerX, centerY;         // Center point of the polygon (for rotation)
    float minX, minY, maxX, maxY;   // Cached bounding box (updated when vertices/rotation change)
};

// Water force field with visual effect
struct WaterForceField {
    int waterFieldId;               // Water effect ID (for lookups)
    int forceFieldId;               // Physics force field ID
    int surfaceCollisionBodyId;     // Collision body at water surface for fire/water interactions
    WaterForceFieldConfig config;

    // Ripple state
    WaterRipple ripples[MAX_WATER_RIPPLES];
    int rippleCount;

    // Tracked bodies for splash detection
    int trackedBodies[MAX_TRACKED_BODIES];
    float trackedBodyLastY[MAX_TRACKED_BODIES];
    int trackedBodyCount;

    bool active;
};

// Manages water visual effects for force fields
class WaterEffectManager {
public:
    WaterEffectManager();
    ~WaterEffectManager();

    // Create a water force field with visual effect
    // Returns the water force field ID (different from physics force field ID)
    // vertices: array of x,y pairs defining the polygon (3-8 vertices)
    // percentageFull: 0.0-1.0, where 1.0 = full (surface at maxY), 0.0 = empty (surface at minY)
    int createWaterForceField(int physicsForceFieldId,
                               const float* vertices, int vertexCount,
                               float alpha, float rippleAmplitude, float rippleSpeed,
                               float percentageFull = 1.0f);

    // Destroy a water force field
    void destroyWaterForceField(int waterFieldId);

    // Update ripples and splash detection
    void update(float deltaTime);

    // Add a splash ripple at position
    void addSplash(int waterFieldId, float x, float y, float amplitude);

    // Track body entering/exiting water for splash detection
    void onBodyEnterWater(int waterFieldId, int bodyId, float x, float y, float velocity);
    void onBodyExitWater(int waterFieldId, int bodyId, float x, float y, float velocity);

    // Update tracked body position
    // Returns true if body crossed the water surface (for triggering splash-related events)
    bool updateTrackedBody(int waterFieldId, int bodyId, float x, float y);

    // Set water percentage (0.0-1.0, where 1.0 = 100% full)
    void setWaterPercentage(int waterFieldId, float percentage);

    // Set rotation angle for water force field (in radians)
    void setWaterRotation(int waterFieldId, float rotation);

    // Get water force field data for rendering
    const WaterForceField* getWaterForceField(int waterFieldId) const;

    // Get all active water force fields
    int getActiveFieldCount() const { return activeFieldCount_; }
    const WaterForceField* getFields() const { return fields_; }

    // Check if a body is inside a water force field
    bool isBodyInWater(int bodyId, int* outWaterFieldId = nullptr) const;

    // Clear all water force fields
    void clear();

    // Find water field by physics force field ID
    int findByPhysicsForceField(int physicsForceFieldId) const;

private:
    // Update bounding box and center point from polygon vertices and rotation
    void updateBoundingBox(WaterForceFieldConfig& config);
    WaterForceField fields_[MAX_WATER_FORCE_FIELDS];
    int activeFieldCount_;
    int nextFieldId_;
};
