#pragma once

#include "../core/Stack.h"
#include "../core/HashSet.h"
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
class ConsoleBuffer;
class TrigLookup;
class AnimationEngine;

class SceneManager {
public:
    SceneManager(MemoryAllocator* allocator, PakResource& pakResource, VulkanRenderer& renderer,
                 Box2DPhysics* physics, SceneLayerManager* layerManager, AudioManager* audioManager,
                 ParticleSystemManager* particleManager, WaterEffectManager* waterEffectManager,
                 LuaInterface* luaInterface, ConsoleBuffer* consoleBuffer, TrigLookup* trigLookup,
                 AnimationEngine* animationEngine);
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

    // Transition configuration
    void setTransitionFadeTime(float fadeOutTime, float fadeInTime);
    void setTransitionColor(float r, float g, float b);

private:
    // Transition state machine
    enum TransitionState {
        TRANSITION_NONE,
        TRANSITION_FADE_OUT,
        TRANSITION_FADE_IN
    };

    void updateTransition(float deltaTime);

    TransitionState transitionState_;
    float transitionTimer_;
    float fadeOutTime_;
    float fadeInTime_;
    float fadeColorR_;
    float fadeColorG_;
    float fadeColorB_;
    uint64_t pendingSceneId_;
    bool pendingScenePush_;
    MemoryAllocator* allocator_;
    PakResource& pakResource_;
    VulkanRenderer& renderer_;
    Box2DPhysics* physics_;
    SceneLayerManager* layerManager_;
    AudioManager* audioManager_;
    ParticleSystemManager* particleManager_;
    WaterEffectManager* waterEffectManager_;
    LuaInterface* luaInterface_;
    AnimationEngine* animationEngine_;
    Stack<uint64_t> sceneStack_;
    HashSet<uint64_t> loadedScenes_;
    HashSet<uint64_t> initializedScenes_;
    bool pendingPop_;

    // Particle editor state
    bool particleEditorActive_;
    int particleEditorPipelineId_;
    int editorPreviewSystemId_;

    // Console buffer for logging (optional, may be nullptr)
    ConsoleBuffer* consoleBuffer_;

    // Trig lookup table for fast sin/cos calculations
    TrigLookup* trigLookup_;
};