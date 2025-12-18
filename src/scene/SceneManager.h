#pragma once

#include <stack>
#include <unordered_set>
#include "../resources/resource.h"
#include "../vulkan/VulkanRenderer.h"
#include "../input/InputActions.h"
#include "../input/VibrationManager.h"

class LuaInterface;
class MemoryAllocator;
class Box2DPhysics;
class SceneLayerManager;
class AudioManager;
class ParticleSystemManager;
class WaterEffectManager;

class SceneManager {
public:
    SceneManager(PakResource& pakResource, VulkanRenderer& renderer,
                 Box2DPhysics* physics, SceneLayerManager* layerManager, AudioManager* audioManager,
                 ParticleSystemManager* particleManager, WaterEffectManager* waterEffectManager,
                 LuaInterface* luaInterface);
    ~SceneManager();

    // Scene management
    void pushScene(uint64_t sceneId);
    void popScene();
    bool isEmpty() const;
    uint64_t getActiveSceneId() const;
    void reloadCurrentScene();

    // Active scene operations
    void initActiveScene();
    bool updateActiveScene(float deltaTime);
    void handleAction(Action action);

    // Cursor position for drag operations (in world coordinates)
    void setCursorPosition(float x, float y);

    // Camera control (in world coordinates)
    void setCameraOffset(float x, float y);
    float getCameraOffsetX() const;
    float getCameraOffsetY() const;
    float getCameraZoom() const;
    void applyScrollZoom(float scrollDelta);

    // Particle editor support (DEBUG only)
    void setParticleEditorActive(bool active, int pipelineId);
    bool isParticleEditorActive() const;
    int getParticleEditorPipelineId() const;
    void setEditorPreviewSystemId(int systemId);
    int getEditorPreviewSystemId() const;

    // Access to PakResource for texture list
    PakResource& getPakResource() { return pakResource_; }

private:
    PakResource& pakResource_;
    VulkanRenderer& renderer_;
    Box2DPhysics* physics_;
    SceneLayerManager* layerManager_;
    AudioManager* audioManager_;
    ParticleSystemManager* particleManager_;
    WaterEffectManager* waterEffectManager_;
    LuaInterface* luaInterface_;
    std::stack<uint64_t> sceneStack_;
    std::unordered_set<uint64_t> loadedScenes_;
    std::unordered_set<uint64_t> initializedScenes_;
    bool pendingPop_;

    // Particle editor state
    bool particleEditorActive_;
    int particleEditorPipelineId_;
    int editorPreviewSystemId_;
};