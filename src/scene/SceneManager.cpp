#include "SceneManager.h"
#include "LuaInterface.h"
#include "SceneLayer.h"
#include "../core/TrigLookup.h"
#include "../core/hash.h"
#include "../effects/ParticleSystem.h"
#include "../physics/Box2DPhysics.h"
#include "../audio/AudioManager.h"
#include "../effects/WaterEffect.h"
#include "../animation/AnimationEngine.h"
#include "../debug/ConsoleBuffer.h"
#include "../debug/ThreadProfiler.h"
#include <SDL3/SDL.h>
#include <cassert>

// Default scene transition times (in seconds) - fade to/from black
static const float DEFAULT_FADE_OUT_TIME = 0.25f;  // 250ms fade-out
static const float DEFAULT_FADE_IN_TIME = 0.25f;   // 250ms fade-in

SceneManager::SceneManager(MemoryAllocator* allocator, PakResource& pakResource, VulkanRenderer& renderer,
                           Box2DPhysics* physics, SceneLayerManager* layerManager, AudioManager* audioManager,
                           ParticleSystemManager* particleManager, WaterEffectManager* waterEffectManager,
                           LuaInterface* luaInterface, ConsoleBuffer* consoleBuffer, TrigLookup* trigLookup,
                           AnimationEngine* animationEngine)
    : allocator_(allocator), pakResource_(pakResource), renderer_(renderer), physics_(physics), layerManager_(layerManager),
      audioManager_(audioManager), particleManager_(particleManager), waterEffectManager_(waterEffectManager),
      luaInterface_(luaInterface), animationEngine_(animationEngine), sceneStack_(*allocator, "SceneManager::sceneStack_"),
      loadedScenes_(*allocator, "SceneManager::loadedScenes_"),
      initializedScenes_(*allocator, "SceneManager::initializedScenes_"), pendingPop_(false),
      transitionState_(TRANSITION_NONE), transitionTimer_(0.0f), fadeOutTime_(DEFAULT_FADE_OUT_TIME), fadeInTime_(DEFAULT_FADE_IN_TIME),
      fadeColorR_(0.0f), fadeColorG_(0.0f), fadeColorB_(0.0f),
    fadePipelineReady_(false), fadeVertShaderId_(hashCString("res/shaders/fade_vertex.spv")), fadeFragShaderId_(hashCString("res/shaders/fade_fragment.spv")),
      pendingSceneId_(0), pendingScenePush_(false),
      particleEditorActive_(false), particleEditorPipelineId_(-1), editorPreviewSystemId_(-1),
            consoleBuffer_(consoleBuffer), trigLookup_(trigLookup),
            renderPrepThread_(nullptr), renderPrepMutex_(nullptr), renderPrepCondition_(nullptr),
            renderPrepWorkerRunning_(false), renderPrepRequestPending_(false), renderPrepCompleted_(false),
            renderPrepWriteIndex_(0), renderPrepReadyIndex_(-1),
            queuedCameraX_(0.0f), queuedCameraY_(0.0f), queuedCameraZoom_(1.0f)
{
    assert(allocator_ != nullptr);
    assert(physics_ != nullptr);
    assert(layerManager_ != nullptr);
    assert(audioManager_ != nullptr);
    assert(particleManager_ != nullptr);
    assert(waterEffectManager_ != nullptr);
    assert(luaInterface_ != nullptr);
    assert(trigLookup_ != nullptr);
    assert(animationEngine_ != nullptr);

    consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "SceneManager: Received all managers and LuaInterface from main.cpp");
    consoleBuffer_->log(SDL_LOG_PRIORITY_VERBOSE, "SceneManager: Default transition times: fadeOut=%.3fs, fadeIn=%.3fs", fadeOutTime_, fadeInTime_);

    renderPrepBuffers_[0] = (RenderPrepOutput*)allocator_->allocate(sizeof(RenderPrepOutput), "SceneManager::renderPrepBuffer0");
    new (renderPrepBuffers_[0]) RenderPrepOutput(*allocator_);
    renderPrepBuffers_[1] = (RenderPrepOutput*)allocator_->allocate(sizeof(RenderPrepOutput), "SceneManager::renderPrepBuffer1");
    new (renderPrepBuffers_[1]) RenderPrepOutput(*allocator_);

    renderPrepMutex_ = SDL_CreateMutex();
    renderPrepCondition_ = SDL_CreateCondition();
    renderPrepWorkerRunning_ = true;
    renderPrepThread_ = SDL_CreateThread(renderPrepWorkerThread, "RenderPrepWorker", this);
    assert(renderPrepMutex_ != nullptr);
    assert(renderPrepCondition_ != nullptr);
    assert(renderPrepThread_ != nullptr);

    // Load and create fade overlay pipeline
    consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "SceneManager: Loading fade overlay shaders");
    ensureFadePipelineReady();
}

SceneManager::~SceneManager() {
    if (renderPrepMutex_ != nullptr) {
        SDL_LockMutex(renderPrepMutex_);
        renderPrepWorkerRunning_ = false;
        SDL_SignalCondition(renderPrepCondition_);
        SDL_UnlockMutex(renderPrepMutex_);
    }

    if (renderPrepThread_ != nullptr) {
        SDL_WaitThread(renderPrepThread_, nullptr);
        renderPrepThread_ = nullptr;
    }

    if (renderPrepCondition_ != nullptr) {
        SDL_DestroyCondition(renderPrepCondition_);
        renderPrepCondition_ = nullptr;
    }

    if (renderPrepMutex_ != nullptr) {
        SDL_DestroyMutex(renderPrepMutex_);
        renderPrepMutex_ = nullptr;
    }

    if (renderPrepBuffers_[0] != nullptr) {
        renderPrepBuffers_[0]->~RenderPrepOutput();
        allocator_->free(renderPrepBuffers_[0]);
        renderPrepBuffers_[0] = nullptr;
    }
    if (renderPrepBuffers_[1] != nullptr) {
        renderPrepBuffers_[1]->~RenderPrepOutput();
        allocator_->free(renderPrepBuffers_[1]);
        renderPrepBuffers_[1] = nullptr;
    }
}

int SceneManager::renderPrepWorkerThread(void* data) {
    SceneManager* sceneManager = static_cast<SceneManager*>(data);
    assert(sceneManager != nullptr);

    ThreadProfiler& profiler = ThreadProfiler::instance();
    profiler.registerThread("RenderPrepWorker");

    while (true) {
        profiler.updateThreadState(THREAD_STATE_WAITING);

        SDL_LockMutex(sceneManager->renderPrepMutex_);
        while (sceneManager->renderPrepWorkerRunning_ && !sceneManager->renderPrepRequestPending_) {
            SDL_WaitCondition(sceneManager->renderPrepCondition_, sceneManager->renderPrepMutex_);
        }

        if (!sceneManager->renderPrepWorkerRunning_ && !sceneManager->renderPrepRequestPending_) {
            SDL_UnlockMutex(sceneManager->renderPrepMutex_);
            break;
        }

        const float cameraX = sceneManager->queuedCameraX_;
        const float cameraY = sceneManager->queuedCameraY_;
        const float cameraZoom = sceneManager->queuedCameraZoom_;
        const int writeIndex = sceneManager->renderPrepWriteIndex_;
        sceneManager->renderPrepRequestPending_ = false;
        SDL_UnlockMutex(sceneManager->renderPrepMutex_);

        profiler.updateThreadState(THREAD_STATE_BUSY);

        RenderPrepOutput* output = sceneManager->renderPrepBuffers_[writeIndex];
        output->spriteBatches.clear();
        output->particleBatches.clear();

        SceneLayerManager& layerManager = sceneManager->luaInterface_->getSceneLayerManager();
        layerManager.updateLayerVertices(output->spriteBatches, cameraX, cameraY, cameraZoom);
        sceneManager->buildParticleBatches(output->particleBatches);

        SDL_LockMutex(sceneManager->renderPrepMutex_);
        sceneManager->renderPrepReadyIndex_ = writeIndex;
        sceneManager->renderPrepWriteIndex_ = 1 - writeIndex;
        sceneManager->renderPrepCompleted_ = true;
        SDL_SignalCondition(sceneManager->renderPrepCondition_);
        SDL_UnlockMutex(sceneManager->renderPrepMutex_);
    }

    return 0;
}

void SceneManager::submitRenderPrepJob(float cameraX, float cameraY, float cameraZoom) {
    SDL_LockMutex(renderPrepMutex_);
    queuedCameraX_ = cameraX;
    queuedCameraY_ = cameraY;
    queuedCameraZoom_ = cameraZoom;
    renderPrepRequestPending_ = true;
    renderPrepCompleted_ = false;
    SDL_SignalCondition(renderPrepCondition_);
    SDL_UnlockMutex(renderPrepMutex_);
}

int SceneManager::waitForRenderPrepJob() {
    SDL_LockMutex(renderPrepMutex_);
    while (!renderPrepCompleted_) {
        SDL_WaitCondition(renderPrepCondition_, renderPrepMutex_);
    }
    int readyIndex = renderPrepReadyIndex_;
    renderPrepCompleted_ = false;
    SDL_UnlockMutex(renderPrepMutex_);
    return readyIndex;
}

void SceneManager::buildParticleBatches(Vector<ParticleBatch>& particleBatches) {
    particleBatches.clear();

    ParticleSystemManager& particleManager = luaInterface_->getParticleSystemManager();
    for (int i = 0; i < particleManager.getSystemCount(); ++i) {
        ParticleSystem* system = &particleManager.getSystems()[i];
        if (!system || system->liveParticleCount == 0) continue;

        uint64_t textureId = 0;
        if (system->config.textureCount > 0) {
            AtlasUV atlasUV;
            if (pakResource_.tryGetAtlasUV(system->config.textureIds[0], atlasUV)) {
                textureId = atlasUV.atlasId;
            } else {
                textureId = system->config.textureIds[0];
            }
        }

        ParticleBatch batch(particleBatches.getAllocator());
        batch.textureId = textureId;
        batch.pipelineId = system->pipelineId;
        batch.parallaxDepth = system->parallaxDepth;

        AtlasUV cachedAtlasUVs[8];
        bool cachedAtlasUVValid[8] = {false, false, false, false, false, false, false, false};
        if (system->config.textureCount > 0) {
            for (int t = 0; t < system->config.textureCount && t < 8; ++t) {
                cachedAtlasUVValid[t] = pakResource_.tryGetAtlasUV(system->config.textureIds[t], cachedAtlasUVs[t]);
            }
        }

        for (int p = 0; p < system->liveParticleCount; ++p) {
            float x = system->posX[p];
            float y = system->posY[p];
            float size = system->size[p];
            float halfSize = size * 0.5f;

            float texU0 = 0.0f, texV0 = 0.0f, texU1 = 1.0f, texV1 = 1.0f;
            if (system->config.textureCount > 0) {
                int texIdx = system->textureIndex[p];
                if (texIdx >= 0 && texIdx < system->config.textureCount) {
                    if (cachedAtlasUVValid[texIdx]) {
                        const AtlasUV& atlasUV = cachedAtlasUVs[texIdx];
                        texU0 = atlasUV.u0;
                        texV0 = atlasUV.v0;
                        texU1 = atlasUV.u1;
                        texV1 = atlasUV.v1;
                    }
                }
            }

            float lifeRatio = 1.0f - (system->lifetime[p] / system->totalLifetime[p]);
            float rotZ = system->rotZ[p];

            ParticleInstance instance;
            instance.x = x;
            instance.y = y;
            instance.halfSize = halfSize;
            instance.rotZ = rotZ;
            instance.startR = system->colorR[p];
            instance.startG = system->colorG[p];
            instance.startB = system->colorB[p];
            instance.startA = system->colorA[p];
            instance.endR = system->endColorR[p];
            instance.endG = system->endColorG[p];
            instance.endB = system->endColorB[p];
            instance.endA = system->endColorA[p];
            instance.lifeRatio = lifeRatio;
            instance.uvMinX = texU0;
            instance.uvMinY = texV0;
            instance.uvMaxX = texU1;
            instance.uvMaxY = texV1;
            batch.instances.push_back(instance);
        }

        if (!batch.instances.empty()) {
            particleBatches.push_back(batch);
        }
    }
}

void SceneManager::pushScene(uint64_t sceneId) {
    // If we're not currently in a transition and there's an active scene, start fade-out
    if (transitionState_ == TRANSITION_NONE && !sceneStack_.empty()) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "SceneManager: Starting fade-out transition for scene push");
        transitionState_ = TRANSITION_FADE_OUT;
        transitionTimer_ = 0.0f;
        pendingSceneId_ = sceneId;
        pendingScenePush_ = true;
        return;
    }

    // If no current scene or transition is completing, push immediately
    consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "SceneManager: Pushing scene %llu", (unsigned long long)sceneId);

    // Load the scene if not already loaded
    if (!loadedScenes_.contains(sceneId)) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "Loading scene %llu", (unsigned long long)sceneId);
        pakResource_.requestResourceAsync(sceneId);
        ResourceData sceneScript{nullptr, 0, 0};
        bool ready = pakResource_.tryGetResource(sceneId, sceneScript);
        assert(ready);
        luaInterface_->loadScene(sceneId, sceneScript);
        loadedScenes_.insert(sceneId);
    } else {
        consoleBuffer_->log(SDL_LOG_PRIORITY_VERBOSE, "Scene %llu already loaded (cache hit)", (unsigned long long)sceneId);
    }

    // Push scene onto stack
    sceneStack_.push(sceneId);

    // Initialize the scene if not already initialized
    if (!initializedScenes_.contains(sceneId)) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "Initializing scene %llu", (unsigned long long)sceneId);
        luaInterface_->initScene(sceneId);
        initializedScenes_.insert(sceneId);
    }

    // Set the pipelines for this scene
    luaInterface_->switchToScenePipeline(sceneId);

    // Start fade-in transition
    consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "SceneManager: Starting fade-in transition");
    transitionState_ = TRANSITION_FADE_IN;
    transitionTimer_ = 0.0f;
    pendingScenePush_ = false;
}

void SceneManager::popScene() {
    if (!sceneStack_.empty()) {
        // If we're not in a transition, start fade-out
        if (transitionState_ == TRANSITION_NONE) {
            consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "SceneManager: Starting fade-out transition for scene pop");
            transitionState_ = TRANSITION_FADE_OUT;
            transitionTimer_ = 0.0f;
            pendingPop_ = true;
        } else {
            // Already transitioning, just mark for pop
            pendingPop_ = true;
        }
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
        pakResource_.requestResourceAsync(currentSceneId);
        ResourceData sceneScript{nullptr, 0, 0};
        bool ready = pakResource_.tryGetResource(currentSceneId, sceneScript);
        assert(ready);
        luaInterface_->loadScene(currentSceneId, sceneScript);
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
    ensureFadePipelineReady();

    // Update transition state first
    updateTransition(deltaTime);

    if (!sceneStack_.empty()) {
        uint64_t activeSceneId = sceneStack_.top();
        luaInterface_->updateScene(activeSceneId, deltaTime);

        // Update animations
        animationEngine_->update(deltaTime);

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

        // Capture camera transform for render-prep job
        float cameraX = luaInterface_->getCameraOffsetX();
        float cameraY = luaInterface_->getCameraOffsetY();
        float cameraZoom = luaInterface_->getCameraZoom();

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

        // Kick render-prep worker (sprite + particle batches)
        submitRenderPrepJob(cameraX, cameraY, cameraZoom);

        // Update debug draw data if physics debug drawing is enabled
#ifdef DEBUG
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
            for (uint64_t i = 0; i < debugTriangleVerts.size(); i += 3) {
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
#endif // DEBUG

        // Start async physics step (requested by Lua via b2StepAsync) after frame physics reads.
        luaInterface_->submitPendingAsyncPhysicsStep();

        // Wait for render-prep output and submit to renderer
        int readyBuffer = waitForRenderPrepJob();
        if (readyBuffer >= 0 && readyBuffer < 2) {
            renderer_.setSpriteBatches(renderPrepBuffers_[readyBuffer]->spriteBatches);
            renderer_.setParticleBatches(renderPrepBuffers_[readyBuffer]->particleBatches);
        }
    }

    return !sceneStack_.empty();
}

void SceneManager::ensureFadePipelineReady() {
    if (fadePipelineReady_) {
        return;
    }

    pakResource_.requestResourceAsync(fadeVertShaderId_);
    pakResource_.requestResourceAsync(fadeFragShaderId_);

    ResourceData fadeVertShader{nullptr, 0, 0};
    ResourceData fadeFragShader{nullptr, 0, 0};
    bool haveFadeVert = pakResource_.tryGetResource(fadeVertShaderId_, fadeVertShader);
    bool haveFadeFrag = pakResource_.tryGetResource(fadeFragShaderId_, fadeFragShader);

    if (!haveFadeVert || !haveFadeFrag) {
        return;
    }

    renderer_.createFadePipeline(fadeVertShader, fadeFragShader);
    fadePipelineReady_ = true;
    consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "SceneManager: Fade overlay pipeline ready");
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

void SceneManager::setTransitionFadeTime(float fadeOutTime, float fadeInTime) {
    assert(fadeOutTime >= 0.0f);
    assert(fadeInTime >= 0.0f);
    fadeOutTime_ = fadeOutTime;
    fadeInTime_ = fadeInTime;
    consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "SceneManager: Transition times set: fadeOut=%.3fs, fadeIn=%.3fs", fadeOutTime_, fadeInTime_);
}

void SceneManager::setTransitionColor(float r, float g, float b) {
    assert(r >= 0.0f && r <= 1.0f);
    assert(g >= 0.0f && g <= 1.0f);
    assert(b >= 0.0f && b <= 1.0f);
    fadeColorR_ = r;
    fadeColorG_ = g;
    fadeColorB_ = b;
    consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "SceneManager: Transition color set: RGB(%.3f, %.3f, %.3f)", r, g, b);
}

void SceneManager::updateTransition(float deltaTime) {
    if (transitionState_ == TRANSITION_NONE) {
        return;
    }

    transitionTimer_ += deltaTime;

    if (transitionState_ == TRANSITION_FADE_OUT) {
        float fadeProgress = (fadeOutTime_ > 0.0f) ? (transitionTimer_ / fadeOutTime_) : 1.0f;
        if (fadeProgress >= 1.0f) {
            fadeProgress = 1.0f;
        }

        // Set fade overlay with fade progress (0 = no overlay, 1 = full overlay)
        renderer_.setFadeOverlay(fadeColorR_, fadeColorG_, fadeColorB_, fadeProgress);

        if (fadeProgress >= 1.0f) {
            // Fade-out complete
            consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "SceneManager: Fade-out complete");

            // Handle pending scene change
            if (pendingScenePush_) {
                // Complete the push operation
                uint64_t sceneId = pendingSceneId_;
                consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "SceneManager: Pushing scene %llu after fade-out", (unsigned long long)sceneId);

                // Load the scene if not already loaded
                if (!loadedScenes_.contains(sceneId)) {
                    consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "Loading scene %llu", (unsigned long long)sceneId);
                    pakResource_.requestResourceAsync(sceneId);
                    ResourceData sceneScript{nullptr, 0, 0};
                    bool ready = pakResource_.tryGetResource(sceneId, sceneScript);
                    assert(ready);
                    luaInterface_->loadScene(sceneId, sceneScript);
                    loadedScenes_.insert(sceneId);
                } else {
                    consoleBuffer_->log(SDL_LOG_PRIORITY_VERBOSE, "Scene %llu already loaded (cache hit)", (unsigned long long)sceneId);
                }

                // Push scene onto stack
                sceneStack_.push(sceneId);

                // Initialize the scene if not already initialized
                if (!initializedScenes_.contains(sceneId)) {
                    consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "Initializing scene %llu", (unsigned long long)sceneId);
                    luaInterface_->initScene(sceneId);
                    initializedScenes_.insert(sceneId);
                }

                // Set the pipelines for this scene
                luaInterface_->switchToScenePipeline(sceneId);

                pendingScenePush_ = false;
            }

            if (pendingPop_) {
                // Complete the pop operation
                uint64_t poppedSceneId = sceneStack_.top();
                consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "SceneManager: Popping scene %llu after fade-out", (unsigned long long)poppedSceneId);

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

            // Start fade-in
            transitionState_ = TRANSITION_FADE_IN;
            transitionTimer_ = 0.0f;
            consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "SceneManager: Starting fade-in transition");
        }
    } else if (transitionState_ == TRANSITION_FADE_IN) {
        float fadeProgress = (fadeInTime_ > 0.0f) ? (transitionTimer_ / fadeInTime_) : 1.0f;
        if (fadeProgress >= 1.0f) {
            fadeProgress = 1.0f;
        }

        // Set fade overlay with fade progress (1 = full overlay, 0 = no overlay)
        float invProgress = 1.0f - fadeProgress;
        renderer_.setFadeOverlay(fadeColorR_, fadeColorG_, fadeColorB_, invProgress);

        if (fadeProgress >= 1.0f) {
            // Fade-in complete
            consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "SceneManager: Fade-in complete, transition finished");
            transitionState_ = TRANSITION_NONE;
            transitionTimer_ = 0.0f;
            // Ensure fade overlay is fully transparent
            renderer_.setFadeOverlay(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }
}