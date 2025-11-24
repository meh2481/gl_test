#include "SceneLayer.h"
#include <cmath>
#include <cassert>
#include <algorithm>

// Hash function for std::pair to use in unordered_map
struct PairHash {
    template <typename T1, typename T2>
    std::size_t operator()(const std::pair<T1, T2>& p) const {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        // Combine hashes using a standard technique
        return h1 ^ (h2 << 1);
    }
};

SceneLayerManager::SceneLayerManager()
    : nextLayerId_(1) {
}

SceneLayerManager::~SceneLayerManager() {
}

int SceneLayerManager::createLayer(uint64_t textureId, float width, float height, uint64_t normalMapId, int pipelineId) {
    assert(width > 0.0f && height > 0.0f);
    
    int layerId = nextLayerId_++;
    SceneLayer layer;
    layer.textureId = textureId;
    layer.normalMapId = normalMapId;
    layer.pipelineId = pipelineId;
    
    // Compute descriptor ID based on textures used
    if (normalMapId != 0) {
        // Dual texture - combine texture IDs for unique descriptor ID
        layer.descriptorId = textureId ^ (normalMapId << 1);
    } else {
        // Single texture - use texture ID directly
        layer.descriptorId = textureId;
    }
    
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

void SceneLayerManager::updateLayerVertices(std::vector<SpriteBatch>& batches) {
    batches.clear();
    
    // Group layers by pipeline ID first, then by descriptor ID
    // Use a pair<pipelineId, descriptorId> as the batch map key
    // Map to batch index instead of pointer to avoid invalidation during sorting
    std::unordered_map<std::pair<int, uint64_t>, size_t, PairHash> batchMap;
    
    for (const auto& pair : layers_) {
        const SceneLayer& layer = pair.second;
        
        // Skip disabled layers or layers not attached to a body
        if (!layer.enabled || layer.physicsBodyId < 0) {
            continue;
        }
        
        // Create batch key from pipeline ID and descriptor ID
        auto batchKey = std::make_pair(layer.pipelineId, layer.descriptorId);
        
        // Find or create batch for this key
        size_t batchIndex;
        auto batchIt = batchMap.find(batchKey);
        if (batchIt == batchMap.end()) {
            batchIndex = batches.size();
            batches.push_back(SpriteBatch());
            batches[batchIndex].textureId = layer.textureId;
            batches[batchIndex].normalMapId = layer.normalMapId;
            batches[batchIndex].descriptorId = layer.descriptorId;
            batches[batchIndex].pipelineId = layer.pipelineId;
            batchMap[batchKey] = batchIndex;
        } else {
            batchIndex = batchIt->second;
        }
        
        SpriteBatch& batch = batches[batchIndex];
        
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
        
        uint16_t baseIndex = static_cast<uint16_t>(batch.vertices.size());
        
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
            
            batch.vertices.push_back(vert);
        }
        
        // Create indices for two triangles (quad)
        batch.indices.push_back(baseIndex + 0);
        batch.indices.push_back(baseIndex + 1);
        batch.indices.push_back(baseIndex + 2);
        
        batch.indices.push_back(baseIndex + 2);
        batch.indices.push_back(baseIndex + 3);
        batch.indices.push_back(baseIndex + 0);
    }
    
    // Sort batches by pipeline ID first, then by descriptor ID for deterministic rendering order
    std::sort(batches.begin(), batches.end(), [](const SpriteBatch& a, const SpriteBatch& b) {
        if (a.pipelineId != b.pipelineId) {
            return a.pipelineId < b.pipelineId;
        }
        return a.descriptorId < b.descriptorId;
    });
}
