#include "SceneLayer.h"
#include <cmath>
#include <cassert>
#include <algorithm>

// Epsilon for parallax depth comparisons
static const float PARALLAX_EPSILON = 0.001f;

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
    layer.atlasTextureId = textureId;  // Default to same as textureId
    layer.atlasNormalMapId = normalMapId;  // Default to same as normalMapId
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
    layer.scaleX = 1.0f;
    layer.scaleY = 1.0f;
    layer.enabled = true;
    layer.useLocalUV = false;

    // Default UV coordinates (full texture)
    layer.textureUV.u0 = 0.0f;
    layer.textureUV.v0 = 0.0f;
    layer.textureUV.u1 = 1.0f;
    layer.textureUV.v1 = 1.0f;
    layer.textureUV.isAtlas = false;

    layer.normalMapUV.u0 = 0.0f;
    layer.normalMapUV.v0 = 0.0f;
    layer.normalMapUV.u1 = 1.0f;
    layer.normalMapUV.v1 = 1.0f;
    layer.normalMapUV.isAtlas = false;

    // Default to quad rendering (no polygon)
    layer.polygonVertexCount = 0;
    // Initialize polygon arrays to zero (defensive against uninitialized reads)
    for (int i = 0; i < MAX_POLYGON_VERTEX_FLOATS; ++i) {
        layer.polygonVertices[i] = 0.0f;
        layer.polygonUVs[i] = 0.0f;
        layer.polygonNormalUVs[i] = 0.0f;
    }

    layer.cachedX = 0.0f;
    layer.cachedY = 0.0f;
    layer.cachedAngle = 0.0f;
    layer.parallaxDepth = 0.0f;

    layers_[layerId] = layer;
    return layerId;
}

void SceneLayerManager::setLayerUseLocalUV(int layerId, bool useLocalUV) {
    auto it = layers_.find(layerId);
    if (it != layers_.end()) {
        it->second.useLocalUV = useLocalUV;
    }
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

void SceneLayerManager::setLayerAtlasUV(int layerId, uint64_t atlasTextureId, float u0, float v0, float u1, float v1) {
    auto it = layers_.find(layerId);
    if (it != layers_.end()) {
        it->second.atlasTextureId = atlasTextureId;
        it->second.textureUV.u0 = u0;
        it->second.textureUV.v0 = v0;
        it->second.textureUV.u1 = u1;
        it->second.textureUV.v1 = v1;
        it->second.textureUV.isAtlas = true;

        // Update descriptor ID to use atlas texture
        if (it->second.normalMapUV.isAtlas) {
            it->second.descriptorId = atlasTextureId ^ (it->second.atlasNormalMapId << 1);
        } else if (it->second.normalMapId != 0) {
            it->second.descriptorId = atlasTextureId ^ (it->second.normalMapId << 1);
        } else {
            it->second.descriptorId = atlasTextureId;
        }
    }
}

void SceneLayerManager::setLayerNormalMapAtlasUV(int layerId, uint64_t atlasNormalMapId, float u0, float v0, float u1, float v1) {
    auto it = layers_.find(layerId);
    if (it != layers_.end()) {
        it->second.atlasNormalMapId = atlasNormalMapId;
        it->second.normalMapUV.u0 = u0;
        it->second.normalMapUV.v0 = v0;
        it->second.normalMapUV.u1 = u1;
        it->second.normalMapUV.v1 = v1;
        it->second.normalMapUV.isAtlas = true;

        // Update descriptor ID to use atlas textures
        uint64_t texId = it->second.textureUV.isAtlas ? it->second.atlasTextureId : it->second.textureId;
        it->second.descriptorId = texId ^ (atlasNormalMapId << 1);
    }
}

void SceneLayerManager::setLayerPolygon(int layerId, const float* vertices, const float* uvs, const float* normalUvs, int vertexCount) {
    auto it = layers_.find(layerId);
    if (it != layers_.end() && vertexCount >= 3 && vertexCount <= 8) {
        // Validate input data - check for NaN/Inf values
        for (int i = 0; i < vertexCount * 2; ++i) {
            assert(std::isfinite(vertices[i]));
            assert(std::isfinite(uvs[i]));
            if (normalUvs) {
                assert(std::isfinite(normalUvs[i]));
            }
        }

        it->second.polygonVertexCount = vertexCount;
        for (int i = 0; i < vertexCount * 2; ++i) {
            it->second.polygonVertices[i] = vertices[i];
            it->second.polygonUVs[i] = uvs[i];
            // Use separate normal map UVs if provided, otherwise same as texture UVs
            it->second.polygonNormalUVs[i] = normalUvs ? normalUvs[i] : uvs[i];
        }
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

void SceneLayerManager::setLayerPosition(int layerId, float x, float y, float angle) {
    auto it = layers_.find(layerId);
    if (it != layers_.end()) {
        it->second.cachedX = x;
        it->second.cachedY = y;
        it->second.cachedAngle = angle;
    }
}

void SceneLayerManager::setLayerParallaxDepth(int layerId, float depth) {
    auto it = layers_.find(layerId);
    if (it != layers_.end()) {
        it->second.parallaxDepth = depth;
    }
}

void SceneLayerManager::setLayerScale(int layerId, float scaleX, float scaleY) {
    auto it = layers_.find(layerId);
    if (it != layers_.end()) {
        it->second.scaleX = scaleX;
        it->second.scaleY = scaleY;
    }
}

void SceneLayerManager::clear() {
    layers_.clear();
}

void SceneLayerManager::updateLayerVertices(std::vector<SpriteBatch>& batches, float cameraX, float cameraY, float cameraZoom) {
    batches.clear();

    // Group layers by pipeline ID, descriptor ID, AND parallax depth
    // Use a tuple<pipelineId, descriptorId, parallaxDepth> as the batch map key
    // Map to batch index instead of pointer to avoid invalidation during sorting
    struct BatchKey {
        int pipelineId;
        uint64_t descriptorId;
        float parallaxDepth;

        bool operator==(const BatchKey& other) const {
            return pipelineId == other.pipelineId &&
                   descriptorId == other.descriptorId &&
                   std::abs(parallaxDepth - other.parallaxDepth) < PARALLAX_EPSILON;
        }
    };
    struct BatchKeyHash {
        std::size_t operator()(const BatchKey& k) const {
            auto h1 = std::hash<int>{}(k.pipelineId);
            auto h2 = std::hash<uint64_t>{}(k.descriptorId);
            auto h3 = std::hash<int>{}(static_cast<int>(k.parallaxDepth * 1000));
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
    std::unordered_map<BatchKey, size_t, BatchKeyHash> batchMap;

    for (const auto& pair : layers_) {
        const SceneLayer& layer = pair.second;

        // Skip disabled layers
        // Allow layers without physics bodies if they have a parallax depth set (static layers)
        if (!layer.enabled) {
            continue;
        }
        // Skip layers without physics bodies unless they have parallax depth (static layers)
        if (layer.physicsBodyId < 0 && std::abs(layer.parallaxDepth) < PARALLAX_EPSILON) {
            continue;
        }

        // Create batch key from pipeline ID, descriptor ID, and parallax depth
        BatchKey batchKey{layer.pipelineId, layer.descriptorId, layer.parallaxDepth};

        // Find or create batch for this key
        size_t batchIndex;
        auto batchIt = batchMap.find(batchKey);
        if (batchIt == batchMap.end()) {
            batchIndex = batches.size();
            batches.push_back(SpriteBatch());
            // Use atlas texture ID if available, otherwise original texture ID
            batches[batchIndex].textureId = layer.textureUV.isAtlas ? layer.atlasTextureId : layer.textureId;
            batches[batchIndex].normalMapId = layer.normalMapUV.isAtlas ? layer.atlasNormalMapId : layer.normalMapId;
            batches[batchIndex].descriptorId = layer.descriptorId;
            batches[batchIndex].pipelineId = layer.pipelineId;
            batches[batchIndex].parallaxDepth = layer.parallaxDepth;
            batchMap[batchKey] = batchIndex;
        } else {
            batchIndex = batchIt->second;
        }

        SpriteBatch& batch = batches[batchIndex];

        float centerX = layer.cachedX;
        float centerY = layer.cachedY;
        float angle = layer.cachedAngle;

        // Apply parallax offset for layers without physics bodies
        if (layer.physicsBodyId < 0 && std::abs(layer.parallaxDepth) >= PARALLAX_EPSILON) {
            // Parallax factor: depth / (1.0 + abs(depth))
            // depth < 0: foreground, moves faster (appears closer)
            // depth > 0: background, moves slower (appears farther)
            float absDepth = std::abs(layer.parallaxDepth);
            float parallaxFactor = layer.parallaxDepth / (1.0f + absDepth);
            // Apply parallax offset to position based on camera offset
            centerX += cameraX * parallaxFactor;
            centerY += cameraY * parallaxFactor;
        }

        // Apply rotation and position
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        uint16_t baseIndex = static_cast<uint16_t>(batch.vertices.size());

        // Check if using polygon rendering (for fragment texture clipping)
        if (layer.polygonVertexCount >= 3) {
            // Validate polygon vertex count
            assert(layer.polygonVertexCount <= 8);

            // Get UV bounds for atlas clamping
            float u0 = layer.textureUV.u0;
            float v0 = layer.textureUV.v0;
            float u1 = layer.textureUV.u1;
            float v1 = layer.textureUV.v1;

            // Polygon rendering with per-vertex UVs
            for (int i = 0; i < layer.polygonVertexCount; i++) {
                // Get local vertex position
                float lx = layer.polygonVertices[i * 2] + layer.offsetX;
                float ly = layer.polygonVertices[i * 2 + 1] + layer.offsetY;

                // Validate vertex positions are finite
                assert(std::isfinite(lx) && std::isfinite(ly));

                // Rotate
                float rx = lx * cosA - ly * sinA;
                float ry = lx * sinA + ly * cosA;

                // Get texture UV from polygon UVs
                float u = layer.polygonUVs[i * 2];
                float v = layer.polygonUVs[i * 2 + 1];

                // Get normal map UV from polygon normal UVs
                float nu = layer.polygonNormalUVs[i * 2];
                float nv = layer.polygonNormalUVs[i * 2 + 1];

                // Translate to body position
                SpriteVertex vert;
                vert.x = centerX + rx;
                vert.y = centerY + ry;
                vert.u = u;
                vert.v = v;
                vert.nu = nu;
                vert.nv = nv;
                // Store UV bounds for atlas clamping (prevents MSAA bleeding)
                vert.uvMinX = u0;
                vert.uvMinY = v0;
                vert.uvMaxX = u1;
                vert.uvMaxY = v1;

                // Validate final vertex position is finite
                assert(std::isfinite(vert.x) && std::isfinite(vert.y));

                batch.vertices.push_back(vert);
            }

            // Create triangle fan indices for polygon (works for convex polygons)
            for (int i = 1; i < layer.polygonVertexCount - 1; i++) {
                batch.indices.push_back(baseIndex + 0);
                batch.indices.push_back(baseIndex + i);
                batch.indices.push_back(baseIndex + i + 1);
            }
        } else {
            // Standard quad rendering
            // Calculate half-extents with user-defined scale applied
            float hw = layer.width * 0.5f * layer.scaleX;
            float hh = layer.height * 0.5f * layer.scaleY;

            // Create quad vertices (4 vertices for a sprite)
            // Vertices in local space (before rotation)
            float localVerts[4][2] = {
                {-hw, -hh},  // Bottom-left
                { hw, -hh},  // Bottom-right
                { hw,  hh},  // Top-right
                {-hw,  hh}   // Top-left
            };

            // Get UV coordinates from layer (supports atlas or full texture)
            float u0 = layer.textureUV.u0;
            float v0 = layer.textureUV.v0;
            float u1 = layer.textureUV.u1;
            float v1 = layer.textureUV.v1;

            // Get normal map UV coordinates
            float nu0 = layer.normalMapUV.u0;
            float nv0 = layer.normalMapUV.v0;
            float nu1 = layer.normalMapUV.u1;
            float nv1 = layer.normalMapUV.v1;

            float uvs[4][2];
            if (layer.useLocalUV) {
                // Use local 0..1 coordinates across quad (left->right, bottom->top)
                uvs[0][0] = 0.0f; uvs[0][1] = 0.0f; // Bottom-left
                uvs[1][0] = 1.0f; uvs[1][1] = 0.0f; // Bottom-right
                uvs[2][0] = 1.0f; uvs[2][1] = 1.0f; // Top-right
                uvs[3][0] = 0.0f; uvs[3][1] = 1.0f; // Top-left
            } else {
                // Texture coordinates using atlas UV or default 0-1
                uvs[0][0] = u0; uvs[0][1] = v1;  // Bottom-left
                uvs[1][0] = u1; uvs[1][1] = v1;  // Bottom-right
                uvs[2][0] = u1; uvs[2][1] = v0;  // Top-right
                uvs[3][0] = u0; uvs[3][1] = v0;  // Top-left
            }

            // Normal map texture coordinates
            float nuvs[4][2] = {
                {nu0, nv1},  // Bottom-left
                {nu1, nv1},  // Bottom-right
                {nu1, nv0},  // Top-right
                {nu0, nv0}   // Top-left
            };

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
                vert.nu = nuvs[i][0];
                vert.nv = nuvs[i][1];
                // Store UV bounds for atlas clamping (prevents MSAA bleeding)
                vert.uvMinX = u0;
                vert.uvMinY = v0;
                vert.uvMaxX = u1;
                vert.uvMaxY = v1;

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
    }

    // Sort batches by parallax depth (lower/positive = background = drawn first, higher/negative = foreground = drawn last)
    // Within same parallax depth, sort by pipeline ID then descriptor ID for deterministic order
    std::sort(batches.begin(), batches.end(), [](const SpriteBatch& a, const SpriteBatch& b) {
        // Sort by parallax depth first (higher depth = background = drawn first)
        // Positive depth = background, negative depth = foreground
        if (std::abs(a.parallaxDepth - b.parallaxDepth) >= PARALLAX_EPSILON) {
            return a.parallaxDepth > b.parallaxDepth; // Higher depth (background) drawn first
        }
        // Then by pipeline ID
        if (a.pipelineId != b.pipelineId) {
            return a.pipelineId < b.pipelineId;
        }
        // Then by descriptor ID
        return a.descriptorId < b.descriptorId;
    });
}
