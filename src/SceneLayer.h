#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>

// Sprite vertex structure with position and texture coordinates
struct SpriteVertex {
    float x, y;        // Position
    float u, v;        // Texture coordinates
};

// Sprite batch for a single texture
struct SpriteBatch {
    uint64_t textureId;
    uint64_t normalMapId;    // Normal map texture ID (0 if none)
    uint64_t descriptorId;   // Descriptor set ID to use for this batch
    int pipelineId;          // Pipeline ID to use for this batch
    std::vector<SpriteVertex> vertices;
    std::vector<uint16_t> indices;
};

// Scene layer that can be attached to a physics body
struct SceneLayer {
    uint64_t textureId;      // Resource ID of the primary texture
    uint64_t normalMapId;    // Resource ID of normal map (0 if none)
    uint64_t descriptorId;   // Descriptor set ID to use for rendering
    int pipelineId;          // Pipeline ID to use for rendering
    int physicsBodyId;       // Physics body this layer is attached to (-1 if not attached)
    float width;             // Width of sprite in world units
    float height;            // Height of sprite in world units
    float offsetX;           // Offset from body center
    float offsetY;           // Offset from body center
    bool enabled;            // Whether this layer is visible
    
    // Cached transform from physics
    float cachedX;
    float cachedY;
    float cachedAngle;
};

// Manager for scene layers
class SceneLayerManager {
public:
    SceneLayerManager();
    ~SceneLayerManager();

    // Layer management
    int createLayer(uint64_t textureId, float width, float height, uint64_t normalMapId = 0, int pipelineId = -1);
    void destroyLayer(int layerId);
    void attachLayerToBody(int layerId, int physicsBodyId);
    void detachLayer(int layerId);
    void setLayerOffset(int layerId, float offsetX, float offsetY);
    void setLayerEnabled(int layerId, bool enabled);
    
    // Get all active layers
    const std::unordered_map<int, SceneLayer>& getLayers() const { return layers_; }
    
    // Generate vertex data for all layers based on physics body positions
    // Groups sprites by texture for efficient batch rendering
    void updateLayerVertices(std::vector<SpriteBatch>& batches);
    
    // Update a single layer's transform based on physics body
    void updateLayerTransform(int layerId, float bodyX, float bodyY, float bodyAngle);

private:
    std::unordered_map<int, SceneLayer> layers_;
    int nextLayerId_;
};
