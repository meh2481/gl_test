#pragma once

#include <lua.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include "resource.h"
#include "VulkanRenderer.h"

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
    void switchToScenePipeline(uint64_t sceneId);

private:
    // Lua-callable functions
    static int loadShaders(lua_State* L);
    static int pushScene(lua_State* L);
    static int popScene(lua_State* L);
    static int isKeyPressed(lua_State* L);

    void registerFunctions();

    PakResource& pakResource_;
    VulkanRenderer& renderer_;
    lua_State* luaState_;
    SceneManager* sceneManager_;
    int pipelineIndex_;
    uint64_t currentSceneId_;
    std::unordered_map<uint64_t, std::vector<int> > scenePipelines_;
};;