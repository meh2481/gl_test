#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>

// Sprite vertex structure with position and texture coordinates
struct SpriteVertex {
    float x, y;        // Position
    float u, v;        // Texture coordinates
};

// Scene layer that can be attached to a physics body
struct SceneLayer {
    uint64_t textureId;      // Resource ID of the texture
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
    int createLayer(uint64_t textureId, float width, float height);
    void destroyLayer(int layerId);
    void attachLayerToBody(int layerId, int physicsBodyId);
    void detachLayer(int layerId);
    void setLayerOffset(int layerId, float offsetX, float offsetY);
    void setLayerEnabled(int layerId, bool enabled);
    
    // Get all active layers
    const std::unordered_map<int, SceneLayer>& getLayers() const { return layers_; }
    
    // Generate vertex data for all layers based on physics body positions
    // bodyPositionsX/Y are parallel arrays indexed by bodyId
    // bodyAngles is indexed by bodyId
    void updateLayerVertices(
        std::vector<SpriteVertex>& vertices,
        std::vector<uint16_t>& indices
    );
    
    // Update a single layer's transform based on physics body
    void updateLayerTransform(int layerId, float bodyX, float bodyY, float bodyAngle);

private:
    std::unordered_map<int, SceneLayer> layers_;
    int nextLayerId_;
};
