#include "LuaInterface.h"
#include "SceneManager.h"
#include <iostream>
#include <cassert>

LuaInterface::LuaInterface(PakResource& pakResource, VulkanRenderer& renderer, SceneManager* sceneManager)
    : pakResource_(pakResource), renderer_(renderer), sceneManager_(sceneManager), pipelineIndex_(0) {
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

void LuaInterface::loadScene(uint64_t sceneId, const ResourceData& scriptData) {
    // Create a table for this scene
    lua_newtable(luaState_);

    // Copy global functions and tables into the scene table
    const char* globalFunctions[] = {"loadShaders", "pushScene", "popScene", "print", nullptr};
    for (const char** func = globalFunctions; *func; ++func) {
        lua_getglobal(luaState_, *func);
        lua_setfield(luaState_, -2, *func);
    }

    // Copy math table
    lua_getglobal(luaState_, "math");
    lua_setfield(luaState_, -2, "math");

    // Load and execute the script in the global environment first
    if (luaL_loadbuffer(luaState_, (char*)scriptData.data, scriptData.size, NULL) != LUA_OK) {
        lua_pop(luaState_, 1); // Pop the table
        assert(false);
        return;
    }

    // Execute the script
    if (lua_pcall(luaState_, 0, 0, 0) != LUA_OK) {
        lua_pop(luaState_, 1); // Pop the table
        assert(false);
        return;
    }

    // Now move the init and update functions from global to the scene table
    lua_getglobal(luaState_, "init");
    if (lua_isfunction(luaState_, -1)) {
        lua_setfield(luaState_, -2, "init");
        // Set the scene table as the _ENV upvalue for the init function
        lua_getfield(luaState_, -1, "init");
        lua_pushvalue(luaState_, -2); // scene table
        lua_setupvalue(luaState_, -2, 1); // set _ENV
        lua_pop(luaState_, 1); // pop init
    } else {
        lua_pop(luaState_, 1); // Pop nil
    }

    lua_getglobal(luaState_, "update");
    if (lua_isfunction(luaState_, -1)) {
        lua_setfield(luaState_, -2, "update");
        // Set the scene table as the _ENV upvalue for the update function
        lua_getfield(luaState_, -1, "update");
        lua_pushvalue(luaState_, -2); // scene table
        lua_setupvalue(luaState_, -2, 1); // set _ENV
        lua_pop(luaState_, 1); // pop update
    } else {
        lua_pop(luaState_, 1); // Pop nil
    }

    // Store the table in the global registry with the scene ID as key
    lua_pushinteger(luaState_, sceneId);
    lua_pushvalue(luaState_, -2); // Push the table
    lua_settable(luaState_, LUA_REGISTRYINDEX);

    // Pop the table from the stack
    lua_pop(luaState_, 1);
}

void LuaInterface::initScene(uint64_t sceneId) {
    // Get the scene table from registry
    lua_pushinteger(luaState_, sceneId);
    lua_gettable(luaState_, LUA_REGISTRYINDEX);

    if (!lua_istable(luaState_, -1)) {
        lua_pop(luaState_, 1);
        assert(false);
        return;
    }

    // Get the init function from the table
    lua_getfield(luaState_, -1, "init");
    if (!lua_isfunction(luaState_, -1)) {
        lua_pop(luaState_, 2); // Pop function and table
        assert(false);
        return;
    }

    // Call init()
    if (lua_pcall(luaState_, 0, 0, 0) != LUA_OK) {
        // Get the error message
        const char* errorMsg = lua_tostring(luaState_, -1);
        std::cerr << "Lua init error: " << (errorMsg ? errorMsg : "unknown error") << std::endl;
        lua_pop(luaState_, 2); // Pop error message and table
        assert(false);
        return;
    }

    // Pop the table
    lua_pop(luaState_, 1);
}

void LuaInterface::updateScene(uint64_t sceneId, float deltaTime) {
    // Get the scene table from registry
    lua_pushinteger(luaState_, sceneId);
    lua_gettable(luaState_, LUA_REGISTRYINDEX);

    if (!lua_istable(luaState_, -1)) {
        lua_pop(luaState_, 1);
        assert(false);
        return;
    }

    // Get the update function from the table
    lua_getfield(luaState_, -1, "update");
    if (!lua_isfunction(luaState_, -1)) {
        lua_pop(luaState_, 2); // Pop function and table
        assert(false);
        return;
    }

    // Push deltaTime parameter
    lua_pushnumber(luaState_, deltaTime);

    // Call update(deltaTime)
    if (lua_pcall(luaState_, 1, 0, 0) != LUA_OK) {
        // Get the error message
        const char* errorMsg = lua_tostring(luaState_, -1);
        std::cerr << "Lua update error: " << (errorMsg ? errorMsg : "unknown error") << std::endl;
        lua_pop(luaState_, 2); // Pop error message and table
        assert(false);
        return;
    }

    // Pop the table
    lua_pop(luaState_, 1);
}

void LuaInterface::registerFunctions() {
    // Store this instance in the Lua registry
    lua_pushlightuserdata(luaState_, this);
    lua_setfield(luaState_, LUA_REGISTRYINDEX, "LuaInterface");

    // Register functions in the global table
    lua_register(luaState_, "loadShaders", loadShaders);
    lua_register(luaState_, "pushScene", pushScene);
    lua_register(luaState_, "popScene", popScene);
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

    std::cout << "Loaded shaders: " << vertFile << " and " << fragFile << std::endl;

    return 0; // No return values
}

int LuaInterface::pushScene(lua_State* L) {
    // Get the LuaInterface instance from the Lua registry
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Check arguments
    assert(lua_gettop(L) == 1);
    assert(lua_isstring(L, 1));

    const char* sceneFile = lua_tostring(L, 1);

    // Hash filename to get resource ID
    uint64_t sceneId = std::hash<std::string>{}(sceneFile);

    // Push the scene using the scene manager
    assert(interface->sceneManager_ != nullptr);
    interface->sceneManager_->pushScene(sceneId);

    return 0; // No return values
}

int LuaInterface::popScene(lua_State* L) {
    // Get the LuaInterface instance from the Lua registry
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Check arguments (no arguments expected)
    assert(lua_gettop(L) == 0);

    // Pop the scene using the scene manager
    assert(interface->sceneManager_ != nullptr);
    interface->sceneManager_->popScene();

    return 0; // No return values
}