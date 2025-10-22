#pragma once

#include <lua.hpp>
#include <string>
#include "resource.h"
#include "VulkanRenderer.h"

class LuaInterface {
public:
    LuaInterface(PakResource& pakResource, VulkanRenderer& renderer);
    ~LuaInterface();

    // Execute a Lua script from ResourceData
    void executeScript(const ResourceData& scriptData);

    // Scene management functions
    void initScene();
    void updateScene(float deltaTime);

    // Lua-callable functions
    static int loadShaders(lua_State* L);

private:
    PakResource& pakResource_;
    VulkanRenderer& renderer_;
    int pipelineIndex_;
    lua_State* luaState_;

    // Register functions with Lua
    void registerFunctions();
};