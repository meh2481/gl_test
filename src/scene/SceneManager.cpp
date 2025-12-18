#include "SceneManager.h"
#include "LuaInterface.h"
#include "SceneLayer.h"
#include "../effects/ParticleSystem.h"
#include "../physics/Box2DPhysics.h"
#include "../audio/AudioManager.h"
#include "../effects/WaterEffect.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <cmath>

SceneManager::SceneManager(PakResource& pakResource, VulkanRenderer& renderer,
                           Box2DPhysics* physics, SceneLayerManager* layerManager, AudioManager* audioManager,
                           ParticleSystemManager* particleManager, WaterEffectManager* waterEffectManager,
                           LuaInterface* luaInterface)
    : pakResource_(pakResource), renderer_(renderer), physics_(physics), layerManager_(layerManager),
      audioManager_(audioManager), particleManager_(particleManager), waterEffectManager_(waterEffectManager),
      luaInterface_(luaInterface), pendingPop_(false), particleEditorActive_(false),
      particleEditorPipelineId_(-1), editorPreviewSystemId_(-1) {
    assert(physics_ != nullptr);
    assert(layerManager_ != nullptr);
    assert(audioManager_ != nullptr);
    assert(particleManager_ != nullptr);
    assert(waterEffectManager_ != nullptr);
    assert(luaInterface_ != nullptr);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SceneManager: Received all managers and LuaInterface from main.cpp");
}

SceneManager::~SceneManager() {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SceneManager: Destructor (managers owned by main.cpp)");
}

void SceneManager::pushScene(uint64_t sceneId) {
    // Load the scene if not already loaded
    if (loadedScenes_.find(sceneId) == loadedScenes_.end()) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading scene %llu", (unsigned long long)sceneId);
        ResourceData sceneScript = pakResource_.getResource(sceneId);
        luaInterface_->loadScene(sceneId, sceneScript);
        loadedScenes_.insert(sceneId);
    } else {
        SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Scene %llu already loaded (cache hit)", (unsigned long long)sceneId);
    }

    // Push scene onto stack
    sceneStack_.push(sceneId);

    // Initialize the scene if not already initialized
    if (initializedScenes_.find(sceneId) == initializedScenes_.end()) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Initializing scene %llu", (unsigned long long)sceneId);
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
        const auto& layers = layerManager.getLayers();
        for (auto it = layers.begin(); it != layers.end(); ++it) {
            const SceneLayer& layer = it.value();
            if (layer.physicsBodyId >= 0) {
                float bodyX = physics.getBodyPositionX(layer.physicsBodyId);
                float bodyY = physics.getBodyPositionY(layer.physicsBodyId);
                float bodyAngle = physics.getBodyAngle(layer.physicsBodyId);
                layerManager.updateLayerTransform(it.key(), bodyX, bodyY, bodyAngle);
            }
        }

        // Generate sprite batches grouped by texture
        Vector<SpriteBatch> spriteBatches(*luaInterface_->getStringAllocator(), "SceneManager::render::spriteBatches");
        float cameraX = luaInterface_->getCameraOffsetX();
        float cameraY = luaInterface_->getCameraOffsetY();
        float cameraZoom = luaInterface_->getCameraZoom();
        layerManager.updateLayerVertices(spriteBatches, cameraX, cameraY, cameraZoom);

        // Send batches to renderer
        renderer_.setSpriteBatches(spriteBatches);

        // Update and render particle systems
        ParticleSystemManager& particleManager = luaInterface_->getParticleSystemManager();
        particleManager.update(deltaTime);

        // Auto-cleanup systems with expired lifetime and no particles
        // Skip the editor preview system if editor is active
        int systemsToDestroy[64];
        int destroyCount = 0;
        particleManager.getSystemsToDestroy(systemsToDestroy, &destroyCount, 64);
        for (int i = 0; i < destroyCount; ++i) {
            // Don't auto-destroy the editor's preview system
            if (particleEditorActive_ && systemsToDestroy[i] == editorPreviewSystemId_) {
                continue;
            }
            particleManager.destroySystem(systemsToDestroy[i]);
        }

        // Generate particle batches - one per particle system for proper parallax sorting
        Vector<ParticleBatch> particleBatches(*luaInterface_->getStringAllocator(), "SceneManager::render::particleBatches");

        for (int i = 0; i < particleManager.getSystemCount(); ++i) {
            ParticleSystem* system = &particleManager.getSystems()[i];
            if (!system || system->liveParticleCount == 0) continue;

            // Determine the texture ID for this system
            uint64_t textureId = 0;
            if (system->config.textureCount > 0) {
                AtlasUV atlasUV;
                if (pakResource_.getAtlasUV(system->config.textureIds[0], atlasUV)) {
                    textureId = atlasUV.atlasId;
                } else {
                    textureId = system->config.textureIds[0];
                }
            }

            ParticleBatch batch(particleBatches.getAllocator());
            batch.textureId = textureId;
            batch.pipelineId = system->pipelineId;
            batch.parallaxDepth = system->parallaxDepth;

            for (int p = 0; p < system->liveParticleCount; ++p) {
                float x = system->posX[p];
                float y = system->posY[p];
                float size = system->size[p];
                float halfSize = size * 0.5f;

                // Get texture UV coordinates for this particle's texture
                float texU0 = 0.0f, texV0 = 0.0f, texU1 = 1.0f, texV1 = 1.0f;
                if (system->config.textureCount > 0) {
                    int texIdx = system->textureIndex[p];
                    if (texIdx >= 0 && texIdx < system->config.textureCount) {
                        AtlasUV atlasUV;
                        if (pakResource_.getAtlasUV(system->config.textureIds[texIdx], atlasUV)) {
                            texU0 = atlasUV.u0;
                            texV0 = atlasUV.v0;
                            texU1 = atlasUV.u1;
                            texV1 = atlasUV.v1;
                        }
                    }
                }

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

                uint16_t vertexBase = static_cast<uint16_t>(batch.vertices.size());

                for (int v = 0; v < 4; ++v) {
                    // Rotate
                    float rx = corners[v][0] * cosZ - corners[v][1] * sinZ;
                    float ry = corners[v][0] * sinZ + corners[v][1] * cosZ;

                    ParticleVertex vert;
                    vert.x = x + rx;
                    vert.y = y + ry;
                    vert.u = uvs[v][0];
                    vert.v = uvs[v][1];
                    vert.r = r;
                    vert.g = g;
                    vert.b = b;
                    vert.a = a;
                    vert.uvMinX = texU0;
                    vert.uvMinY = texV0;
                    vert.uvMaxX = texU1;
                    vert.uvMaxY = texV1;
                    batch.vertices.push_back(vert);
                }

                // Add indices for two triangles (quad)
                batch.indices.push_back(vertexBase + 0);
                batch.indices.push_back(vertexBase + 1);
                batch.indices.push_back(vertexBase + 2);
                batch.indices.push_back(vertexBase + 2);
                batch.indices.push_back(vertexBase + 3);
                batch.indices.push_back(vertexBase + 0);
            }

            if (!batch.vertices.empty()) {
                SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "SceneManager::updateActiveScene: adding ParticleBatch with %zu vertices", batch.vertices.size());
                particleBatches.push_back(batch);
            }
        }

        renderer_.setParticleBatches(particleBatches);

        // Update debug draw data if physics debug drawing is enabled
        if (physics.isDebugDrawEnabled()) {
            const Vector<DebugVertex>& debugLineVerts = physics.getDebugLineVertices();
            Vector<float> lineVertexData(*luaInterface_->getStringAllocator(), "SceneManager::render::lineVertexData");
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

            const Vector<DebugVertex>& debugTriangleVerts = physics.getDebugTriangleVertices();
            Vector<float> triangleVertexData(*luaInterface_->getStringAllocator(), "SceneManager::render::triangleVertexData");
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
            Vector<float> emptyData(*luaInterface_->getStringAllocator(), "SceneManager::render::emptyData");
            renderer_.setDebugLineDrawData(emptyData);
            renderer_.setDebugTriangleDrawData(emptyData);
        }

        // Pop the scene after Lua execution is complete
        if (pendingPop_) {
            uint64_t poppedSceneId = sceneStack_.top();
            // Cleanup the scene before popping
            luaInterface_->cleanupScene(poppedSceneId);
            // Clear the scene's pipelines so they can be re-registered on re-init
            luaInterface_->clearScenePipelines(poppedSceneId);
            sceneStack_.pop();
            pendingPop_ = false;
            // Mark as not initialized so it can be reinitialized if pushed again
            initializedScenes_.erase(poppedSceneId);

            // Deactivate particle editor when exiting a scene
            particleEditorActive_ = false;
            particleEditorPipelineId_ = -1;

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

void SceneManager::setEditorPreviewSystemId(int systemId) {
    editorPreviewSystemId_ = systemId;
}

int SceneManager::getEditorPreviewSystemId() const {
    return editorPreviewSystemId_;
}