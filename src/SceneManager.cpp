#include "SceneManager.h"
#include "LuaInterface.h"
#include "SceneLayer.h"
#include "ParticleSystem.h"
#include <cassert>
#include <cmath>

SceneManager::SceneManager(PakResource& pakResource, VulkanRenderer& renderer, VibrationManager* vibrationManager)
    : pakResource_(pakResource), renderer_(renderer), luaInterface_(std::make_unique<LuaInterface>(pakResource, renderer, this, vibrationManager)), pendingPop_(false), particleEditorActive_(false), particleEditorPipelineId_(-1) {
}

SceneManager::~SceneManager() {
    // LuaInterface will be automatically cleaned up
}

void SceneManager::pushScene(uint64_t sceneId) {
    // Load the scene if not already loaded
    if (loadedScenes_.find(sceneId) == loadedScenes_.end()) {
        ResourceData sceneScript = pakResource_.getResource(sceneId);
        luaInterface_->loadScene(sceneId, sceneScript);
        loadedScenes_.insert(sceneId);
    }

    // Push scene onto stack
    sceneStack_.push(sceneId);

    // Initialize the scene if not already initialized
    if (initializedScenes_.find(sceneId) == initializedScenes_.end()) {
        luaInterface_->initScene(sceneId);
        initializedScenes_.insert(sceneId);
    }

    // Set the pipelines for this scene
    luaInterface_->switchToScenePipeline(sceneId);
}

void SceneManager::popScene() {
    if (!sceneStack_.empty()) {
        pendingPop_ = true;
    }
}

bool SceneManager::isEmpty() const {
    return sceneStack_.empty();
}

uint64_t SceneManager::getActiveSceneId() const {
    if (!sceneStack_.empty()) {
        return sceneStack_.top();
    }
    return 0; // Or some invalid ID
}

void SceneManager::reloadCurrentScene() {
    if (!sceneStack_.empty()) {
        uint64_t currentSceneId = sceneStack_.top();
        // Cleanup the scene before reloading
        luaInterface_->cleanupScene(currentSceneId);
        // Clear existing pipelines for this scene
        luaInterface_->clearScenePipelines(currentSceneId);
        // Remove from loaded scenes so it will reload
        loadedScenes_.erase(currentSceneId);
        // Mark as not initialized so it will reinitialize
        initializedScenes_.erase(currentSceneId);
        // Reinitialize the scene
        luaInterface_->loadScene(currentSceneId, pakResource_.getResource(currentSceneId));
        luaInterface_->initScene(currentSceneId);
        luaInterface_->switchToScenePipeline(currentSceneId);
    }
}

void SceneManager::initActiveScene() {
    // initActiveScene is called after pushing, so this might not be needed
    // But keeping for consistency
    if (!sceneStack_.empty()) {
        // Scenes are initialized when pushed
    }
}

bool SceneManager::updateActiveScene(float deltaTime) {
    if (!sceneStack_.empty()) {
        uint64_t activeSceneId = sceneStack_.top();
        luaInterface_->updateScene(activeSceneId, deltaTime);

        // Update scene layer transforms from physics bodies
        Box2DPhysics& physics = luaInterface_->getPhysics();
        SceneLayerManager& layerManager = luaInterface_->getSceneLayerManager();

        // Update each layer's transform based on its attached physics body
        for (const auto& layerPair : layerManager.getLayers()) {
            const SceneLayer& layer = layerPair.second;
            if (layer.physicsBodyId >= 0) {
                float bodyX = physics.getBodyPositionX(layer.physicsBodyId);
                float bodyY = physics.getBodyPositionY(layer.physicsBodyId);
                float bodyAngle = physics.getBodyAngle(layer.physicsBodyId);
                layerManager.updateLayerTransform(layerPair.first, bodyX, bodyY, bodyAngle);
            }
        }

        // Generate sprite batches grouped by texture
        std::vector<SpriteBatch> spriteBatches;
        layerManager.updateLayerVertices(spriteBatches);

        // Send batches to renderer
        renderer_.setSpriteBatches(spriteBatches);

        // Update and render particle systems
        ParticleSystemManager& particleManager = luaInterface_->getParticleSystemManager();
        particleManager.update(deltaTime);

        // Generate particle vertex data for all particle systems
        std::vector<float> particleVertexData;
        std::vector<uint16_t> particleIndices;

        for (int i = 0; i < particleManager.getSystemCount(); ++i) {
            ParticleSystem* system = &particleManager.getSystems()[i];
            if (!system || system->liveParticleCount == 0) continue;

            // Get texture UV coordinates (may be atlas or full texture)
            float texU0 = 0.0f, texV0 = 0.0f, texU1 = 1.0f, texV1 = 1.0f;
            if (system->config.textureCount > 0) {
                AtlasUV atlasUV;
                if (pakResource_.getAtlasUV(system->config.textureIds[0], atlasUV)) {
                    // Use atlas UV coordinates
                    texU0 = atlasUV.u0;
                    texV0 = atlasUV.v0;
                    texU1 = atlasUV.u1;
                    texV1 = atlasUV.v1;
                }
            }

            uint16_t baseVertex = (uint16_t)(particleVertexData.size() / 8);  // 8 floats per vertex

            for (int p = 0; p < system->liveParticleCount; ++p) {
                float x = system->posX[p];
                float y = system->posY[p];
                float size = system->size[p];
                float halfSize = size * 0.5f;

                // Calculate life ratio for color interpolation
                float lifeRatio = 1.0f - (system->lifetime[p] / system->totalLifetime[p]);

                // Interpolate color
                float r = system->colorR[p] + (system->endColorR[p] - system->colorR[p]) * lifeRatio;
                float g = system->colorG[p] + (system->endColorG[p] - system->colorG[p]) * lifeRatio;
                float b = system->colorB[p] + (system->endColorB[p] - system->colorB[p]) * lifeRatio;
                float a = system->colorA[p] + (system->endColorA[p] - system->colorA[p]) * lifeRatio;

                // Apply Z rotation to quad
                float cosZ = cosf(system->rotZ[p]);
                float sinZ = sinf(system->rotZ[p]);

                // Quad corners in local space
                float corners[4][2] = {
                    {-halfSize, -halfSize},  // Bottom-left
                    { halfSize, -halfSize},  // Bottom-right
                    { halfSize,  halfSize},  // Top-right
                    {-halfSize,  halfSize}   // Top-left
                };

                // UV coordinates using atlas UVs if available
                float uvs[4][2] = {
                    {texU0, texV1},  // Bottom-left
                    {texU1, texV1},  // Bottom-right
                    {texU1, texV0},  // Top-right
                    {texU0, texV0}   // Top-left
                };

                uint16_t vertexBase = (uint16_t)(particleVertexData.size() / 8);

                for (int v = 0; v < 4; ++v) {
                    // Rotate
                    float rx = corners[v][0] * cosZ - corners[v][1] * sinZ;
                    float ry = corners[v][0] * sinZ + corners[v][1] * cosZ;

                    // Translate
                    particleVertexData.push_back(x + rx);
                    particleVertexData.push_back(y + ry);
                    particleVertexData.push_back(uvs[v][0]);
                    particleVertexData.push_back(uvs[v][1]);
                    particleVertexData.push_back(r);
                    particleVertexData.push_back(g);
                    particleVertexData.push_back(b);
                    particleVertexData.push_back(a);
                }

                // Add indices for two triangles (quad)
                particleIndices.push_back(vertexBase + 0);
                particleIndices.push_back(vertexBase + 1);
                particleIndices.push_back(vertexBase + 2);
                particleIndices.push_back(vertexBase + 2);
                particleIndices.push_back(vertexBase + 3);
                particleIndices.push_back(vertexBase + 0);
            }
        }

        renderer_.setParticleDrawData(particleVertexData, particleIndices);

        // Update debug draw data if physics debug drawing is enabled
        if (physics.isDebugDrawEnabled()) {
            const std::vector<DebugVertex>& debugLineVerts = physics.getDebugLineVertices();
            std::vector<float> lineVertexData;
            lineVertexData.reserve(debugLineVerts.size() * 6);
            for (const auto& v : debugLineVerts) {
                lineVertexData.push_back(v.x);
                lineVertexData.push_back(v.y);
                lineVertexData.push_back(v.r);
                lineVertexData.push_back(v.g);
                lineVertexData.push_back(v.b);
                lineVertexData.push_back(v.a);
            }
            renderer_.setDebugLineDrawData(lineVertexData);

            const std::vector<DebugVertex>& debugTriangleVerts = physics.getDebugTriangleVertices();
            std::vector<float> triangleVertexData;
            triangleVertexData.reserve(debugTriangleVerts.size() * 6);
            for (size_t i = 0; i < debugTriangleVerts.size(); i += 3) {
                // Reverse winding order: v0, v2, v1 instead of v0, v1, v2
                const auto& v0 = debugTriangleVerts[i];
                const auto& v1 = debugTriangleVerts[i + 1];
                const auto& v2 = debugTriangleVerts[i + 2];
                // Push v0
                triangleVertexData.push_back(v0.x);
                triangleVertexData.push_back(v0.y);
                triangleVertexData.push_back(v0.r);
                triangleVertexData.push_back(v0.g);
                triangleVertexData.push_back(v0.b);
                triangleVertexData.push_back(v0.a);
                // Push v2
                triangleVertexData.push_back(v2.x);
                triangleVertexData.push_back(v2.y);
                triangleVertexData.push_back(v2.r);
                triangleVertexData.push_back(v2.g);
                triangleVertexData.push_back(v2.b);
                triangleVertexData.push_back(v2.a);
                // Push v1
                triangleVertexData.push_back(v1.x);
                triangleVertexData.push_back(v1.y);
                triangleVertexData.push_back(v1.r);
                triangleVertexData.push_back(v1.g);
                triangleVertexData.push_back(v1.b);
                triangleVertexData.push_back(v1.a);
            }
            renderer_.setDebugTriangleDrawData(triangleVertexData);
        } else {
            // Clear debug draw data
            renderer_.setDebugLineDrawData({});
            renderer_.setDebugTriangleDrawData({});
        }

        // Pop the scene after Lua execution is complete
        if (pendingPop_) {
            uint64_t poppedSceneId = sceneStack_.top();
            // Cleanup the scene before popping
            luaInterface_->cleanupScene(poppedSceneId);
            sceneStack_.pop();
            pendingPop_ = false;
            // Mark as not initialized so it can be reinitialized if pushed again
            initializedScenes_.erase(poppedSceneId);

            // Switch to the new active scene's pipeline
            if (!sceneStack_.empty()) {
                uint64_t newActiveSceneId = sceneStack_.top();
                luaInterface_->switchToScenePipeline(newActiveSceneId);
            }
        }
    }

    return !sceneStack_.empty();
}

void SceneManager::handleAction(Action action) {
    if (!sceneStack_.empty()) {
        uint64_t activeSceneId = sceneStack_.top();
        luaInterface_->handleAction(activeSceneId, action);
    }
}

void SceneManager::setCursorPosition(float x, float y) {
    luaInterface_->setCursorPosition(x, y);
}

void SceneManager::setCameraOffset(float x, float y) {
    luaInterface_->setCameraOffset(x, y);
}

float SceneManager::getCameraOffsetX() const {
    return luaInterface_->getCameraOffsetX();
}

float SceneManager::getCameraOffsetY() const {
    return luaInterface_->getCameraOffsetY();
}

float SceneManager::getCameraZoom() const {
    return luaInterface_->getCameraZoom();
}

void SceneManager::applyScrollZoom(float scrollDelta) {
    luaInterface_->applyScrollZoom(scrollDelta);
}

void SceneManager::setParticleEditorActive(bool active, int pipelineId) {
    particleEditorActive_ = active;
    particleEditorPipelineId_ = pipelineId;
}

bool SceneManager::isParticleEditorActive() const {
    return particleEditorActive_;
}

int SceneManager::getParticleEditorPipelineId() const {
    return particleEditorPipelineId_;
}