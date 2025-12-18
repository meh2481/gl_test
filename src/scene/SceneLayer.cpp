#include "SceneLayer.h"
#include <cmath>
#include <cassert>
#include <functional>

// Epsilon for parallax depth comparisons
static const float PARALLAX_EPSILON = 0.001f;

SceneLayerManager::SceneLayerManager(MemoryAllocator* allocator)
    : layers_(*allocator, "SceneLayerManager::layers"), nextLayerId_(1), allocator_(allocator) {
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

    // Initialize animation parameters to default (no animation)
    layer.spinSpeed = 0.0f;
    layer.blinkSecondsOn = 0.0f;
    layer.blinkSecondsOff = 0.0f;
    layer.blinkRiseTime = 0.0f;
    layer.blinkFallTime = 0.0f;
    layer.blinkPhase = 0.0f;
    layer.waveWavelength = 0.0f;
    layer.waveSpeed = 0.0f;
    layer.waveAngle = 0.0f;
    layer.waveAmplitude = 0.0f;
    layer.colorR = 1.0f;
    layer.colorG = 1.0f;
    layer.colorB = 1.0f;
    layer.colorA = 1.0f;
    layer.colorEndR = 1.0f;
    layer.colorEndG = 1.0f;
    layer.colorEndB = 1.0f;
    layer.colorEndA = 1.0f;
    layer.colorCycleTime = 0.0f;
    layer.colorPhase = 0.0f;

    layers_.insert(layerId, layer);
    return layerId;
}

void SceneLayerManager::setLayerUseLocalUV(int layerId, bool useLocalUV) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->useLocalUV = useLocalUV;
    }
}

void SceneLayerManager::destroyLayer(int layerId) {
    layers_.remove(layerId);
}

void SceneLayerManager::attachLayerToBody(int layerId, int physicsBodyId) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->physicsBodyId = physicsBodyId;
    }
}

void SceneLayerManager::detachLayer(int layerId) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->physicsBodyId = -1;
    }
}

void SceneLayerManager::setLayerOffset(int layerId, float offsetX, float offsetY) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->offsetX = offsetX;
        layer->offsetY = offsetY;
    }
}

void SceneLayerManager::setLayerEnabled(int layerId, bool enabled) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->enabled = enabled;
    }
}

void SceneLayerManager::setLayerAtlasUV(int layerId, uint64_t atlasTextureId, float u0, float v0, float u1, float v1) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->atlasTextureId = atlasTextureId;
        layer->textureUV.u0 = u0;
        layer->textureUV.v0 = v0;
        layer->textureUV.u1 = u1;
        layer->textureUV.v1 = v1;
        layer->textureUV.isAtlas = true;

        // Update descriptor ID to use atlas texture
        if (layer->normalMapUV.isAtlas) {
            layer->descriptorId = atlasTextureId ^ (layer->atlasNormalMapId << 1);
        } else if (layer->normalMapId != 0) {
            layer->descriptorId = atlasTextureId ^ (layer->normalMapId << 1);
        } else {
            layer->descriptorId = atlasTextureId;
        }
    }
}

void SceneLayerManager::setLayerNormalMapAtlasUV(int layerId, uint64_t atlasNormalMapId, float u0, float v0, float u1, float v1) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->atlasNormalMapId = atlasNormalMapId;
        layer->normalMapUV.u0 = u0;
        layer->normalMapUV.v0 = v0;
        layer->normalMapUV.u1 = u1;
        layer->normalMapUV.v1 = v1;
        layer->normalMapUV.isAtlas = true;

        // Update descriptor ID to use atlas textures
        uint64_t texId = layer->textureUV.isAtlas ? layer->atlasTextureId : layer->textureId;
        layer->descriptorId = texId ^ (atlasNormalMapId << 1);
    }
}

void SceneLayerManager::setLayerPolygon(int layerId, const float* vertices, const float* uvs, const float* normalUvs, int vertexCount) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr && vertexCount >= 3 && vertexCount <= 8) {
        // Validate input data - check for NaN/Inf values
        for (int i = 0; i < vertexCount * 2; ++i) {
            assert(std::isfinite(vertices[i]));
            assert(std::isfinite(uvs[i]));
            if (normalUvs) {
                assert(std::isfinite(normalUvs[i]));
            }
        }

        layer->polygonVertexCount = vertexCount;
        for (int i = 0; i < vertexCount * 2; ++i) {
            layer->polygonVertices[i] = vertices[i];
            layer->polygonUVs[i] = uvs[i];
            // Use separate normal map UVs if provided, otherwise same as texture UVs
            layer->polygonNormalUVs[i] = normalUvs ? normalUvs[i] : uvs[i];
        }
    }
}

void SceneLayerManager::updateLayerTransform(int layerId, float bodyX, float bodyY, float bodyAngle) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->cachedX = bodyX;
        layer->cachedY = bodyY;
        layer->cachedAngle = bodyAngle;
    }
}

void SceneLayerManager::setLayerPosition(int layerId, float x, float y, float angle) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->cachedX = x;
        layer->cachedY = y;
        layer->cachedAngle = angle;
    }
}

void SceneLayerManager::setLayerParallaxDepth(int layerId, float depth) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->parallaxDepth = depth;
    }
}

void SceneLayerManager::setLayerScale(int layerId, float scaleX, float scaleY) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->scaleX = scaleX;
        layer->scaleY = scaleY;
    }
}

void SceneLayerManager::setLayerSpin(int layerId, float degreesPerSecond) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->spinSpeed = degreesPerSecond;
    }
}

void SceneLayerManager::setLayerBlink(int layerId, float secondsOn, float secondsOff, float riseTime, float fallTime) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->blinkSecondsOn = secondsOn;
        layer->blinkSecondsOff = secondsOff;
        layer->blinkRiseTime = riseTime;
        layer->blinkFallTime = fallTime;
        layer->blinkPhase = 0.0f;
    }
}

void SceneLayerManager::setLayerWave(int layerId, float wavelength, float speed, float angle, float amplitude) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->waveWavelength = wavelength;
        layer->waveSpeed = speed;
        layer->waveAngle = angle;
        layer->waveAmplitude = amplitude;
    }
}

void SceneLayerManager::setLayerColor(int layerId, float r, float g, float b, float a) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->colorR = r;
        layer->colorG = g;
        layer->colorB = b;
        layer->colorA = a;
        layer->colorEndR = r;
        layer->colorEndG = g;
        layer->colorEndB = b;
        layer->colorEndA = a;
        layer->colorCycleTime = 0.0f;
    }
}

void SceneLayerManager::setLayerColorCycle(int layerId, float r1, float g1, float b1, float a1,
                                           float r2, float g2, float b2, float a2, float cycleTime) {
    SceneLayer* layer = layers_.find(layerId);
    if (layer != nullptr) {
        layer->colorR = r1;
        layer->colorG = g1;
        layer->colorB = b1;
        layer->colorA = a1;
        layer->colorEndR = r2;
        layer->colorEndG = g2;
        layer->colorEndB = b2;
        layer->colorEndA = a2;
        layer->colorCycleTime = cycleTime;
        layer->colorPhase = 0.0f;
    }
}

void SceneLayerManager::clear() {
    layers_.clear();
}

void SceneLayerManager::updateLayerVertices(Vector<SpriteBatch>& batches, float cameraX, float cameraY, float cameraZoom) {
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
    HashTable<BatchKey, size_t> batchMap(*allocator_, "updateLayerVertices::batchMap");

    for (auto it = layers_.begin(); it != layers_.end(); ++it) {
        const SceneLayer& layer = it.value();

        // Skip disabled layers
        // Allow layers without physics bodies if they have a parallax depth set (static layers)
        if (!layer.enabled) {
            continue;
        }

        // Create batch key from pipeline ID, descriptor ID, and parallax depth
        BatchKey batchKey{layer.pipelineId, layer.descriptorId, layer.parallaxDepth};

        // Check if layer has any animation effects enabled
        // Layers with animation need separate batches as they have per-batch push constants
        // This includes: spin, blink, wave, color cycling, or static color modulation
        bool hasAnimation = layer.spinSpeed != 0.0f ||
                           layer.blinkSecondsOn > 0.0f ||
                           layer.waveAmplitude != 0.0f ||
                           layer.colorCycleTime > 0.0f ||
                           layer.colorR != 1.0f || layer.colorG != 1.0f ||
                           layer.colorB != 1.0f || layer.colorA != 1.0f;

        // Find or create batch for this key
        size_t batchIndex;
        size_t* batchIndexPtr = batchMap.find(batchKey);
        if (batchIndexPtr == nullptr || hasAnimation) {
            // Always create new batch for animated layers or if batch doesn't exist
            batchIndex = batches.size();
            batches.push_back(SpriteBatch(batches.getAllocator()));
            // Use atlas texture ID if available, otherwise original texture ID
            batches[batchIndex].textureId = layer.textureUV.isAtlas ? layer.atlasTextureId : layer.textureId;
            batches[batchIndex].normalMapId = layer.normalMapUV.isAtlas ? layer.atlasNormalMapId : layer.normalMapId;
            batches[batchIndex].descriptorId = layer.descriptorId;
            batches[batchIndex].pipelineId = layer.pipelineId;
            batches[batchIndex].parallaxDepth = layer.parallaxDepth;

            // Copy animation parameters
            batches[batchIndex].spinSpeed = layer.spinSpeed;
            batches[batchIndex].blinkSecondsOn = layer.blinkSecondsOn;
            batches[batchIndex].blinkSecondsOff = layer.blinkSecondsOff;
            batches[batchIndex].blinkRiseTime = layer.blinkRiseTime;
            batches[batchIndex].blinkFallTime = layer.blinkFallTime;
            batches[batchIndex].blinkPhase = layer.blinkPhase;
            batches[batchIndex].waveWavelength = layer.waveWavelength;
            batches[batchIndex].waveSpeed = layer.waveSpeed;
            batches[batchIndex].waveAngle = layer.waveAngle;
            batches[batchIndex].waveAmplitude = layer.waveAmplitude;
            batches[batchIndex].colorR = layer.colorR;
            batches[batchIndex].colorG = layer.colorG;
            batches[batchIndex].colorB = layer.colorB;
            batches[batchIndex].colorA = layer.colorA;
            batches[batchIndex].colorEndR = layer.colorEndR;
            batches[batchIndex].colorEndG = layer.colorEndG;
            batches[batchIndex].colorEndB = layer.colorEndB;
            batches[batchIndex].colorEndA = layer.colorEndA;
            batches[batchIndex].colorCycleTime = layer.colorCycleTime;
            batches[batchIndex].colorPhase = layer.colorPhase;

            // Only add to batch map if not animated (animated layers always get unique batches)
            if (!hasAnimation) {
                batchMap.insert(batchKey, batchIndex);
            }
        } else {
            batchIndex = *batchIndexPtr;
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

        // Update batch spin center to match the parallax-adjusted position
        batch.centerX = centerX;
        batch.centerY = centerY;

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
    batches.sort([](const SpriteBatch& a, const SpriteBatch& b) {
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
