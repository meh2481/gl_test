#pragma once

#include <stack>
#include <unordered_set>
#include <memory>
#include "resource.h"
#include "VulkanRenderer.h"
#include "InputActions.h"
#include "VibrationManager.h"

class LuaInterface;

class SceneManager {
public:
    SceneManager(PakResource& pakResource, VulkanRenderer& renderer, VibrationManager* vibrationManager = nullptr);
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

    // Access to LuaInterface for particle system manager
    LuaInterface* getLuaInterface() { return luaInterface_.get(); }

    // Access to PakResource for texture list
    PakResource& getPakResource() { return pakResource_; }

private:
    PakResource& pakResource_;
    VulkanRenderer& renderer_;
    std::unique_ptr<LuaInterface> luaInterface_;
    std::stack<uint64_t> sceneStack_;
    std::unordered_set<uint64_t> loadedScenes_;
    std::unordered_set<uint64_t> initializedScenes_;
    bool pendingPop_;

    // Particle editor state
    bool particleEditorActive_;
    int particleEditorPipelineId_;
};