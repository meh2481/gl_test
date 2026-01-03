#pragma once

#include <cstdint>
#include "../core/Vector.h"
#include "../core/HashTable.h"

// Forward declaration
class MemoryAllocator;
class TrigLookup;

// Maximum number of vertices for polygon layers (8 vertices * 2 floats per vertex = 16)
static const int MAX_POLYGON_VERTEX_FLOATS = 16;

// Sprite vertex structure with position and texture coordinates
struct SpriteVertex {
    float x, y;        // Position
    float u, v;        // Texture coordinates (diffuse)
    float nu, nv;      // Normal map texture coordinates
    float uvMinX, uvMinY, uvMaxX, uvMaxY;  // UV bounds for atlas clamping (prevents MSAA bleeding)
};

// Sprite batch for a single texture
struct SpriteBatch {
    uint64_t textureId;
    uint64_t normalMapId;    // Normal map texture ID (0 if none)
    uint64_t descriptorId;   // Descriptor set ID to use for this batch
    int pipelineId;          // Pipeline ID to use for this batch
    float parallaxDepth;     // Parallax depth for sorting (lower = background, higher = foreground)
    Vector<SpriteVertex> vertices;
    Vector<uint16_t> indices;

    // Animation parameters for this batch
    float spinSpeed;         // Degrees per second
    float blinkSecondsOn;
    float blinkSecondsOff;
    float blinkRiseTime;
    float blinkFallTime;
    float blinkPhase;
    float waveWavelength;
    float waveSpeed;
    float waveAngle;
    float waveAmplitude;
    float colorR, colorG, colorB, colorA;
    float colorEndR, colorEndG, colorEndB, colorEndA;
    float colorCycleTime;
    float colorPhase;
    float centerX, centerY;  // Center point for spin rotation

    // Constructor
    SpriteBatch(MemoryAllocator& allocator)
        : textureId(0), normalMapId(0), descriptorId(0), pipelineId(0), parallaxDepth(0.0f),
          vertices(allocator, "SpriteBatch::vertices"), indices(allocator, "SpriteBatch::indices"),
          spinSpeed(0.0f), blinkSecondsOn(0.0f), blinkSecondsOff(0.0f),
          blinkRiseTime(0.0f), blinkFallTime(0.0f), blinkPhase(0.0f),
          waveWavelength(0.0f), waveSpeed(0.0f), waveAngle(0.0f), waveAmplitude(0.0f),
          colorR(1.0f), colorG(1.0f), colorB(1.0f), colorA(1.0f),
          colorEndR(1.0f), colorEndG(1.0f), colorEndB(1.0f), colorEndA(1.0f),
          colorCycleTime(0.0f), colorPhase(0.0f), centerX(0.0f), centerY(0.0f) {}
};

// Particle vertex structure with position, texture coordinates, and color
struct ParticleVertex {
    float x, y;        // Position
    float u, v;        // Texture coordinates
    float r, g, b, a;  // Vertex color
    float uvMinX, uvMinY, uvMaxX, uvMaxY;  // UV bounds for atlas clamping
};

// Particle batch for a group of particles at a specific parallax depth
struct ParticleBatch {
    uint64_t textureId;      // Atlas texture ID
    int pipelineId;          // Pipeline ID to use for this batch
    float parallaxDepth;     // Parallax depth for sorting
    Vector<ParticleVertex> vertices;
    Vector<uint16_t> indices;

    // Constructor
    ParticleBatch(MemoryAllocator& allocator)
        : textureId(0), pipelineId(0), parallaxDepth(0.0f),
          vertices(allocator, "ParticleBatch::vertices"), indices(allocator, "ParticleBatch::indices") {}
};

// Atlas UV coordinates for texture
struct LayerAtlasUV {
    float u0, v0;       // Bottom-left UV
    float u1, v1;       // Top-right UV
    bool isAtlas;       // Whether this layer uses atlas coordinates
};

// Scene layer that can be attached to a physics body
struct SceneLayer {
    uint64_t textureId;      // Resource ID of the primary texture
    uint64_t normalMapId;    // Resource ID of normal map (0 if none)
    uint64_t atlasTextureId; // Atlas texture ID (if using atlas, otherwise same as textureId)
    uint64_t atlasNormalMapId; // Atlas normal map ID (if using atlas)
    uint64_t descriptorId;   // Descriptor set ID to use for rendering
    int pipelineId;          // Pipeline ID to use for rendering
    int physicsBodyId;       // Physics body this layer is attached to (-1 if not attached)
    float width;             // Width of sprite in world units
    float height;            // Height of sprite in world units
    float offsetX;           // Offset from body center
    float offsetY;           // Offset from body center
    float scaleX;            // X scale factor (default 1.0)
    float scaleY;            // Y scale factor (default 1.0)
    bool enabled;            // Whether this layer is visible
    bool useLocalUV;         // Whether to use local 0..1 UVs instead of texture atlas UVs

    // Atlas UV coordinates for texture
    LayerAtlasUV textureUV;
    LayerAtlasUV normalMapUV;

    // Polygon vertices for non-quad shapes (fragment rendering)
    // If polygonVertexCount > 0, use polygon rendering instead of quad
    float polygonVertices[MAX_POLYGON_VERTEX_FLOATS];  // Max 8 vertices, x/y pairs (local coordinates)
    float polygonUVs[MAX_POLYGON_VERTEX_FLOATS];       // UV coordinates for each polygon vertex (texture)
    float polygonNormalUVs[MAX_POLYGON_VERTEX_FLOATS]; // UV coordinates for each polygon vertex (normal map)
    int polygonVertexCount;     // 0 = use quad, > 0 = use polygon

    // Cached transform from physics
    float cachedX;
    float cachedY;
    float cachedAngle;

    // Parallax depth for layers without physics bodies
    float parallaxDepth;

    // Animation parameters
    // Spin: rotation speed in degrees per second
    float spinSpeed;

    // Blink: visibility animation
    float blinkSecondsOn;
    float blinkSecondsOff;
    float blinkRiseTime;
    float blinkFallTime;
    float blinkPhase;  // Current phase offset in seconds

    // Wave: sine wave displacement
    float waveWavelength;
    float waveSpeed;
    float waveAngle;
    float waveAmplitude;

    // Color: animated color modulation
    float colorR, colorG, colorB, colorA;
    float colorEndR, colorEndG, colorEndB, colorEndA;
    float colorCycleTime;
    float colorPhase;
};

// Manager for scene layers
class SceneLayerManager {
public:
    SceneLayerManager(MemoryAllocator* smallAllocator, MemoryAllocator* largeAllocator, TrigLookup* trigLookup);
    ~SceneLayerManager();

    // Layer management
    int createLayer(uint64_t textureId, float width, float height, uint64_t normalMapId = 0, int pipelineId = -1);
    void destroyLayer(int layerId);
    void attachLayerToBody(int layerId, int physicsBodyId);
    void detachLayer(int layerId);
    void setLayerOffset(int layerId, float offsetX, float offsetY);
    void setLayerEnabled(int layerId, bool enabled);

    // Set atlas UV coordinates for a layer's texture
    void setLayerAtlasUV(int layerId, uint64_t atlasTextureId, float u0, float v0, float u1, float v1);
    void setLayerNormalMapAtlasUV(int layerId, uint64_t atlasNormalMapId, float u0, float v0, float u1, float v1);

    // Set polygon vertices and UVs for fragment rendering (texture clipping)
    // vertices: array of x,y pairs in local coordinates
    // uvs: array of u,v pairs for texture coordinates
    // normalUvs: array of u,v pairs for normal map coordinates (can be NULL to use same as uvs)
    // vertexCount: number of vertices (3-8)
    void setLayerPolygon(int layerId, const float* vertices, const float* uvs, const float* normalUvs, int vertexCount);

    // Get all active layers
    const HashTable<int, SceneLayer>& getLayers() const { return layers_; }

    // Generate vertex data for all layers based on physics body positions
    // Groups sprites by texture for efficient batch rendering
    void updateLayerVertices(Vector<SpriteBatch>& batches) {
        updateLayerVertices(batches, 0.0f, 0.0f, 1.0f);
    }

    // Update a single layer's transform based on physics body
    void updateLayerTransform(int layerId, float bodyX, float bodyY, float bodyAngle);
    void setLayerUseLocalUV(int layerId, bool useLocalUV);

    // Set a layer's position directly (for layers without physics bodies)
    void setLayerPosition(int layerId, float x, float y, float angle = 0.0f);

    // Set parallax depth for a layer (used for layers without physics bodies)
    void setLayerParallaxDepth(int layerId, float depth);

    // Set scale for a layer
    void setLayerScale(int layerId, float scaleX, float scaleY);

    // Animation effect setters
    void setLayerSpin(int layerId, float degreesPerSecond);
    void setLayerBlink(int layerId, float secondsOn, float secondsOff, float riseTime, float fallTime);
    void setLayerWave(int layerId, float wavelength, float speed, float angle, float amplitude);
    void setLayerColor(int layerId, float r, float g, float b, float a);
    void setLayerColorCycle(int layerId, float r1, float g1, float b1, float a1, float r2, float g2, float b2, float a2, float cycleTime);

    // Update layer vertices with camera info for parallax calculation
    void updateLayerVertices(Vector<SpriteBatch>& batches, float cameraX, float cameraY, float cameraZoom);

    // Clear all layers (for scene cleanup)
    void clear();

private:
    HashTable<int, SceneLayer> layers_;
    int nextLayerId_;
    MemoryAllocator* allocator_;
    TrigLookup* trigLookup_;
};
