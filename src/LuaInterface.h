#pragma once

#include <lua.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include "resource.h"
#include "VulkanRenderer.h"
#include "Box2DPhysics.h"
#include "SceneLayer.h"
#include "InputActions.h"

class SceneManager;

class LuaInterface {
public:
    LuaInterface(PakResource& pakResource, VulkanRenderer& renderer, SceneManager* sceneManager = nullptr);
    ~LuaInterface();

    // Execute a Lua script from ResourceData
    void executeScript(const ResourceData& scriptData);

    // Scene management functions
    void loadScene(uint64_t sceneId, const ResourceData& scriptData);
    void initScene(uint64_t sceneId);
    void updateScene(uint64_t sceneId, float deltaTime);
    void handleKeyDown(uint64_t sceneId, int keyCode);
    void handleKeyUp(uint64_t sceneId, int keyCode);
    void handleAction(uint64_t sceneId, Action action);
    void cleanupScene(uint64_t sceneId);
    void switchToScenePipeline(uint64_t sceneId);
    void clearScenePipelines(uint64_t sceneId);

    // Physics access
    Box2DPhysics& getPhysics() { return *physics_; }
    
    // Scene layer access
    SceneLayerManager& getSceneLayerManager() { return *layerManager_; }

private:
    // Lua-callable functions
    static int loadShaders(lua_State* L);
    static int pushScene(lua_State* L);
    static int popScene(lua_State* L);
    static int isKeyPressed(lua_State* L);

    // Box2D Lua bindings
    static int b2SetGravity(lua_State* L);
    static int b2Step(lua_State* L);
    static int b2CreateBody(lua_State* L);
    static int b2DestroyBody(lua_State* L);
    static int b2AddBoxFixture(lua_State* L);
    static int b2AddCircleFixture(lua_State* L);
    static int b2SetBodyPosition(lua_State* L);
    static int b2SetBodyAngle(lua_State* L);
    static int b2SetBodyLinearVelocity(lua_State* L);
    static int b2SetBodyAngularVelocity(lua_State* L);
    static int b2SetBodyAwake(lua_State* L);
    static int b2ApplyForce(lua_State* L);
    static int b2ApplyTorque(lua_State* L);
    static int b2GetBodyPosition(lua_State* L);
    static int b2GetBodyAngle(lua_State* L);
    static int b2GetBodyLinearVelocity(lua_State* L);
    static int b2GetBodyAngularVelocity(lua_State* L);
    static int b2EnableDebugDraw(lua_State* L);
    
    // Scene layer Lua bindings
    static int createLayer(lua_State* L);
    static int destroyLayer(lua_State* L);
    static int attachLayerToBody(lua_State* L);
    static int detachLayer(lua_State* L);
    static int setLayerEnabled(lua_State* L);

    void registerFunctions();

    PakResource& pakResource_;
    VulkanRenderer& renderer_;
    lua_State* luaState_;
    SceneManager* sceneManager_;
    int pipelineIndex_;
    uint64_t currentSceneId_;
    std::unordered_map<uint64_t, std::vector<std::pair<int, int>> > scenePipelines_; // pipelineId, zIndex
    std::unique_ptr<Box2DPhysics> physics_;
    std::unique_ptr<SceneLayerManager> layerManager_;
};