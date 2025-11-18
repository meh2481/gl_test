#include "SceneLayer.h"
#include <cmath>
#include <cassert>

SceneLayerManager::SceneLayerManager()
    : nextLayerId_(1) {
}

SceneLayerManager::~SceneLayerManager() {
}

int SceneLayerManager::createLayer(uint64_t textureId, float width, float height) {
    assert(width > 0.0f && height > 0.0f);
    
    int layerId = nextLayerId_++;
    SceneLayer layer;
    layer.textureId = textureId;
    layer.physicsBodyId = -1;  // Not attached initially
    layer.width = width;
    layer.height = height;
    layer.offsetX = 0.0f;
    layer.offsetY = 0.0f;
    layer.enabled = true;
    layer.cachedX = 0.0f;
    layer.cachedY = 0.0f;
    layer.cachedAngle = 0.0f;
    
    layers_[layerId] = layer;
    return layerId;
}

void SceneLayerManager::destroyLayer(int layerId) {
    layers_.erase(layerId);
}

void SceneLayerManager::attachLayerToBody(int layerId, int physicsBodyId) {
    auto it = layers_.find(layerId);
    if (it != layers_.end()) {
        it->second.physicsBodyId = physicsBodyId;
    }
}

void SceneLayerManager::detachLayer(int layerId) {
    auto it = layers_.find(layerId);
    if (it != layers_.end()) {
        it->second.physicsBodyId = -1;
    }
}

void SceneLayerManager::setLayerOffset(int layerId, float offsetX, float offsetY) {
    auto it = layers_.find(layerId);
    if (it != layers_.end()) {
        it->second.offsetX = offsetX;
        it->second.offsetY = offsetY;
    }
}

void SceneLayerManager::setLayerEnabled(int layerId, bool enabled) {
    auto it = layers_.find(layerId);
    if (it != layers_.end()) {
        it->second.enabled = enabled;
    }
}

void SceneLayerManager::updateLayerTransform(int layerId, float bodyX, float bodyY, float bodyAngle) {
    auto it = layers_.find(layerId);
    if (it != layers_.end()) {
        it->second.cachedX = bodyX;
        it->second.cachedY = bodyY;
        it->second.cachedAngle = bodyAngle;
    }
}

void SceneLayerManager::updateLayerVertices(
    std::vector<SpriteVertex>& vertices,
    std::vector<uint16_t>& indices
) {
    vertices.clear();
    indices.clear();
    
    uint16_t vertexIndex = 0;
    
    for (const auto& pair : layers_) {
        const SceneLayer& layer = pair.second;
        
        // Skip disabled layers or layers not attached to a body
        if (!layer.enabled || layer.physicsBodyId < 0) {
            continue;
        }
        
        float centerX = layer.cachedX;
        float centerY = layer.cachedY;
        float angle = layer.cachedAngle;
        
        // Calculate half-extents
        float hw = layer.width * 0.5f;
        float hh = layer.height * 0.5f;
        
        // Create quad vertices (4 vertices for a sprite)
        // Vertices in local space (before rotation)
        float localVerts[4][2] = {
            {-hw, -hh},  // Bottom-left
            { hw, -hh},  // Bottom-right
            { hw,  hh},  // Top-right
            {-hw,  hh}   // Top-left
        };
        
        // Texture coordinates
        float uvs[4][2] = {
            {0.0f, 1.0f},  // Bottom-left
            {1.0f, 1.0f},  // Bottom-right
            {1.0f, 0.0f},  // Top-right
            {0.0f, 0.0f}   // Top-left
        };
        
        // Apply rotation and position
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        
        for (int i = 0; i < 4; i++) {
            // Apply offset
            float lx = localVerts[i][0] + layer.offsetX;
            float ly = localVerts[i][1] + layer.offsetY;
            
            // Rotate
            float rx = lx * cosA - ly * sinA;
            float ry = lx * sinA + ly * cosA;
            
            // Translate to body position
            SpriteVertex vert;
            vert.x = centerX + rx;
            vert.y = centerY + ry;
            vert.u = uvs[i][0];
            vert.v = uvs[i][1];
            
            vertices.push_back(vert);
        }
        
        // Create indices for two triangles (quad)
        indices.push_back(vertexIndex + 0);
        indices.push_back(vertexIndex + 1);
        indices.push_back(vertexIndex + 2);
        
        indices.push_back(vertexIndex + 2);
        indices.push_back(vertexIndex + 3);
        indices.push_back(vertexIndex + 0);
        
        vertexIndex += 4;
    }
}
