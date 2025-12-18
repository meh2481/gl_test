#pragma once

#include <lua.hpp>
#include <memory>
#include "../resources/resource.h"
#include "../vulkan/VulkanRenderer.h"
#include "../physics/Box2DPhysics.h"
#include "SceneLayer.h"
#include "../effects/ParticleSystem.h"
#include "../input/InputActions.h"
#include "../audio/AudioManager.h"
#include "../input/VibrationManager.h"
#include "../effects/WaterEffect.h"
#include "../core/String.h"
#include "../core/Vector.h"
#include "../core/HashTable.h"
#include "../memory/MemoryAllocator.h"

class SceneManager;
class ConsoleBuffer;

class LuaInterface {
public:
    LuaInterface(PakResource& pakResource, VulkanRenderer& renderer, MemoryAllocator* allocator,
                 Box2DPhysics* physics, SceneLayerManager* layerManager, AudioManager* audioManager,
                 ParticleSystemManager* particleManager, WaterEffectManager* waterEffectManager,
                 SceneManager* sceneManager, VibrationManager* vibrationManager, ConsoleBuffer* consoleBuffer);
    ~LuaInterface();

    // Execute a Lua script from ResourceData
    void executeScript(const ResourceData& scriptData);

    // Scene management functions
    void loadScene(uint64_t sceneId, const ResourceData& scriptData);
    void initScene(uint64_t sceneId);
    void updateScene(uint64_t sceneId, float deltaTime);
    void handleAction(uint64_t sceneId, Action action);
    void cleanupScene(uint64_t sceneId);
    void switchToScenePipeline(uint64_t sceneId);
    void clearScenePipelines(uint64_t sceneId);

    // Physics access
    Box2DPhysics& getPhysics() { return *physics_; }

    // Scene layer access
    SceneLayerManager& getSceneLayerManager() { return *layerManager_; }
    // Handle sensor events for splash particles
    void handleSensorEvent(const SensorEvent& event);

    // Audio access
    AudioManager& getAudioManager() { return *audioManager_; }

    // Particle system access
    ParticleSystemManager& getParticleSystemManager() { return *particleManager_; }
    int getParticleEditorPipelineId(int blendMode) const {
        if (blendMode >= 0 && blendMode < 3) {
            return particleEditorPipelineIds_[blendMode];
        }
        return particleEditorPipelineIds_[0]; // Default to additive
    }

    // Water effect access
    WaterEffectManager& getWaterEffectManager() { return *waterEffectManager_; }

    // Access to stringAllocator for temporary allocations
    MemoryAllocator* getStringAllocator() { return stringAllocator_; }

    // Set scene manager (called after SceneManager is created)
    void setSceneManager(SceneManager* sceneManager) { sceneManager_ = sceneManager; }

    // Cursor position for drag operations (in world coordinates)
    void setCursorPosition(float x, float y) { cursorX_ = x; cursorY_ = y; }
    float getCursorX() const { return cursorX_; }
    float getCursorY() const { return cursorY_; }

    // Camera pan/zoom (in world coordinates)
    void setCameraOffset(float x, float y) { cameraOffsetX_ = x; cameraOffsetY_ = y; }
    float getCameraOffsetX() const { return cameraOffsetX_; }
    float getCameraOffsetY() const { return cameraOffsetY_; }
    void setCameraZoom(float zoom) { cameraZoom_ = zoom; }
    float getCameraZoom() const { return cameraZoom_; }
    void applyScrollZoom(float scrollDelta);

private:
    // Lua-callable functions
    static int loadShaders(lua_State* L);
    static int pushScene(lua_State* L);
    static int popScene(lua_State* L);
    static int luaPrint(lua_State* L);

    // Box2D Lua bindings
    static int b2SetGravity(lua_State* L);
    static int b2Step(lua_State* L);
    static int b2CreateBody(lua_State* L);
    static int b2DestroyBody(lua_State* L);
    static int b2AddBoxFixture(lua_State* L);
    static int b2AddCircleFixture(lua_State* L);
    static int b2AddPolygonFixture(lua_State* L);
    static int b2AddSegmentFixture(lua_State* L);
    static int b2ClearAllFixtures(lua_State* L);
    static int b2SetBodyPosition(lua_State* L);
    static int b2SetBodyAngle(lua_State* L);
    static int b2SetBodyLinearVelocity(lua_State* L);
    static int b2SetBodyAngularVelocity(lua_State* L);
    static int b2SetBodyAwake(lua_State* L);
    static int b2EnableBody(lua_State* L);
    static int b2DisableBody(lua_State* L);
    static int b2GetBodyPosition(lua_State* L);
    static int b2GetBodyAngle(lua_State* L);
    static int b2EnableDebugDraw(lua_State* L);
    static int b2CreateRevoluteJoint(lua_State* L);
    static int b2DestroyJoint(lua_State* L);
    static int b2QueryBodyAtPoint(lua_State* L);
    static int b2CreateMouseJoint(lua_State* L);
    static int b2UpdateMouseJointTarget(lua_State* L);
    static int b2DestroyMouseJoint(lua_State* L);
    static int b2SetBodyDestructible(lua_State* L);
    static int b2SetBodyDestructibleLayer(lua_State* L);
    static int b2ClearBodyDestructible(lua_State* L);
    static int b2CleanupAllFragments(lua_State* L);

    // Body type system Lua bindings
    static int b2AddBodyType(lua_State* L);
    static int b2RemoveBodyType(lua_State* L);
    static int b2ClearBodyTypes(lua_State* L);
    static int b2BodyHasType(lua_State* L);
    static int b2GetBodyTypes(lua_State* L);
    static int b2SetCollisionCallback(lua_State* L);

    // Force field Lua bindings
    static int createForceField(lua_State* L);
    static int createRadialForceField(lua_State* L);
    static int getForceFieldBodyId(lua_State* L);

    // Scene layer Lua bindings
    static int createLayer(lua_State* L);
    static int destroyLayer(lua_State* L);
    static int attachLayerToBody(lua_State* L);
    static int setLayerOffset(lua_State* L);
    static int setLayerUseLocalUV(lua_State* L);
    static int setLayerPosition(lua_State* L);
    static int setLayerParallaxDepth(lua_State* L);
    static int setLayerScale(lua_State* L);

    // Layer animation Lua bindings
    static int setLayerSpin(lua_State* L);
    static int setLayerBlink(lua_State* L);
    static int setLayerWave(lua_State* L);
    static int setLayerColor(lua_State* L);
    static int setLayerColorCycle(lua_State* L);

    // Texture loading
    static int loadTexture(lua_State* L);
    static int loadTexturedShaders(lua_State* L);
    static int loadTexturedShadersEx(lua_State* L);
    static int loadTexturedShadersAdditive(lua_State* L);
    static int loadAnimTexturedShaders(lua_State* L);
    static int setShaderParameters(lua_State* L);

    // Audio Lua bindings
    static int audioLoadOpus(lua_State* L);
    static int audioCreateSource(lua_State* L);
    static int audioPlaySource(lua_State* L);
    static int audioSetSourcePosition(lua_State* L);
    static int audioSetListenerPosition(lua_State* L);
    static int audioSetListenerOrientation(lua_State* L);
    static int audioSetGlobalVolume(lua_State* L);
    static int audioSetGlobalEffect(lua_State* L);

    // Cursor position Lua bindings
    static int getCursorPosition(lua_State* L);

    // Camera Lua bindings
    static int setCameraOffset(lua_State* L);
    static int setCameraZoom(lua_State* L);

    // Light management Lua bindings
    static int addLight(lua_State* L);
    static int updateLight(lua_State* L);
    static int removeLight(lua_State* L);
    static int setAmbientLight(lua_State* L);

    // Particle system Lua bindings
    static int createParticleSystem(lua_State* L);
    static int destroyParticleSystem(lua_State* L);
    static int setParticleSystemPosition(lua_State* L);
    static int loadParticleShaders(lua_State* L);

    // Particle editor Lua bindings (DEBUG only)
    static int openParticleEditor(lua_State* L);

    // Resource loading Lua bindings
    static int loadParticleConfig(lua_State* L);
    static int loadObject(lua_State* L);

    // Node Lua bindings
    static int createNode(lua_State* L);
    static int destroyNode(lua_State* L);
    static int getNodePosition(lua_State* L);

    void registerFunctions();

    PakResource& pakResource_;
    VulkanRenderer& renderer_;
    lua_State* luaState_;
    SceneManager* sceneManager_;

    // Console buffer for logging (optional, may be nullptr)
    ConsoleBuffer* consoleBuffer_;

    int pipelineIndex_;
    uint64_t currentSceneId_;
    HashTable<uint64_t, Vector<std::pair<int, int>>* > scenePipelines_; // pipelineId, zIndex
    Box2DPhysics* physics_;
    SceneLayerManager* layerManager_;
    AudioManager* audioManager_;
    ParticleSystemManager* particleManager_;
    WaterEffectManager* waterEffectManager_;
    VibrationManager* vibrationManager_;
    float cursorX_;
    float cursorY_;
    float cameraOffsetX_;
    float cameraOffsetY_;
    float cameraZoom_;

    // Water field to shader pipeline mapping for splash ripples
    HashTable<int, int> waterFieldShaderMap_; // waterFieldId -> pipelineId

    // Water visual setup helper (called from createForceField when water=true)
    void setupWaterVisuals(int physicsForceFieldId, int waterFieldId,
                           float minX, float minY, float maxX, float maxY,
                           float alpha, float rippleAmplitude, float rippleSpeed);

    // Particle editor state (DEBUG only)
    int particleEditorPipelineIds_[3]; // [0]=additive, [1]=alpha, [2]=subtractive

    // Scene objects tracking - objects created via loadObject are tracked here
    // The C++ side calls update/cleanup on these automatically
    Vector<int> sceneObjects_; // Lua registry references to object tables

    // Node system
    struct Node {
        Node() : bodyId(-1), name(nullptr), centerX(0.0f), centerY(0.0f),
                 luaCallbackRef(LUA_NOREF), updateFuncRef(LUA_NOREF), onEnterFuncRef(LUA_NOREF) {}
        Node(MemoryAllocator* allocator) : bodyId(-1), name(allocator), centerX(0.0f), centerY(0.0f),
                 luaCallbackRef(LUA_NOREF), updateFuncRef(LUA_NOREF), onEnterFuncRef(LUA_NOREF) {}
        int bodyId;           // Physics sensor body ID
        String name;          // Node name for lookup
        float centerX;        // Center position X
        float centerY;        // Center position Y
        int luaCallbackRef;   // Lua registry reference to callback table (or LUA_NOREF)
        int updateFuncRef;    // Lua registry reference to update function (or LUA_NOREF)
        int onEnterFuncRef;   // Lua registry reference to onEnter function (or LUA_NOREF)
    };
    HashTable<int, Node*> nodes_; // nodeId -> Node
    HashTable<int, int> bodyToNodeMap_; // bodyId -> nodeId
    int nextNodeId_;

    // Memory allocator for string operations
    MemoryAllocator* stringAllocator_;

    void updateNodes(float deltaTime);
    void handleNodeSensorEvent(const SensorEvent& event);
};