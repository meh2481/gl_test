#include "LuaInterface.h"
#include <cassert>

LuaInterface::LuaInterface(PakResource& pakResource, VulkanRenderer& renderer)
    : pakResource_(pakResource), renderer_(renderer), pipelineIndex_(0) {
    luaState_ = luaL_newstate();
    luaL_openlibs(luaState_);
    registerFunctions();
}

LuaInterface::~LuaInterface() {
    if (luaState_) {
        lua_close(luaState_);
    }
}

void LuaInterface::executeScript(const ResourceData& scriptData) {
    assert(luaL_loadbuffer(luaState_, (char*)scriptData.data, scriptData.size, NULL) == LUA_OK);
    assert(lua_pcall(luaState_, 0, 0, 0) == LUA_OK);
}

void LuaInterface::initScene() {
    lua_getglobal(luaState_, "init");
    assert(lua_isfunction(luaState_, -1));
    assert(lua_pcall(luaState_, 0, 0, 0) == LUA_OK);
}

void LuaInterface::updateScene(float deltaTime) {
    lua_getglobal(luaState_, "update");
    assert(lua_isfunction(luaState_, -1));
    lua_pushnumber(luaState_, deltaTime);
    assert(lua_pcall(luaState_, 1, 0, 0) == LUA_OK);
}

void LuaInterface::registerFunctions() {
    // Store this instance in the Lua registry
    lua_pushlightuserdata(luaState_, this);
    lua_setfield(luaState_, LUA_REGISTRYINDEX, "LuaInterface");

    // Register functions in the global table
    lua_register(luaState_, "loadShaders", loadShaders);
}

int LuaInterface::loadShaders(lua_State* L) {
    // Get the LuaInterface instance from the Lua registry
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Check arguments
    assert(lua_gettop(L) == 2);
    assert(lua_isstring(L, 1) && lua_isstring(L, 2));

    const char* vertFile = lua_tostring(L, 1);
    const char* fragFile = lua_tostring(L, 2);

    // Hash filenames to get resource IDs
    uint64_t vertId = std::hash<std::string>{}(vertFile);
    uint64_t fragId = std::hash<std::string>{}(fragFile);

    // Get shader data from pak file
    ResourceData vertShader = interface->pakResource_.getResource(vertId);
    ResourceData fragShader = interface->pakResource_.getResource(fragId);

    assert(vertShader.size != 0 && fragShader.size != 0);

    // Create pipeline
    interface->renderer_.createPipeline(interface->pipelineIndex_, vertShader, fragShader);
    interface->renderer_.setCurrentPipeline(interface->pipelineIndex_);
    interface->pipelineIndex_++;

    return 0; // No return values
}