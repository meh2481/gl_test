#include "LuaInterface.h"
#include <iostream>

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

bool LuaInterface::executeScript(const ResourceData& scriptData) {
    if (luaL_loadbuffer(luaState_, (char*)scriptData.data, scriptData.size, NULL) != LUA_OK) {
        std::cerr << "Failed to load Lua script: " << lua_tostring(luaState_, -1) << std::endl;
        return false;
    }

    if (lua_pcall(luaState_, 0, 0, 0) != LUA_OK) {
        std::cerr << "Failed to execute Lua script: " << lua_tostring(luaState_, -1) << std::endl;
        return false;
    }

    return true;
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
    if (lua_gettop(L) != 2) {
        luaL_error(L, "loadShaders expects 2 arguments: vertex shader filename and fragment shader filename");
        return 0;
    }

    if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
        luaL_error(L, "loadShaders arguments must be strings");
        return 0;
    }

    const char* vertFile = lua_tostring(L, 1);
    const char* fragFile = lua_tostring(L, 2);

    // Hash filenames to get resource IDs
    uint64_t vertId = std::hash<std::string>{}(vertFile);
    uint64_t fragId = std::hash<std::string>{}(fragFile);

    // Get shader data from pak file
    ResourceData vertShader = interface->pakResource_.getResource(vertId);
    ResourceData fragShader = interface->pakResource_.getResource(fragId);

    if (vertShader.size == 0 || fragShader.size == 0) {
        luaL_error(L, "Failed to load shader resources: %s or %s not found", vertFile, fragFile);
        return 0;
    }

    // Create pipeline
    interface->renderer_.createPipeline(interface->pipelineIndex_, vertShader, fragShader);
    interface->renderer_.setCurrentPipeline(interface->pipelineIndex_);
    interface->pipelineIndex_++;

    std::cout << "Loaded shaders: " << vertFile << " and " << fragFile << std::endl;

    return 0; // No return values
}