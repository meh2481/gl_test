#include "LuaInterface.h"
#include "SceneManager.h"
#include <iostream>
#include <cassert>
#include <algorithm>

LuaInterface::LuaInterface(PakResource& pakResource, VulkanRenderer& renderer, SceneManager* sceneManager)
    : pakResource_(pakResource), renderer_(renderer), sceneManager_(sceneManager), pipelineIndex_(0), currentSceneId_(0) {
    luaState_ = luaL_newstate();
    luaL_openlibs(luaState_);
    physics_ = std::make_unique<Box2DPhysics>();
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
    const char* globalFunctions[] = {"loadShaders", "pushScene", "popScene", "print",
                                     "b2SetGravity", "b2Step", "b2CreateBody", "b2DestroyBody",
                                     "b2AddBoxFixture", "b2AddCircleFixture", "b2SetBodyPosition",
                                     "b2SetBodyAngle", "b2SetBodyLinearVelocity", "b2SetBodyAngularVelocity",
                                     "b2SetBodyAwake", "b2ApplyForce", "b2ApplyTorque", "b2GetBodyPosition", "b2GetBodyAngle",
                                     "b2GetBodyLinearVelocity", "b2GetBodyAngularVelocity", "b2EnableDebugDraw",
                                     "ipairs", "pairs", nullptr};
    for (const char** func = globalFunctions; *func; ++func) {
        lua_getglobal(luaState_, *func);
        lua_setfield(luaState_, -2, *func);
    }

    // Copy math table
    lua_getglobal(luaState_, "math");
    lua_setfield(luaState_, -2, "math");

    // Copy table library
    lua_getglobal(luaState_, "table");
    lua_setfield(luaState_, -2, "table");

    // Copy SDL keycode constants
    const char* sdlKeycodes[] = {
        "SDLK_ESCAPE", "SDLK_RETURN", "SDLK_BACKSPACE", "SDLK_TAB", "SDLK_SPACE", "SDLK_DELETE",
        "SDLK_F1", "SDLK_F2", "SDLK_F3", "SDLK_F4", "SDLK_F5", "SDLK_F6",
        "SDLK_F7", "SDLK_F8", "SDLK_F9", "SDLK_F10", "SDLK_F11", "SDLK_F12",
        "SDLK_UP", "SDLK_DOWN", "SDLK_RIGHT", "SDLK_LEFT",
        "SDLK_LSHIFT", "SDLK_RSHIFT", "SDLK_LCTRL", "SDLK_RCTRL", "SDLK_LALT", "SDLK_RALT",
        "SDLK_HOME", "SDLK_END", "SDLK_PAGEUP", "SDLK_PAGEDOWN", "SDLK_INSERT",
        "SDLK_KP0", "SDLK_KP1", "SDLK_KP2", "SDLK_KP3", "SDLK_KP4", "SDLK_KP5", "SDLK_KP6", "SDLK_KP7", "SDLK_KP8", "SDLK_KP9",
        "SDLK_KP_PERIOD", "SDLK_KP_DIVIDE", "SDLK_KP_MULTIPLY", "SDLK_KP_MINUS", "SDLK_KP_PLUS", "SDLK_KP_ENTER", "SDLK_KP_EQUALS",
        "SDLK_0", "SDLK_1", "SDLK_2", "SDLK_3", "SDLK_4", "SDLK_5", "SDLK_6", "SDLK_7", "SDLK_8", "SDLK_9",
        "SDLK_a", "SDLK_b", "SDLK_c", "SDLK_d", "SDLK_e", "SDLK_f", "SDLK_g", "SDLK_h", "SDLK_i", "SDLK_j",
        "SDLK_k", "SDLK_l", "SDLK_m", "SDLK_n", "SDLK_o", "SDLK_p", "SDLK_q", "SDLK_r", "SDLK_s", "SDLK_t",
        "SDLK_u", "SDLK_v", "SDLK_w", "SDLK_x", "SDLK_y", "SDLK_z",
        nullptr
    };
    for (const char** keycode = sdlKeycodes; *keycode; ++keycode) {
        lua_getglobal(luaState_, *keycode);
        lua_setfield(luaState_, -2, *keycode);
    }

    // Copy Box2D constants
    const char* box2dConstants[] = {
        "B2_STATIC_BODY", "B2_KINEMATIC_BODY", "B2_DYNAMIC_BODY",
        nullptr
    };
    for (const char** constant = box2dConstants; *constant; ++constant) {
        lua_getglobal(luaState_, *constant);
        lua_setfield(luaState_, -2, *constant);
    }

    // Copy Action constants
    const char* actionConstants[] = {
        "ACTION_EXIT", "ACTION_MENU", "ACTION_PHYSICS_DEMO", "ACTION_TOGGLE_FULLSCREEN",
        "ACTION_HOTRELOAD", "ACTION_APPLY_FORCE", "ACTION_RESET_PHYSICS",
        nullptr
    };
    for (const char** constant = actionConstants; *constant; ++constant) {
        lua_getglobal(luaState_, *constant);
        lua_setfield(luaState_, -2, *constant);
    }

    // Load the script
    if (luaL_loadbuffer(luaState_, (char*)scriptData.data, scriptData.size, NULL) != LUA_OK) {
        lua_pop(luaState_, 1); // Pop the table
        assert(false);
        return;
    }

    // Set the scene table as the environment (_ENV) for the loaded script
    lua_pushvalue(luaState_, -2); // Push the scene table
    lua_setupvalue(luaState_, -2, 1); // Set _ENV upvalue

    // Execute the script with the scene table as its environment
    if (lua_pcall(luaState_, 0, 0, 0) != LUA_OK) {
        lua_pop(luaState_, 1); // Pop the table
        assert(false);
        return;
    }

    // Store the table in the global registry with the scene ID as key
    lua_pushinteger(luaState_, sceneId);
    lua_pushvalue(luaState_, -2); // Push the table
    lua_settable(luaState_, LUA_REGISTRYINDEX);

    // Pop the table
    lua_pop(luaState_, 1);
}

void LuaInterface::initScene(uint64_t sceneId) {
    currentSceneId_ = sceneId;
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

void LuaInterface::handleKeyDown(uint64_t sceneId, int keyCode) {
    // Get the scene table from registry
    lua_pushinteger(luaState_, sceneId);
    lua_gettable(luaState_, LUA_REGISTRYINDEX);

    if (!lua_istable(luaState_, -1)) {
        lua_pop(luaState_, 1);
        return; // Scene not found, silently ignore
    }

    // Get the onKeyDown function from the table (optional)
    lua_getfield(luaState_, -1, "onKeyDown");
    if (!lua_isfunction(luaState_, -1)) {
        lua_pop(luaState_, 2); // Pop nil and table
        return; // No onKeyDown function, silently ignore
    }

    // Push keyCode parameter
    lua_pushinteger(luaState_, keyCode);

    // Call onKeyDown(keyCode)
    if (lua_pcall(luaState_, 1, 0, 0) != LUA_OK) {
        // Get the error message
        const char* errorMsg = lua_tostring(luaState_, -1);
        std::cerr << "Lua onKeyDown error: " << (errorMsg ? errorMsg : "unknown error") << std::endl;
        lua_pop(luaState_, 2); // Pop error message and table
        return;
    }

    // Pop the table
    lua_pop(luaState_, 1);
}

void LuaInterface::handleKeyUp(uint64_t sceneId, int keyCode) {
    // Get the scene table from registry
    lua_pushinteger(luaState_, sceneId);
    lua_gettable(luaState_, LUA_REGISTRYINDEX);

    if (!lua_istable(luaState_, -1)) {
        lua_pop(luaState_, 1);
        return; // Scene not found, silently ignore
    }

    // Get the onKeyUp function from the table (optional)
    lua_getfield(luaState_, -1, "onKeyUp");
    if (!lua_isfunction(luaState_, -1)) {
        lua_pop(luaState_, 2); // Pop nil and table
        return; // No onKeyUp function, silently ignore
    }

    // Push keyCode parameter
    lua_pushinteger(luaState_, keyCode);

    // Call onKeyUp(keyCode)
    if (lua_pcall(luaState_, 1, 0, 0) != LUA_OK) {
        // Get the error message
        const char* errorMsg = lua_tostring(luaState_, -1);
        std::cerr << "Lua onKeyUp error: " << (errorMsg ? errorMsg : "unknown error") << std::endl;
        lua_pop(luaState_, 2); // Pop error message and table
        return;
    }

    // Pop the table
    lua_pop(luaState_, 1);
}

void LuaInterface::handleAction(uint64_t sceneId, Action action) {
    // Get the scene table from registry
    lua_pushinteger(luaState_, sceneId);
    lua_gettable(luaState_, LUA_REGISTRYINDEX);

    if (!lua_istable(luaState_, -1)) {
        lua_pop(luaState_, 1);
        return; // Scene not found, silently ignore
    }

    // Get the onAction function from the table (optional)
    lua_getfield(luaState_, -1, "onAction");
    if (!lua_isfunction(luaState_, -1)) {
        lua_pop(luaState_, 2); // Pop nil and table
        return; // No onAction function, silently ignore
    }

    // Push action parameter
    lua_pushinteger(luaState_, action);

    // Call onAction(action)
    if (lua_pcall(luaState_, 1, 0, 0) != LUA_OK) {
        // Get the error message
        const char* errorMsg = lua_tostring(luaState_, -1);
        std::cerr << "Lua onAction error: " << (errorMsg ? errorMsg : "unknown error") << std::endl;
        lua_pop(luaState_, 2); // Pop error message and table
        return;
    }

    // Pop the table
    lua_pop(luaState_, 1);
}

void LuaInterface::cleanupScene(uint64_t sceneId) {
    // Get the scene table from registry
    lua_pushinteger(luaState_, sceneId);
    lua_gettable(luaState_, LUA_REGISTRYINDEX);

    if (!lua_istable(luaState_, -1)) {
        lua_pop(luaState_, 1);
        return; // Scene not found, silently ignore
    }

    // Get the cleanup function from the table (optional)
    lua_getfield(luaState_, -1, "cleanup");
    if (!lua_isfunction(luaState_, -1)) {
        lua_pop(luaState_, 2); // Pop nil and table
        return; // No cleanup function, silently ignore
    }

    // Call cleanup()
    if (lua_pcall(luaState_, 0, 0, 0) != LUA_OK) {
        // Get the error message
        const char* errorMsg = lua_tostring(luaState_, -1);
        std::cerr << "Lua cleanup error: " << (errorMsg ? errorMsg : "unknown error") << std::endl;
        lua_pop(luaState_, 2); // Pop error message and table
        return;
    }

    // Pop the table
    lua_pop(luaState_, 1);
}

void LuaInterface::switchToScenePipeline(uint64_t sceneId) {
    auto it = scenePipelines_.find(sceneId);
    if (it != scenePipelines_.end()) {
        // Sort pipelines by z-index (lower z-index drawn first)
        std::vector<std::pair<int, int>> sortedPipelines = it->second;
        std::sort(sortedPipelines.begin(), sortedPipelines.end(),
                  [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                      return a.second < b.second; // Sort by z-index ascending
                  });

        // Extract just the pipeline IDs in sorted order
        std::vector<uint64_t> pipelineIds;
        for (const auto& pair : sortedPipelines) {
            pipelineIds.push_back(pair.first);
        }

        renderer_.setPipelinesToDraw(pipelineIds);
    }
}

void LuaInterface::clearScenePipelines(uint64_t sceneId) {
    scenePipelines_[sceneId].clear();
}

void LuaInterface::registerFunctions() {
    // Store this instance in the Lua registry
    lua_pushlightuserdata(luaState_, this);
    lua_setfield(luaState_, LUA_REGISTRYINDEX, "LuaInterface");

    // Register functions in the global table
    lua_register(luaState_, "loadShaders", loadShaders);
    lua_register(luaState_, "pushScene", pushScene);
    lua_register(luaState_, "popScene", popScene);

    // Register SDL keycode constants
    // Special keys
    lua_pushinteger(luaState_, 27);
    lua_setglobal(luaState_, "SDLK_ESCAPE");
    lua_pushinteger(luaState_, 13);
    lua_setglobal(luaState_, "SDLK_RETURN");
    lua_pushinteger(luaState_, 8);
    lua_setglobal(luaState_, "SDLK_BACKSPACE");
    lua_pushinteger(luaState_, 9);
    lua_setglobal(luaState_, "SDLK_TAB");
    lua_pushinteger(luaState_, 32);
    lua_setglobal(luaState_, "SDLK_SPACE");
    lua_pushinteger(luaState_, 127);
    lua_setglobal(luaState_, "SDLK_DELETE");

    // Function keys
    for (int i = 1; i <= 12; ++i) {
        lua_pushinteger(luaState_, 282 + (i - 1));
        lua_setglobal(luaState_, ("SDLK_F" + std::to_string(i)).c_str());
    }

    // Arrow keys
    lua_pushinteger(luaState_, 273);
    lua_setglobal(luaState_, "SDLK_UP");
    lua_pushinteger(luaState_, 274);
    lua_setglobal(luaState_, "SDLK_DOWN");
    lua_pushinteger(luaState_, 275);
    lua_setglobal(luaState_, "SDLK_RIGHT");
    lua_pushinteger(luaState_, 276);
    lua_setglobal(luaState_, "SDLK_LEFT");

    // Modifier keys
    lua_pushinteger(luaState_, 304);
    lua_setglobal(luaState_, "SDLK_LSHIFT");
    lua_pushinteger(luaState_, 303);
    lua_setglobal(luaState_, "SDLK_RSHIFT");
    lua_pushinteger(luaState_, 306);
    lua_setglobal(luaState_, "SDLK_LCTRL");
    lua_pushinteger(luaState_, 305);
    lua_setglobal(luaState_, "SDLK_RCTRL");
    lua_pushinteger(luaState_, 308);
    lua_setglobal(luaState_, "SDLK_LALT");
    lua_pushinteger(luaState_, 307);
    lua_setglobal(luaState_, "SDLK_RALT");

    // Other special keys
    lua_pushinteger(luaState_, 278);
    lua_setglobal(luaState_, "SDLK_HOME");
    lua_pushinteger(luaState_, 279);
    lua_setglobal(luaState_, "SDLK_END");
    lua_pushinteger(luaState_, 280);
    lua_setglobal(luaState_, "SDLK_PAGEUP");
    lua_pushinteger(luaState_, 281);
    lua_setglobal(luaState_, "SDLK_PAGEDOWN");
    lua_pushinteger(luaState_, 277);
    lua_setglobal(luaState_, "SDLK_INSERT");

    // Numpad keys
    lua_pushinteger(luaState_, 256);
    lua_setglobal(luaState_, "SDLK_KP0");
    lua_pushinteger(luaState_, 257);
    lua_setglobal(luaState_, "SDLK_KP1");
    lua_pushinteger(luaState_, 258);
    lua_setglobal(luaState_, "SDLK_KP2");
    lua_pushinteger(luaState_, 259);
    lua_setglobal(luaState_, "SDLK_KP3");
    lua_pushinteger(luaState_, 260);
    lua_setglobal(luaState_, "SDLK_KP4");
    lua_pushinteger(luaState_, 261);
    lua_setglobal(luaState_, "SDLK_KP5");
    lua_pushinteger(luaState_, 262);
    lua_setglobal(luaState_, "SDLK_KP6");
    lua_pushinteger(luaState_, 263);
    lua_setglobal(luaState_, "SDLK_KP7");
    lua_pushinteger(luaState_, 264);
    lua_setglobal(luaState_, "SDLK_KP8");
    lua_pushinteger(luaState_, 265);
    lua_setglobal(luaState_, "SDLK_KP9");
    lua_pushinteger(luaState_, 266);
    lua_setglobal(luaState_, "SDLK_KP_PERIOD");
    lua_pushinteger(luaState_, 267);
    lua_setglobal(luaState_, "SDLK_KP_DIVIDE");
    lua_pushinteger(luaState_, 268);
    lua_setglobal(luaState_, "SDLK_KP_MULTIPLY");
    lua_pushinteger(luaState_, 269);
    lua_setglobal(luaState_, "SDLK_KP_MINUS");
    lua_pushinteger(luaState_, 270);
    lua_setglobal(luaState_, "SDLK_KP_PLUS");
    lua_pushinteger(luaState_, 271);
    lua_setglobal(luaState_, "SDLK_KP_ENTER");
    lua_pushinteger(luaState_, 272);
    lua_setglobal(luaState_, "SDLK_KP_EQUALS");

    // Number keys
    for (int i = 0; i <= 9; ++i) {
        lua_pushinteger(luaState_, 48 + i);
        lua_setglobal(luaState_, ("SDLK_" + std::to_string(i)).c_str());
    }

    // Letter keys
    for (char c = 'a'; c <= 'z'; ++c) {
        lua_pushinteger(luaState_, 97 + (c - 'a'));
        lua_setglobal(luaState_, ("SDLK_" + std::string(1, c)).c_str());
    }

    // Register Box2D functions
    lua_register(luaState_, "b2SetGravity", b2SetGravity);
    lua_register(luaState_, "b2Step", b2Step);
    lua_register(luaState_, "b2CreateBody", b2CreateBody);
    lua_register(luaState_, "b2DestroyBody", b2DestroyBody);
    lua_register(luaState_, "b2AddBoxFixture", b2AddBoxFixture);
    lua_register(luaState_, "b2AddCircleFixture", b2AddCircleFixture);
    lua_register(luaState_, "b2SetBodyPosition", b2SetBodyPosition);
    lua_register(luaState_, "b2SetBodyAngle", b2SetBodyAngle);
    lua_register(luaState_, "b2SetBodyLinearVelocity", b2SetBodyLinearVelocity);
    lua_register(luaState_, "b2SetBodyAngularVelocity", b2SetBodyAngularVelocity);
    lua_register(luaState_, "b2SetBodyAwake", b2SetBodyAwake);
    lua_register(luaState_, "b2ApplyForce", b2ApplyForce);
    lua_register(luaState_, "b2ApplyTorque", b2ApplyTorque);
    lua_register(luaState_, "b2GetBodyPosition", b2GetBodyPosition);
    lua_register(luaState_, "b2GetBodyAngle", b2GetBodyAngle);
    lua_register(luaState_, "b2GetBodyLinearVelocity", b2GetBodyLinearVelocity);
    lua_register(luaState_, "b2GetBodyAngularVelocity", b2GetBodyAngularVelocity);
    lua_register(luaState_, "b2EnableDebugDraw", b2EnableDebugDraw);

    // Register Box2D body type constants
    lua_pushinteger(luaState_, 0);
    lua_setglobal(luaState_, "B2_STATIC_BODY");
    lua_pushinteger(luaState_, 1);
    lua_setglobal(luaState_, "B2_KINEMATIC_BODY");
    lua_pushinteger(luaState_, 2);
    lua_setglobal(luaState_, "B2_DYNAMIC_BODY");

    // Register Action constants
    lua_pushinteger(luaState_, ACTION_EXIT);
    lua_setglobal(luaState_, "ACTION_EXIT");
    lua_pushinteger(luaState_, ACTION_MENU);
    lua_setglobal(luaState_, "ACTION_MENU");
    lua_pushinteger(luaState_, ACTION_PHYSICS_DEMO);
    lua_setglobal(luaState_, "ACTION_PHYSICS_DEMO");
    lua_pushinteger(luaState_, ACTION_TOGGLE_FULLSCREEN);
    lua_setglobal(luaState_, "ACTION_TOGGLE_FULLSCREEN");
    lua_pushinteger(luaState_, ACTION_HOTRELOAD);
    lua_setglobal(luaState_, "ACTION_HOTRELOAD");
    lua_pushinteger(luaState_, ACTION_APPLY_FORCE);
    lua_setglobal(luaState_, "ACTION_APPLY_FORCE");
    lua_pushinteger(luaState_, ACTION_RESET_PHYSICS);
    lua_setglobal(luaState_, "ACTION_RESET_PHYSICS");
}

int LuaInterface::loadShaders(lua_State* L) {
    // Get the LuaInterface instance from the Lua registry
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Check arguments - can be 2 or 3 (vertex, fragment, optional z-index)
    int numArgs = lua_gettop(L);
    assert(numArgs >= 2 && numArgs <= 3);
    assert(lua_isstring(L, 1) && lua_isstring(L, 2));
    if (numArgs == 3) {
        assert(lua_isnumber(L, 3));
    }

    const char* vertFile = lua_tostring(L, 1);
    const char* fragFile = lua_tostring(L, 2);
    int zIndex = (numArgs == 3) ? lua_tointeger(L, 3) : 0;

    // Check if this specific shader combination is already loaded for this scene
    auto& scenePipelines = interface->scenePipelines_[interface->currentSceneId_];
    bool alreadyLoaded = false;
    for (const auto& pipeline : scenePipelines) {
        // We can't easily check the actual shader files, but we can check if we already have
        // a pipeline with the same z-index (assuming each z-index should be unique)
        if (pipeline.second == zIndex) {
            alreadyLoaded = true;
            break;
        }
    }

    if (alreadyLoaded) {
        std::cout << "Shader with z-index " << zIndex << " already loaded for scene, skipping: " << vertFile << " and " << fragFile << std::endl;
        return 0; // No return values
    }

    // Hash filenames to get resource IDs
    uint64_t vertId = std::hash<std::string>{}(vertFile);
    uint64_t fragId = std::hash<std::string>{}(fragFile);

    // Get shader data from pak file
    ResourceData vertShader = interface->pakResource_.getResource(vertId);
    ResourceData fragShader = interface->pakResource_.getResource(fragId);

    assert(vertShader.size != 0 && fragShader.size != 0);

    // Check if this is a debug shader
    bool isDebugPipeline = (std::string(vertFile) == "debug_vertex.spv");

    // Create pipeline
    interface->renderer_.createPipeline(interface->pipelineIndex_, vertShader, fragShader, isDebugPipeline);
    // Add to current scene's pipeline list with z-index
    scenePipelines.emplace_back(interface->pipelineIndex_, zIndex);
    interface->pipelineIndex_++;

    std::cout << "Loaded shaders: " << vertFile << " and " << fragFile << " (z-index: " << zIndex << ")" << std::endl;

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
// Box2D Lua bindings
int LuaInterface::b2SetGravity(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 2);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2));

    float x = lua_tonumber(L, 1);
    float y = lua_tonumber(L, 2);

    interface->physics_->setGravity(x, y);
    return 0;
}

int LuaInterface::b2Step(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) >= 1);
    assert(lua_isnumber(L, 1));

    float timeStep = lua_tonumber(L, 1);
    int subStepCount = 4;  // Box2D 3.x uses subStepCount instead of velocity/position iterations

    if (lua_gettop(L) >= 2) {
        assert(lua_isnumber(L, 2));
        subStepCount = lua_tointeger(L, 2);
    }

    interface->physics_->step(timeStep, subStepCount);
    return 0;
}

int LuaInterface::b2CreateBody(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) >= 3);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2) && lua_isnumber(L, 3));

    int bodyType = lua_tointeger(L, 1);
    float x = lua_tonumber(L, 2);
    float y = lua_tonumber(L, 3);
    float angle = 0.0f;

    if (lua_gettop(L) >= 4) {
        assert(lua_isnumber(L, 4));
        angle = lua_tonumber(L, 4);
    }

    int bodyId = interface->physics_->createBody(bodyType, x, y, angle);
    lua_pushinteger(L, bodyId);
    return 1;
}

int LuaInterface::b2DestroyBody(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int bodyId = lua_tointeger(L, 1);
    interface->physics_->destroyBody(bodyId);
    return 0;
}

int LuaInterface::b2AddBoxFixture(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) >= 3);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2) && lua_isnumber(L, 3));

    int bodyId = lua_tointeger(L, 1);
    float halfWidth = lua_tonumber(L, 2);
    float halfHeight = lua_tonumber(L, 3);
    float density = 1.0f;
    float friction = 0.3f;
    float restitution = 0.0f;

    if (lua_gettop(L) >= 4) {
        assert(lua_isnumber(L, 4));
        density = lua_tonumber(L, 4);
    }
    if (lua_gettop(L) >= 5) {
        assert(lua_isnumber(L, 5));
        friction = lua_tonumber(L, 5);
    }
    if (lua_gettop(L) >= 6) {
        assert(lua_isnumber(L, 6));
        restitution = lua_tonumber(L, 6);
    }

    interface->physics_->addBoxFixture(bodyId, halfWidth, halfHeight, density, friction, restitution);
    return 0;
}

int LuaInterface::b2AddCircleFixture(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) >= 2);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2));

    int bodyId = lua_tointeger(L, 1);
    float radius = lua_tonumber(L, 2);
    float density = 1.0f;
    float friction = 0.3f;
    float restitution = 0.0f;

    if (lua_gettop(L) >= 3) {
        assert(lua_isnumber(L, 3));
        density = lua_tonumber(L, 3);
    }
    if (lua_gettop(L) >= 4) {
        assert(lua_isnumber(L, 4));
        friction = lua_tonumber(L, 4);
    }
    if (lua_gettop(L) >= 5) {
        assert(lua_isnumber(L, 5));
        restitution = lua_tonumber(L, 5);
    }

    interface->physics_->addCircleFixture(bodyId, radius, density, friction, restitution);
    return 0;
}

int LuaInterface::b2SetBodyPosition(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 3);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2) && lua_isnumber(L, 3));

    int bodyId = lua_tointeger(L, 1);
    float x = lua_tonumber(L, 2);
    float y = lua_tonumber(L, 3);

    interface->physics_->setBodyPosition(bodyId, x, y);
    return 0;
}

int LuaInterface::b2SetBodyAngle(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 2);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2));

    int bodyId = lua_tointeger(L, 1);
    float angle = lua_tonumber(L, 2);

    interface->physics_->setBodyAngle(bodyId, angle);
    return 0;
}

int LuaInterface::b2SetBodyLinearVelocity(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 3);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2) && lua_isnumber(L, 3));

    int bodyId = lua_tointeger(L, 1);
    float vx = lua_tonumber(L, 2);
    float vy = lua_tonumber(L, 3);

    interface->physics_->setBodyLinearVelocity(bodyId, vx, vy);
    return 0;
}

int LuaInterface::b2SetBodyAngularVelocity(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 2);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2));

    int bodyId = lua_tointeger(L, 1);
    float omega = lua_tonumber(L, 2);

    interface->physics_->setBodyAngularVelocity(bodyId, omega);
    return 0;
}

int LuaInterface::b2SetBodyAwake(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 2);
    assert(lua_isnumber(L, 1) && lua_isboolean(L, 2));

    int bodyId = lua_tointeger(L, 1);
    bool awake = lua_toboolean(L, 2);

    interface->physics_->setBodyAwake(bodyId, awake);
    return 0;
}

int LuaInterface::b2ApplyForce(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 5);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2) && lua_isnumber(L, 3) && lua_isnumber(L, 4) && lua_isnumber(L, 5));

    int bodyId = lua_tointeger(L, 1);
    float fx = lua_tonumber(L, 2);
    float fy = lua_tonumber(L, 3);
    float px = lua_tonumber(L, 4);
    float py = lua_tonumber(L, 5);

    interface->physics_->applyForce(bodyId, fx, fy, px, py);
    return 0;
}

int LuaInterface::b2ApplyTorque(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 2);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2));

    int bodyId = lua_tointeger(L, 1);
    float torque = lua_tonumber(L, 2);

    interface->physics_->applyTorque(bodyId, torque);
    return 0;
}

int LuaInterface::b2GetBodyPosition(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int bodyId = lua_tointeger(L, 1);
    float x = interface->physics_->getBodyPositionX(bodyId);
    float y = interface->physics_->getBodyPositionY(bodyId);

    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
}

int LuaInterface::b2GetBodyAngle(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int bodyId = lua_tointeger(L, 1);
    float angle = interface->physics_->getBodyAngle(bodyId);

    lua_pushnumber(L, angle);
    return 1;
}

int LuaInterface::b2GetBodyLinearVelocity(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int bodyId = lua_tointeger(L, 1);
    float vx = interface->physics_->getBodyLinearVelocityX(bodyId);
    float vy = interface->physics_->getBodyLinearVelocityY(bodyId);

    lua_pushnumber(L, vx);
    lua_pushnumber(L, vy);
    return 2;
}

int LuaInterface::b2GetBodyAngularVelocity(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int bodyId = lua_tointeger(L, 1);
    float omega = interface->physics_->getBodyAngularVelocity(bodyId);

    lua_pushnumber(L, omega);
    return 1;
}

int LuaInterface::b2EnableDebugDraw(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isboolean(L, 1));

    bool enable = lua_toboolean(L, 1);
    interface->physics_->enableDebugDraw(enable);
    return 0;
}
