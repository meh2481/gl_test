#include "LuaInterface.h"
#include "SceneManager.h"
#include <iostream>
#include <cassert>
#include <algorithm>

LuaInterface::LuaInterface(PakResource& pakResource, VulkanRenderer& renderer, SceneManager* sceneManager, VibrationManager* vibrationManager)
    : pakResource_(pakResource), renderer_(renderer), sceneManager_(sceneManager), vibrationManager_(vibrationManager), pipelineIndex_(0), currentSceneId_(0) {
    luaState_ = luaL_newstate();
    luaL_openlibs(luaState_);
    physics_ = std::make_unique<Box2DPhysics>();
    layerManager_ = std::make_unique<SceneLayerManager>();
    audioManager_ = std::make_unique<AudioManager>();
    audioManager_->initialize();
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
    std::cout << "Loading scene " << sceneId << " with script size " << scriptData.size << std::endl;
    // Create a table for this scene
    lua_newtable(luaState_);

    // Copy global functions and tables into the scene table
    const char* globalFunctions[] = {"loadShaders", "loadTexturedShaders", "loadTexturedShadersEx", "loadTexturedShadersAdditive", "loadTexture",
                                     "getTextureDimensions",
                                     "setShaderUniform3f", "setShaderParameters",
                                     "pushScene", "popScene", "print",
                                     "b2SetGravity", "b2SetFixedTimestep", "b2Step", "b2CreateBody", "b2DestroyBody",
                                     "b2AddBoxFixture", "b2AddCircleFixture", "b2SetBodyPosition",
                                     "b2SetBodyAngle", "b2SetBodyLinearVelocity", "b2SetBodyAngularVelocity",
                                     "b2SetBodyAwake", "b2ApplyForce", "b2ApplyTorque", "b2GetBodyPosition", "b2GetBodyAngle",
                                     "b2GetBodyLinearVelocity", "b2GetBodyAngularVelocity", "b2EnableDebugDraw",
                                     "b2CreateRevoluteJoint", "b2DestroyJoint",
                                     "createLayer", "destroyLayer", "attachLayerToBody", "detachLayer", "setLayerEnabled",
                                     "audioLoadBuffer", "audioLoadOpus", "audioCreateSource", "audioPlaySource", "audioStopSource",
                                     "audioPauseSource", "audioSetSourcePosition", "audioSetSourceVelocity",
                                     "audioSetSourceVolume", "audioSetSourcePitch", "audioSetSourceLooping",
                                     "audioReleaseSource", "audioIsSourcePlaying", "audioSetListenerPosition", "audioSetListenerVelocity",
                                     "audioSetListenerOrientation", "audioSetGlobalVolume", "audioSetGlobalEffect",
                                     "vibrate", "vibrateTriggers", "stopVibration",
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

    // Copy string library
    lua_getglobal(luaState_, "string");
    lua_setfield(luaState_, -2, "string");

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
        "ACTION_EXIT", "ACTION_MENU", "ACTION_PHYSICS_DEMO", "ACTION_AUDIO_TEST", "ACTION_TOGGLE_FULLSCREEN",
        "ACTION_HOTRELOAD", "ACTION_APPLY_FORCE", "ACTION_RESET_PHYSICS", "ACTION_TOGGLE_DEBUG_DRAW",
        nullptr
    };
    for (const char** constant = actionConstants; *constant; ++constant) {
        lua_getglobal(luaState_, *constant);
        lua_setfield(luaState_, -2, *constant);
    }

    // Copy Audio effect constants
    const char* audioConstants[] = {
        "AUDIO_EFFECT_NONE", "AUDIO_EFFECT_LOWPASS", "AUDIO_EFFECT_REVERB",
        nullptr
    };
    for (const char** constant = audioConstants; *constant; ++constant) {
        lua_getglobal(luaState_, *constant);
        lua_setfield(luaState_, -2, *constant);
    }

    // Load the script
    if (luaL_loadbuffer(luaState_, (char*)scriptData.data, scriptData.size, NULL) != LUA_OK) {
        const char* errorMsg = lua_tostring(luaState_, -1);
        std::cerr << "Lua load error: " << (errorMsg ? errorMsg : "unknown error") << std::endl;
        lua_pop(luaState_, 1); // Pop the table
        assert(false);
        return;
    }

    // Set the scene table as the environment (_ENV) for the loaded script
    lua_pushvalue(luaState_, -2); // Push the scene table
    lua_setupvalue(luaState_, -2, 1); // Set _ENV upvalue

    // Execute the script with the scene table as its environment
    if (lua_pcall(luaState_, 0, 0, 0) != LUA_OK) {
        const char* errorMsg = lua_tostring(luaState_, -1);
        std::cerr << "Lua exec error: " << (errorMsg ? errorMsg : "unknown error") << std::endl;
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

    // Update audio manager (cleanup finished sources)
    audioManager_->update();

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
    lua_register(luaState_, "print", luaPrint);  // Override default print

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
    lua_register(luaState_, "b2SetFixedTimestep", b2SetFixedTimestep);
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
    lua_register(luaState_, "b2CreateRevoluteJoint", b2CreateRevoluteJoint);
    lua_register(luaState_, "b2DestroyJoint", b2DestroyJoint);

    // Register scene layer functions
    lua_register(luaState_, "createLayer", createLayer);
    lua_register(luaState_, "destroyLayer", destroyLayer);
    lua_register(luaState_, "attachLayerToBody", attachLayerToBody);
    lua_register(luaState_, "detachLayer", detachLayer);
    lua_register(luaState_, "setLayerEnabled", setLayerEnabled);

    // Register texture loading functions
    lua_register(luaState_, "loadTexture", loadTexture);
    lua_register(luaState_, "getTextureDimensions", getTextureDimensions);
    lua_register(luaState_, "loadTexturedShaders", loadTexturedShaders);
    lua_register(luaState_, "loadTexturedShadersEx", loadTexturedShadersEx);
    lua_register(luaState_, "loadTexturedShadersAdditive", loadTexturedShadersAdditive);
    lua_register(luaState_, "setShaderUniform3f", setShaderUniform3f);
    lua_register(luaState_, "setShaderParameters", setShaderParameters);

    // Register audio functions
    lua_register(luaState_, "audioLoadBuffer", audioLoadBuffer);
    lua_register(luaState_, "audioLoadOpus", audioLoadOpus);
    lua_register(luaState_, "audioCreateSource", audioCreateSource);
    lua_register(luaState_, "audioPlaySource", audioPlaySource);
    lua_register(luaState_, "audioStopSource", audioStopSource);
    lua_register(luaState_, "audioPauseSource", audioPauseSource);
    lua_register(luaState_, "audioSetSourcePosition", audioSetSourcePosition);
    lua_register(luaState_, "audioSetSourceVelocity", audioSetSourceVelocity);
    lua_register(luaState_, "audioSetSourceVolume", audioSetSourceVolume);
    lua_register(luaState_, "audioSetSourcePitch", audioSetSourcePitch);
    lua_register(luaState_, "audioSetSourceLooping", audioSetSourceLooping);
    lua_register(luaState_, "audioReleaseSource", audioReleaseSource);
    lua_register(luaState_, "audioIsSourcePlaying", audioIsSourcePlaying);
    lua_register(luaState_, "audioSetListenerPosition", audioSetListenerPosition);
    lua_register(luaState_, "audioSetListenerVelocity", audioSetListenerVelocity);
    lua_register(luaState_, "audioSetListenerOrientation", audioSetListenerOrientation);
    lua_register(luaState_, "audioSetGlobalVolume", audioSetGlobalVolume);
    lua_register(luaState_, "audioSetGlobalEffect", audioSetGlobalEffect);

    // Register vibration functions
    lua_register(luaState_, "vibrate", vibrate);
    lua_register(luaState_, "vibrateTriggers", vibrateTriggers);
    lua_register(luaState_, "stopVibration", stopVibration);

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
    lua_pushinteger(luaState_, ACTION_AUDIO_TEST);
    lua_setglobal(luaState_, "ACTION_AUDIO_TEST");
    lua_pushinteger(luaState_, ACTION_TOGGLE_FULLSCREEN);
    lua_setglobal(luaState_, "ACTION_TOGGLE_FULLSCREEN");
    lua_pushinteger(luaState_, ACTION_HOTRELOAD);
    lua_setglobal(luaState_, "ACTION_HOTRELOAD");
    lua_pushinteger(luaState_, ACTION_APPLY_FORCE);
    lua_setglobal(luaState_, "ACTION_APPLY_FORCE");
    lua_pushinteger(luaState_, ACTION_RESET_PHYSICS);
    lua_setglobal(luaState_, "ACTION_RESET_PHYSICS");
    lua_pushinteger(luaState_, ACTION_TOGGLE_DEBUG_DRAW);
    lua_setglobal(luaState_, "ACTION_TOGGLE_DEBUG_DRAW");

    // Register Audio effect constants
    lua_pushinteger(luaState_, AUDIO_EFFECT_NONE);
    lua_setglobal(luaState_, "AUDIO_EFFECT_NONE");
    lua_pushinteger(luaState_, AUDIO_EFFECT_LOWPASS);
    lua_setglobal(luaState_, "AUDIO_EFFECT_LOWPASS");
    lua_pushinteger(luaState_, AUDIO_EFFECT_REVERB);
    lua_setglobal(luaState_, "AUDIO_EFFECT_REVERB");
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

int LuaInterface::luaPrint(lua_State* L) {
    int n = lua_gettop(L);  // Number of arguments
    std::string output;
    
    // Convert all arguments to strings and concatenate them
    for (int i = 1; i <= n; i++) {
        const char* s = lua_tostring(L, i);
        if (s == nullptr) {
            return luaL_error(L, "'tostring' must return a string to 'print'");
        }
        if (i > 1) {
            output += "\t";  // Add tab separator between arguments
        }
        output += s;
    }
    
    // Output to std::cout (which will be captured by ConsoleCapture in debug builds)
    std::cout << output << std::endl;
    
    return 0;
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

int LuaInterface::b2SetFixedTimestep(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    float timestep = lua_tonumber(L, 1);
    assert(timestep > 0.0f);

    interface->physics_->setFixedTimestep(timestep);
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

int LuaInterface::b2CreateRevoluteJoint(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    int numArgs = lua_gettop(L);
    assert(numArgs >= 6 && numArgs <= 9);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3) && lua_isnumber(L, 4));
    assert(lua_isnumber(L, 5) && lua_isnumber(L, 6));

    int bodyIdA = lua_tointeger(L, 1);
    int bodyIdB = lua_tointeger(L, 2);
    float anchorAx = lua_tonumber(L, 3);
    float anchorAy = lua_tonumber(L, 4);
    float anchorBx = lua_tonumber(L, 5);
    float anchorBy = lua_tonumber(L, 6);
    
    bool enableLimit = false;
    float lowerAngle = 0.0f;
    float upperAngle = 0.0f;
    
    if (numArgs >= 7) {
        assert(lua_isboolean(L, 7));
        enableLimit = lua_toboolean(L, 7);
    }
    if (numArgs >= 8) {
        assert(lua_isnumber(L, 8));
        lowerAngle = lua_tonumber(L, 8);
    }
    if (numArgs >= 9) {
        assert(lua_isnumber(L, 9));
        upperAngle = lua_tonumber(L, 9);
    }

    int jointId = interface->physics_->createRevoluteJoint(bodyIdA, bodyIdB, anchorAx, anchorAy, 
                                                            anchorBx, anchorBy, enableLimit, 
                                                            lowerAngle, upperAngle);
    lua_pushinteger(L, jointId);
    return 1;
}

int LuaInterface::b2DestroyJoint(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int jointId = lua_tointeger(L, 1);
    interface->physics_->destroyJoint(jointId);
    return 0;
}

// Scene layer Lua binding implementations

int LuaInterface::createLayer(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: textureId (integer), size (number), [normalMapId (integer)], pipelineId (integer)
    // Size is the larger dimension - width and height are calculated based on texture aspect ratio
    int numArgs = lua_gettop(L);
    assert(numArgs >= 3 && numArgs <= 4);
    assert(lua_isinteger(L, 1));
    assert(lua_isnumber(L, 2));

    uint64_t textureId = (uint64_t)lua_tointeger(L, 1);
    float size = (float)lua_tonumber(L, 2);

    // Get texture dimensions and calculate width/height based on aspect ratio
    uint32_t texWidth, texHeight;
    float width, height;

    if (!interface->renderer_.getTextureDimensions(textureId, &texWidth, &texHeight)) {
        // Texture not found, default to square
        width = height = size;
    } else {
        // Calculate dimensions preserving aspect ratio
        float aspectRatio = (float)texWidth / (float)texHeight;
        if (aspectRatio >= 1.0f) {
            // Width >= height (landscape or square)
            width = size;
            height = size / aspectRatio;
        } else {
            // Height > width (portrait)
            width = size * aspectRatio;
            height = size;
        }
    }

    uint64_t normalMapId = 0;
    int pipelineId = -1;

    if (numArgs == 3) {
        // 3 args: textureId, size, pipelineId
        assert(lua_isinteger(L, 3));
        pipelineId = (int)lua_tointeger(L, 3);
    } else if (numArgs == 4) {
        // 4 args: textureId, size, normalMapId, pipelineId
        assert(lua_isinteger(L, 3));
        assert(lua_isinteger(L, 4));
        normalMapId = (uint64_t)lua_tointeger(L, 3);
        pipelineId = (int)lua_tointeger(L, 4);
    }

    int layerId = interface->layerManager_->createLayer(textureId, width, height, normalMapId, pipelineId);
    lua_pushinteger(L, layerId);
    return 1;
}

int LuaInterface::destroyLayer(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isinteger(L, 1));

    int layerId = (int)lua_tointeger(L, 1);
    interface->layerManager_->destroyLayer(layerId);
    return 0;
}

int LuaInterface::attachLayerToBody(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: layerId (integer), bodyId (integer)
    assert(lua_gettop(L) == 2);
    assert(lua_isinteger(L, 1));
    assert(lua_isinteger(L, 2));

    int layerId = (int)lua_tointeger(L, 1);
    int bodyId = (int)lua_tointeger(L, 2);

    interface->layerManager_->attachLayerToBody(layerId, bodyId);
    return 0;
}

int LuaInterface::detachLayer(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isinteger(L, 1));

    int layerId = (int)lua_tointeger(L, 1);
    interface->layerManager_->detachLayer(layerId);
    return 0;
}

int LuaInterface::setLayerEnabled(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 2);
    assert(lua_isinteger(L, 1));
    assert(lua_isboolean(L, 2));

    int layerId = (int)lua_tointeger(L, 1);
    bool enabled = lua_toboolean(L, 2);

    interface->layerManager_->setLayerEnabled(layerId, enabled);
    return 0;
}

int LuaInterface::loadTexture(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Argument: textureFilename (string)
    assert(lua_gettop(L) == 1);
    assert(lua_isstring(L, 1));

    const char* filename = lua_tostring(L, 1);

    // Hash the filename to get texture ID (same as packer)
    uint64_t textureId = std::hash<std::string>{}(filename);

    // Load the texture from the pak file
    ResourceData imageData = interface->pakResource_.getResource(textureId);
    assert(imageData.data != nullptr && "Texture not found in pak file");

    // Load the texture into the renderer
    interface->renderer_.loadTexture(textureId, imageData);

    // Return the texture ID so it can be used in createLayer
    lua_pushinteger(L, textureId);
    return 1;
}

int LuaInterface::getTextureDimensions(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Argument: textureId (integer)
    assert(lua_gettop(L) == 1);
    assert(lua_isinteger(L, 1));

    uint64_t textureId = (uint64_t)lua_tointeger(L, 1);

    uint32_t width, height;
    if (interface->renderer_.getTextureDimensions(textureId, &width, &height)) {
        lua_pushinteger(L, width);
        lua_pushinteger(L, height);
        return 2;
    }

    // Return nil if texture not found
    lua_pushnil(L);
    return 1;
}

int LuaInterface::loadTexturedShaders(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: vertexShaderName (string), fragmentShaderName (string), zIndex (integer)
    assert(lua_gettop(L) == 3);
    assert(lua_isstring(L, 1));
    assert(lua_isstring(L, 2));
    assert(lua_isnumber(L, 3));

    const char* vertShaderName = lua_tostring(L, 1);
    const char* fragShaderName = lua_tostring(L, 2);
    int zIndex = (int)lua_tointeger(L, 3);

    uint64_t vertId = std::hash<std::string>{}(vertShaderName);
    uint64_t fragId = std::hash<std::string>{}(fragShaderName);

    ResourceData vertShader = interface->pakResource_.getResource(vertId);
    ResourceData fragShader = interface->pakResource_.getResource(fragId);

    assert(vertShader.data != nullptr);
    assert(fragShader.data != nullptr);

    int pipelineId = interface->pipelineIndex_++;
    interface->scenePipelines_[interface->currentSceneId_].push_back({pipelineId, zIndex});

    // Create textured pipeline
    interface->renderer_.createTexturedPipeline(pipelineId, vertShader, fragShader);

    // Return the pipeline ID so it can be used in createLayer
    lua_pushinteger(L, pipelineId);
    return 1;
}

int LuaInterface::loadTexturedShadersEx(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: vertexShaderName (string), fragmentShaderName (string), zIndex (integer), numTextures (integer)
    assert(lua_gettop(L) == 4);
    assert(lua_isstring(L, 1));
    assert(lua_isstring(L, 2));
    assert(lua_isnumber(L, 3));
    assert(lua_isnumber(L, 4));

    const char* vertShaderName = lua_tostring(L, 1);
    const char* fragShaderName = lua_tostring(L, 2);
    int zIndex = (int)lua_tointeger(L, 3);
    int numTextures = (int)lua_tointeger(L, 4);

    uint64_t vertId = std::hash<std::string>{}(vertShaderName);
    uint64_t fragId = std::hash<std::string>{}(fragShaderName);

    ResourceData vertShader = interface->pakResource_.getResource(vertId);
    ResourceData fragShader = interface->pakResource_.getResource(fragId);

    assert(vertShader.data != nullptr);
    assert(fragShader.data != nullptr);

    int pipelineId = interface->pipelineIndex_++;
    interface->scenePipelines_[interface->currentSceneId_].push_back({pipelineId, zIndex});

    // Create textured pipeline with specified number of textures
    interface->renderer_.createTexturedPipeline(pipelineId, vertShader, fragShader, numTextures);

    // Return the pipeline ID so it can be used in createLayer
    lua_pushinteger(L, pipelineId);
    return 1;
}

int LuaInterface::loadTexturedShadersAdditive(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: vertexShaderName (string), fragmentShaderName (string), zIndex (integer), numTextures (integer)
    assert(lua_gettop(L) == 4);
    assert(lua_isstring(L, 1));
    assert(lua_isstring(L, 2));
    assert(lua_isnumber(L, 3));
    assert(lua_isnumber(L, 4));

    const char* vertShaderName = lua_tostring(L, 1);
    const char* fragShaderName = lua_tostring(L, 2);
    int zIndex = (int)lua_tointeger(L, 3);
    int numTextures = (int)lua_tointeger(L, 4);

    uint64_t vertId = std::hash<std::string>{}(vertShaderName);
    uint64_t fragId = std::hash<std::string>{}(fragShaderName);

    ResourceData vertShader = interface->pakResource_.getResource(vertId);
    ResourceData fragShader = interface->pakResource_.getResource(fragId);

    assert(vertShader.data != nullptr);
    assert(fragShader.data != nullptr);

    int pipelineId = interface->pipelineIndex_++;
    interface->scenePipelines_[interface->currentSceneId_].push_back({pipelineId, zIndex});

    // Create textured pipeline with additive blending
    interface->renderer_.createTexturedPipelineAdditive(pipelineId, vertShader, fragShader, numTextures);

    // Return the pipeline ID so it can be used in createLayer
    lua_pushinteger(L, pipelineId);
    return 1;
}

int LuaInterface::setShaderUniform3f(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: x (number), y (number), z (number)
    assert(lua_gettop(L) == 3);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));

    float x = (float)lua_tonumber(L, 1);
    float y = (float)lua_tonumber(L, 2);
    float z = (float)lua_tonumber(L, 3);

    // This function is deprecated - setShaderParameters now requires a pipeline ID
    // For backward compatibility, this does nothing
    // Users should call setShaderParameters(pipelineId, x, y, z, ...) instead
    
    return 0;
}

int LuaInterface::setShaderParameters(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: pipelineId (number), then 3-7 float parameters
    // Minimum: pipelineId + 3 light position params (4 total)
    // Maximum: pipelineId + 7 params (8 total)
    int numArgs = lua_gettop(L);
    assert(numArgs >= 4 && numArgs <= 8);
    assert(lua_isnumber(L, 1));

    int pipelineId = (int)lua_tonumber(L, 1);
    
    // Read parameters, defaulting to 0.0 if not provided
    float params[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    int paramCount = numArgs - 1;  // Exclude pipelineId from count
    
    for (int i = 0; i < paramCount && i < 7; ++i) {
        assert(lua_isnumber(L, i + 2));
        params[i] = (float)lua_tonumber(L, i + 2);
    }

    interface->renderer_.setShaderParameters(pipelineId, paramCount, params);

    return 0;
}

// Audio Lua bindings

int LuaInterface::audioLoadBuffer(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: data (string/userdata), sampleRate (number), channels (number), bitsPerSample (number)
    assert(lua_gettop(L) == 4);
    assert(lua_isstring(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));
    assert(lua_isnumber(L, 4));

    size_t dataSize;
    const char* data = lua_tolstring(L, 1, &dataSize);
    int sampleRate = lua_tointeger(L, 2);
    int channels = lua_tointeger(L, 3);
    int bitsPerSample = lua_tointeger(L, 4);

    int bufferId = interface->audioManager_->loadAudioBufferFromMemory(data, dataSize, sampleRate, channels, bitsPerSample);

    lua_pushinteger(L, bufferId);
    return 1; // Return buffer ID
}

int LuaInterface::audioLoadOpus(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Argument: resource name (string)
    assert(lua_gettop(L) == 1);
    assert(lua_isstring(L, 1));

    const char* resourceName = lua_tostring(L, 1);

    // Hash the resource name to get its ID
    uint64_t resourceId = std::hash<std::string>{}(resourceName);

    // Load resource from pak
    ResourceData resourceData = interface->pakResource_.getResource(resourceId);
    if (!resourceData.data || resourceData.size == 0) {
        std::cerr << "Failed to load OPUS resource: " << resourceName << std::endl;
        lua_pushinteger(L, -1);
        return 1;
    }

    // Decode OPUS and load into audio buffer
    int bufferId = interface->audioManager_->loadOpusAudioFromMemory(resourceData.data, resourceData.size);

    lua_pushinteger(L, bufferId);
    return 1; // Return buffer ID
}

int LuaInterface::audioCreateSource(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: bufferId (number), looping (boolean, optional), volume (number, optional)
    int numArgs = lua_gettop(L);
    assert(numArgs >= 1 && numArgs <= 3);
    assert(lua_isnumber(L, 1));

    int bufferId = lua_tointeger(L, 1);
    bool looping = false;
    float volume = 1.0f;

    if (numArgs >= 2) {
        assert(lua_isboolean(L, 2));
        looping = lua_toboolean(L, 2);
    }
    if (numArgs >= 3) {
        assert(lua_isnumber(L, 3));
        volume = lua_tonumber(L, 3);
    }

    int sourceId = interface->audioManager_->createAudioSource(bufferId, looping, volume);

    lua_pushinteger(L, sourceId);
    return 1; // Return source ID
}

int LuaInterface::audioPlaySource(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int sourceId = lua_tointeger(L, 1);
    interface->audioManager_->playSource(sourceId);

    return 0;
}

int LuaInterface::audioStopSource(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int sourceId = lua_tointeger(L, 1);
    interface->audioManager_->stopSource(sourceId);

    return 0;
}

int LuaInterface::audioPauseSource(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int sourceId = lua_tointeger(L, 1);
    interface->audioManager_->pauseSource(sourceId);

    return 0;
}

int LuaInterface::audioSetSourcePosition(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 4);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));
    assert(lua_isnumber(L, 4));

    int sourceId = lua_tointeger(L, 1);
    float x = lua_tonumber(L, 2);
    float y = lua_tonumber(L, 3);
    float z = lua_tonumber(L, 4);

    interface->audioManager_->setSourcePosition(sourceId, x, y, z);

    return 0;
}

int LuaInterface::audioSetSourceVelocity(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 4);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));
    assert(lua_isnumber(L, 4));

    int sourceId = lua_tointeger(L, 1);
    float vx = lua_tonumber(L, 2);
    float vy = lua_tonumber(L, 3);
    float vz = lua_tonumber(L, 4);

    interface->audioManager_->setSourceVelocity(sourceId, vx, vy, vz);

    return 0;
}

int LuaInterface::audioSetSourceVolume(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 2);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));

    int sourceId = lua_tointeger(L, 1);
    float volume = lua_tonumber(L, 2);

    interface->audioManager_->setSourceVolume(sourceId, volume);

    return 0;
}

int LuaInterface::audioSetSourcePitch(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 2);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));

    int sourceId = lua_tointeger(L, 1);
    float pitch = lua_tonumber(L, 2);

    interface->audioManager_->setSourcePitch(sourceId, pitch);

    return 0;
}

int LuaInterface::audioSetSourceLooping(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 2);
    assert(lua_isnumber(L, 1));
    assert(lua_isboolean(L, 2));

    int sourceId = lua_tointeger(L, 1);
    bool looping = lua_toboolean(L, 2);

    interface->audioManager_->setSourceLooping(sourceId, looping);

    return 0;
}

int LuaInterface::audioReleaseSource(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int sourceId = lua_tointeger(L, 1);
    interface->audioManager_->releaseSource(sourceId);

    return 0;
}

int LuaInterface::audioIsSourcePlaying(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int sourceId = lua_tointeger(L, 1);
    bool playing = interface->audioManager_->isSourcePlaying(sourceId);

    lua_pushboolean(L, playing);
    return 1;
}

int LuaInterface::audioSetListenerPosition(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 3);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));

    float x = lua_tonumber(L, 1);
    float y = lua_tonumber(L, 2);
    float z = lua_tonumber(L, 3);

    interface->audioManager_->setListenerPosition(x, y, z);

    return 0;
}

int LuaInterface::audioSetListenerVelocity(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 3);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));

    float vx = lua_tonumber(L, 1);
    float vy = lua_tonumber(L, 2);
    float vz = lua_tonumber(L, 3);

    interface->audioManager_->setListenerVelocity(vx, vy, vz);

    return 0;
}

int LuaInterface::audioSetListenerOrientation(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 6);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));
    assert(lua_isnumber(L, 4));
    assert(lua_isnumber(L, 5));
    assert(lua_isnumber(L, 6));

    float atX = lua_tonumber(L, 1);
    float atY = lua_tonumber(L, 2);
    float atZ = lua_tonumber(L, 3);
    float upX = lua_tonumber(L, 4);
    float upY = lua_tonumber(L, 5);
    float upZ = lua_tonumber(L, 6);

    interface->audioManager_->setListenerOrientation(atX, atY, atZ, upX, upY, upZ);

    return 0;
}

int LuaInterface::audioSetGlobalVolume(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    float volume = lua_tonumber(L, 1);
    interface->audioManager_->setGlobalVolume(volume);

    return 0;
}

int LuaInterface::audioSetGlobalEffect(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    int numArgs = lua_gettop(L);
    assert(numArgs >= 1 && numArgs <= 2);
    assert(lua_isnumber(L, 1));

    int effect = lua_tointeger(L, 1);
    float intensity = 1.0f;

    if (numArgs >= 2) {
        assert(lua_isnumber(L, 2));
        intensity = lua_tonumber(L, 2);
    }

    interface->audioManager_->setGlobalEffect((AudioEffect)effect, intensity);

    return 0;
}

// vibrate(leftIntensity, rightIntensity, duration)
// Trigger controller vibration with specified intensities (0.0 to 1.0) and duration in milliseconds
int LuaInterface::vibrate(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    int numArgs = lua_gettop(L);
    assert(numArgs == 3);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));

    float leftIntensity = lua_tonumber(L, 1);
    float rightIntensity = lua_tonumber(L, 2);
    uint32_t duration = lua_tointeger(L, 3);

    if (interface->vibrationManager_) {
        interface->vibrationManager_->vibrate(leftIntensity, rightIntensity, duration);
    }

    return 0;
}

// vibrateTriggers(leftTrigger, rightTrigger, duration)
// Trigger DualSense trigger motor vibration with specified intensities (0.0 to 1.0) and duration in milliseconds
// Returns true if successful (controller supports trigger rumble)
int LuaInterface::vibrateTriggers(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    int numArgs = lua_gettop(L);
    assert(numArgs == 3);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));

    float leftTrigger = lua_tonumber(L, 1);
    float rightTrigger = lua_tonumber(L, 2);
    uint32_t duration = lua_tointeger(L, 3);

    bool success = false;
    if (interface->vibrationManager_) {
        success = interface->vibrationManager_->vibrateTriggers(leftTrigger, rightTrigger, duration);
    }

    lua_pushboolean(L, success);
    return 1;
}

// stopVibration()
// Stop all controller vibration
int LuaInterface::stopVibration(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (interface->vibrationManager_) {
        interface->vibrationManager_->stopVibration();
    }

    return 0;
}

