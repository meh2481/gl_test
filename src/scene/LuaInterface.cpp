#include "LuaInterface.h"
#include "SceneManager.h"
#include "../physics/Box2DPhysics.h"
#include "../memory/SmallAllocator.h"
#include "../core/hash.h"
#include <iostream>
#include <cassert>
#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

LuaInterface::LuaInterface(PakResource& pakResource, VulkanRenderer& renderer, MemoryAllocator* allocator,
                           Box2DPhysics* physics, SceneLayerManager* layerManager, AudioManager* audioManager,
                           ParticleSystemManager* particleManager, WaterEffectManager* waterEffectManager,
                           SceneManager* sceneManager, VibrationManager* vibrationManager)
    : pakResource_(pakResource), renderer_(renderer), sceneManager_(sceneManager), vibrationManager_(vibrationManager),
      pipelineIndex_(0), currentSceneId_(0),
      scenePipelines_(*allocator, "LuaInterface::scenePipelines"),
      waterFieldShaderMap_(*allocator, "LuaInterface::waterFieldShaderMap"),
      nodes_(*allocator, "LuaInterface::nodes"),
      bodyToNodeMap_(*allocator, "LuaInterface::bodyToNodeMap"),
      cursorX_(0.0f), cursorY_(0.0f), cameraOffsetX_(0.0f), cameraOffsetY_(0.0f), cameraZoom_(1.0f),
      nextNodeId_(1), stringAllocator_(allocator), sceneObjects_(*allocator, "LuaInterface::sceneObjects_"),
      physics_(physics), layerManager_(layerManager), audioManager_(audioManager),
      particleManager_(particleManager), waterEffectManager_(waterEffectManager) {
    assert(stringAllocator_ != nullptr);
    assert(physics_ != nullptr);
    assert(layerManager_ != nullptr);
    assert(audioManager_ != nullptr);
    assert(particleManager_ != nullptr);
    assert(waterEffectManager_ != nullptr);
    std::cout << "LuaInterface: Using shared memory allocator and pre-created managers" << std::endl;
    particleEditorPipelineIds_[0] = -1;
    particleEditorPipelineIds_[1] = -1;
    particleEditorPipelineIds_[2] = -1;
    luaState_ = luaL_newstate();
    luaL_openlibs(luaState_);

    audioManager_->initialize();

    // Set layer manager on physics so it can create fragment layers during fracture
    physics_->setLayerManager(layerManager_);

    registerFunctions();

    // Set sensor callback for water splash detection and node triggers
    physics_->setSensorCallback([this](const SensorEvent& event) { handleSensorEvent(event); });
}

LuaInterface::~LuaInterface() {
    if (luaState_) {
        lua_close(luaState_);
        luaState_ = nullptr;
    }

    // Clean up allocated nodes - manually destruct and free through allocator
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
        Node* node = it.value();
        assert(node != nullptr);
        node->~Node();  // Call destructor
        stringAllocator_->free(node);  // Free through allocator
    }
    nodes_.clear();

    // Clean up allocated scenePipelines vectors - manually destruct and free through allocator
    for (auto it = scenePipelines_.begin(); it != scenePipelines_.end(); ++it) {
        Vector<std::pair<int, int>>* vec = it.value();
        assert(vec != nullptr);
        vec->~Vector();  // Call destructor
        stringAllocator_->free(vec);  // Free through allocator
    }
    scenePipelines_.clear();

    // Don't delete stringAllocator_ - we don't own it anymore
    stringAllocator_ = nullptr;
}

void LuaInterface::handleSensorEvent(const SensorEvent& event) {
    if (event.sensorBodyId >= 0) {
        const auto& forceFields = physics_->getForceFields();
        for (auto it = forceFields.begin(); it != forceFields.end(); ++it) {
            const ForceField& fieldValue = it.value();
            if (fieldValue.bodyId == event.sensorBodyId) {
                const ForceField* field = &fieldValue;
                int forceFieldId = it.key();
                if (field->isWater) {
                    int waterFieldId = waterEffectManager_->findByPhysicsForceField(forceFieldId);
                    if (waterFieldId >= 0) {
                        const WaterForceField* waterField = waterEffectManager_->getWaterForceField(waterFieldId);
                        if (waterField) {
                            if (event.isBegin) {
                                // Load particle shaders if not loaded
                                lua_getglobal(luaState_, "loadParticleShaders");
                                lua_pushstring(luaState_, "res/shaders/particle_vertex.spv");
                                lua_pushstring(luaState_, "res/shaders/particle_fragment.spv");
                                lua_pushinteger(luaState_, 1); // Alpha blend
                                lua_pushboolean(luaState_, 1); // Has texture
                                if (lua_pcall(luaState_, 4, 1, 0) != LUA_OK) {
                                    const char* errorMsg = lua_tostring(luaState_, -1);
                                    std::cerr << "loadParticleShaders error: " << (errorMsg ? errorMsg : "unknown") << std::endl;
                                    lua_pop(luaState_, 1);
                                    assert(false);
                                    return;
                                }
                                int pipelineId = lua_tointeger(luaState_, -1);
                                lua_pop(luaState_, 1);
                                // Create splash particles
                                lua_getglobal(luaState_, "loadParticleConfig");
                                lua_pushstring(luaState_, "res/fx/splash1.lua");
                                if (lua_pcall(luaState_, 1, 1, 0) != LUA_OK) {
                                    const char* errorMsg = lua_tostring(luaState_, -1);
                                    std::cerr << "loadParticleConfig error: " << (errorMsg ? errorMsg : "unknown") << std::endl;
                                    lua_pop(luaState_, 1);
                                    assert(false);
                                    return;
                                }
                                int configRef = luaL_ref(luaState_, LUA_REGISTRYINDEX);
                                lua_getglobal(luaState_, "createParticleSystem");
                                lua_rawgeti(luaState_, LUA_REGISTRYINDEX, configRef);
                                lua_pushinteger(luaState_, pipelineId);
                                if (lua_pcall(luaState_, 2, 1, 0) != LUA_OK) {
                                    const char* errorMsg = lua_tostring(luaState_, -1);
                                    std::cerr << "createParticleSystem error: " << (errorMsg ? errorMsg : "unknown") << std::endl;
                                    lua_pop(luaState_, 1);
                                    luaL_unref(luaState_, LUA_REGISTRYINDEX, configRef);
                                    assert(false);
                                    return;
                                }
                                int particleSystemId = lua_tointeger(luaState_, -1);
                                lua_pop(luaState_, 1);
                                luaL_unref(luaState_, LUA_REGISTRYINDEX, configRef);
                                // Set position
                                lua_getglobal(luaState_, "setParticleSystemPosition");
                                lua_pushinteger(luaState_, particleSystemId);
                                lua_pushnumber(luaState_, event.visitorX);
                                lua_pushnumber(luaState_, waterField->config.surfaceY);
                                lua_pcall(luaState_, 3, 0, 0);
                            } else {
                                // Load particle shaders if not loaded
                                lua_getglobal(luaState_, "loadParticleShaders");
                                lua_pushstring(luaState_, "res/shaders/particle_vertex.spv");
                                lua_pushstring(luaState_, "res/shaders/particle_fragment.spv");
                                lua_pushinteger(luaState_, 1); // Alpha blend
                                lua_pushboolean(luaState_, 1); // Has texture
                                if (lua_pcall(luaState_, 4, 1, 0) != LUA_OK) {
                                    const char* errorMsg = lua_tostring(luaState_, -1);
                                    std::cerr << "loadParticleShaders error: " << (errorMsg ? errorMsg : "unknown") << std::endl;
                                    lua_pop(luaState_, 1);
                                    assert(false);
                                    return;
                                }
                                int pipelineId = lua_tointeger(luaState_, -1);
                                lua_pop(luaState_, 1);
                                // Create splash particles
                                lua_getglobal(luaState_, "loadParticleConfig");
                                lua_pushstring(luaState_, "res/fx/splash1.lua");
                                if (lua_pcall(luaState_, 1, 1, 0) != LUA_OK) {
                                    const char* errorMsg = lua_tostring(luaState_, -1);
                                    std::cerr << "loadParticleConfig error: " << (errorMsg ? errorMsg : "unknown") << std::endl;
                                    lua_pop(luaState_, 1);
                                    assert(false);
                                    return;
                                }
                                int configRef = luaL_ref(luaState_, LUA_REGISTRYINDEX);
                                lua_getglobal(luaState_, "createParticleSystem");
                                lua_rawgeti(luaState_, LUA_REGISTRYINDEX, configRef);
                                lua_pushinteger(luaState_, pipelineId);
                                if (lua_pcall(luaState_, 2, 1, 0) != LUA_OK) {
                                    const char* errorMsg = lua_tostring(luaState_, -1);
                                    std::cerr << "createParticleSystem error: " << (errorMsg ? errorMsg : "unknown") << std::endl;
                                    lua_pop(luaState_, 1);
                                    luaL_unref(luaState_, LUA_REGISTRYINDEX, configRef);
                                    assert(false);
                                    return;
                                }
                                int particleSystemId = lua_tointeger(luaState_, -1);
                                lua_pop(luaState_, 1);
                                luaL_unref(luaState_, LUA_REGISTRYINDEX, configRef);
                                // Set position
                                lua_getglobal(luaState_, "setParticleSystemPosition");
                                lua_pushinteger(luaState_, particleSystemId);
                                lua_pushnumber(luaState_, event.visitorX);
                                lua_pushnumber(luaState_, waterField->config.surfaceY);
                                lua_pcall(luaState_, 3, 0, 0);
                            }
                        }
                    }
                }
                break; // Found the field, no need to continue
            }
        }

        // Check if this is a node sensor
        handleNodeSensorEvent(event);
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
    const char* globalFunctions[] = {"loadShaders", "loadTexturedShaders", "loadTexturedShadersEx", "loadTexturedShadersAdditive", "loadAnimTexturedShaders", "loadTexture",
                                     "setShaderParameters",
                                     "pushScene", "popScene", "print",
                                     "b2SetGravity", "b2Step", "b2CreateBody", "b2DestroyBody",
                                     "b2AddBoxFixture", "b2AddCircleFixture", "b2AddPolygonFixture", "b2AddSegmentFixture", "b2ClearAllFixtures", "b2SetBodyPosition",
                                     "b2SetBodyAngle", "b2SetBodyLinearVelocity", "b2SetBodyAngularVelocity",
                                     "b2SetBodyAwake", "b2EnableBody", "b2DisableBody", "b2GetBodyPosition", "b2GetBodyAngle", "b2EnableDebugDraw",
                                     "b2CreateRevoluteJoint", "b2DestroyJoint",
                                     "b2QueryBodyAtPoint", "b2CreateMouseJoint", "b2UpdateMouseJointTarget", "b2DestroyMouseJoint",
                                     "b2SetBodyDestructible", "b2SetBodyDestructibleLayer", "b2ClearBodyDestructible", "b2CleanupAllFragments",
                                     "b2AddBodyType", "b2RemoveBodyType", "b2ClearBodyTypes", "b2BodyHasType", "b2GetBodyTypes", "b2SetCollisionCallback",
                                     "createForceField", "createRadialForceField", "getForceFieldBodyId",
                                     "createLayer", "destroyLayer", "attachLayerToBody", "setLayerOffset", "setLayerUseLocalUV", "setLayerPosition", "setLayerParallaxDepth", "setLayerScale",
                                     "setLayerSpin", "setLayerBlink", "setLayerWave", "setLayerColor", "setLayerColorCycle",
                                     "audioLoadOpus", "audioCreateSource", "audioPlaySource",
                                     "audioSetSourcePosition", "audioSetListenerPosition",
                                     "audioSetListenerOrientation", "audioSetGlobalVolume", "audioSetGlobalEffect",
                                     "getCursorPosition",
                                     "setCameraOffset", "setCameraZoom",
                                     "addLight", "updateLight", "removeLight", "setAmbientLight",
                                     "createParticleSystem", "destroyParticleSystem", "setParticleSystemPosition", "loadParticleShaders",
                                     "openParticleEditor", "loadParticleConfig", "loadObject", "createNode", "getNode", "destroyNode", "getNodePosition",
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
        "ACTION_EXIT", "ACTION_MENU", "ACTION_PHYSICS_DEMO", "ACTION_AUDIO_TEST", "ACTION_PARTICLE_EDITOR", "ACTION_TOGGLE_FULLSCREEN",
        "ACTION_HOTRELOAD", "ACTION_APPLY_FORCE", "ACTION_RESET_PHYSICS", "ACTION_TOGGLE_DEBUG_DRAW",
        "ACTION_DRAG_START", "ACTION_DRAG_END",
        "ACTION_PAN_START", "ACTION_PAN_END",
        "ACTION_TOGGLE_BLADE",
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

    // Update all tracked scene objects
    for (int objRef : sceneObjects_) {
        lua_rawgeti(luaState_, LUA_REGISTRYINDEX, objRef);
        if (lua_istable(luaState_, -1)) {
            lua_getfield(luaState_, -1, "update");
            if (lua_isfunction(luaState_, -1)) {
                lua_pushnumber(luaState_, deltaTime);
                if (lua_pcall(luaState_, 1, 0, 0) != LUA_OK) {
                    const char* errorMsg = lua_tostring(luaState_, -1);
                    std::cerr << "Object update error: " << (errorMsg ? errorMsg : "unknown error") << std::endl;
                    lua_pop(luaState_, 1);
                    assert(false);
                }
            } else {
                lua_pop(luaState_, 1); // Pop non-function
            }
        }
        lua_pop(luaState_, 1); // Pop object table
    }

    // Update audio manager (cleanup finished sources)
    audioManager_->update();

    // Update water effects and detect splashes
    waterEffectManager_->update(deltaTime);

    // Check for splash events - bodies crossing water surface
    if (waterEffectManager_->getActiveFieldCount() > 0) {
        static const int MAX_BODIES_TO_CHECK = 64;
        int bodyIds[MAX_BODIES_TO_CHECK];
        float posX[MAX_BODIES_TO_CHECK];
        float posY[MAX_BODIES_TO_CHECK];
        float velY[MAX_BODIES_TO_CHECK];
        int bodyCount = 0;

        physics_->getAllDynamicBodyInfo(bodyIds, posX, posY, velY, MAX_BODIES_TO_CHECK, &bodyCount);

        // Check each body against each water field
        const WaterForceField* fields = waterEffectManager_->getFields();
        for (int f = 0; f < MAX_WATER_FORCE_FIELDS; ++f) {
            if (!fields[f].active) continue;

            const WaterForceField& field = fields[f];
            float surfaceY = field.config.surfaceY;
            float minX = field.config.minX;
            float maxX = field.config.maxX;
            float minY = field.config.minY;

            for (int i = 0; i < bodyCount; ++i) {
                // Check if body is in horizontal range of water
                if (posX[i] < minX || posX[i] > maxX) continue;

                // Check if body is near or in water
                if (posY[i] < minY - 0.2f || posY[i] > surfaceY + 0.3f) continue;

                // Update tracked body for potential splash
                waterEffectManager_->updateTrackedBody(field.waterFieldId, bodyIds[i], posX[i], posY[i]);
            }

            // Update the shader with ripple data if this field has an associated shader
            int* pipelineIdPtr = waterFieldShaderMap_.find(field.waterFieldId);
            if (pipelineIdPtr != nullptr) {
                int pipelineId = *pipelineIdPtr;

                // Collect active ripples (amplitude > 0 and time < 3.0)
                ShaderRippleData shaderRipples[MAX_SHADER_RIPPLES];
                int shaderRippleCount = 0;

                for (int r = 0; r < field.rippleCount && shaderRippleCount < MAX_SHADER_RIPPLES; ++r) {
                    if (field.ripples[r].amplitude > 0.0f && field.ripples[r].time < 3.0f) {
                        shaderRipples[shaderRippleCount].x = field.ripples[r].x;
                        shaderRipples[shaderRippleCount].time = field.ripples[r].time;
                        shaderRipples[shaderRippleCount].amplitude = field.ripples[r].amplitude;
                        ++shaderRippleCount;
                    }
                }

                renderer_.setWaterRipples(pipelineId, shaderRippleCount, shaderRipples);
            }
        }
    }

    // Update nodes
    updateNodes(deltaTime);

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
        assert(false);
        return;
    }

    // Pop the table
    lua_pop(luaState_, 1);
}

void LuaInterface::cleanupScene(uint64_t sceneId) {
    // First, cleanup all tracked scene objects
    for (int objRef : sceneObjects_) {
        lua_rawgeti(luaState_, LUA_REGISTRYINDEX, objRef);
        if (lua_istable(luaState_, -1)) {
            lua_getfield(luaState_, -1, "cleanup");
            if (lua_isfunction(luaState_, -1)) {
                if (lua_pcall(luaState_, 0, 0, 0) != LUA_OK) {
                    const char* errorMsg = lua_tostring(luaState_, -1);
                    std::cerr << "Object cleanup error: " << (errorMsg ? errorMsg : "unknown error") << std::endl;
                    lua_pop(luaState_, 1);
                    assert(false);
                }
            } else {
                lua_pop(luaState_, 1); // Pop non-function
            }
        }
        lua_pop(luaState_, 1); // Pop object table
        luaL_unref(luaState_, LUA_REGISTRYINDEX, objRef); // Release the reference
    }
    sceneObjects_.clear();

    // Get the scene table from registry
    lua_pushinteger(luaState_, sceneId);
    lua_gettable(luaState_, LUA_REGISTRYINDEX);

    if (lua_istable(luaState_, -1)) {
        // Get the cleanup function from the table (optional)
        lua_getfield(luaState_, -1, "cleanup");
        if (lua_isfunction(luaState_, -1)) {
            // Call cleanup()
            if (lua_pcall(luaState_, 0, 0, 0) != LUA_OK) {
                const char* errorMsg = lua_tostring(luaState_, -1);
                std::cerr << "Lua cleanup error: " << (errorMsg ? errorMsg : "unknown error") << std::endl;
                lua_pop(luaState_, 1); // Pop error message
                assert(false);
            }
        } else {
            lua_pop(luaState_, 1); // Pop nil
        }
    }
    // Pop the table (or nil if scene not found)
    lua_pop(luaState_, 1);

    // Automatically cleanup all scene resources
    // Clear all audio sources
    audioManager_->clearAllSources();

    // Clear all particle systems
    particleManager_->clearAllSystems();

    // Clear all water effects
    waterEffectManager_->clear();

    // Clear water field shader mappings
    waterFieldShaderMap_.clear();

    // Clear all scene layers
    layerManager_->clear();

    // Reset physics (bodies, joints, force fields)
    physics_->reset();

    // Clear all nodes (frees String memory)
    std::cerr << "LuaInterface: Clearing " << nodes_.size() << " nodes" << std::endl;
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
        Node* node = it.value();
        assert(node != nullptr);
        node->~Node();  // Call destructor
        stringAllocator_->free(node);  // Free through allocator
    }
    nodes_.clear();
    bodyToNodeMap_.clear();

    // Clear all lights
    renderer_.clearLights();

    // Reset camera
    cameraOffsetX_ = 0.0f;
    cameraOffsetY_ = 0.0f;
    cameraZoom_ = 1.0f;
}

void LuaInterface::switchToScenePipeline(uint64_t sceneId) {
    std::cout << "LuaInterface::switchToScenePipeline: sceneId=" << sceneId << std::endl;
    Vector<std::pair<int, int>>** pipelinesPtr = scenePipelines_.find(sceneId);
    if (pipelinesPtr != nullptr) {
        assert(*pipelinesPtr != nullptr);
        Vector<std::pair<int, int>>* pipelines = *pipelinesPtr;
        // Sort pipelines by z-index (lower z-index drawn first)
        Vector<std::pair<int, int>> sortedPipelines(*stringAllocator_, "LuaInterface::switchToScenePipeline::sortedPipelines");
        for (const auto& pair : *pipelines) {
            sortedPipelines.push_back(pair);
        }
        sortedPipelines.sort([](const std::pair<int, int>& a, const std::pair<int, int>& b) {
            return a.second < b.second; // Sort by z-index ascending
        });

        // Extract just the pipeline IDs in sorted order
        Vector<uint64_t> pipelineIds(*stringAllocator_, "LuaInterface::switchToScenePipeline::pipelineIds");
        for (const auto& pair : sortedPipelines) {
            pipelineIds.push_back(pair.first);
        }

        renderer_.setPipelinesToDraw(pipelineIds);
        std::cout << "LuaInterface::switchToScenePipeline: set " << pipelineIds.size() << " pipelines" << std::endl;
    }
}

void LuaInterface::clearScenePipelines(uint64_t sceneId) {
    std::cout << "LuaInterface::clearScenePipelines: sceneId=" << sceneId << std::endl;
    Vector<std::pair<int, int>>** vecPtr = scenePipelines_.find(sceneId);
    if (vecPtr != nullptr) {
        Vector<std::pair<int, int>>* vec = *vecPtr;
        assert(vec != nullptr);
        vec->~Vector();  // Call destructor
        stringAllocator_->free(vec);  // Free through allocator
        scenePipelines_.remove(sceneId);
        std::cout << "LuaInterface::clearScenePipelines: cleared pipelines for sceneId " << sceneId << std::endl;
    }
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

    // Register Box2D functions
    lua_register(luaState_, "b2SetGravity", b2SetGravity);
    lua_register(luaState_, "b2Step", b2Step);
    lua_register(luaState_, "b2CreateBody", b2CreateBody);
    lua_register(luaState_, "b2DestroyBody", b2DestroyBody);
    lua_register(luaState_, "b2AddBoxFixture", b2AddBoxFixture);
    lua_register(luaState_, "b2AddCircleFixture", b2AddCircleFixture);
    lua_register(luaState_, "b2AddPolygonFixture", b2AddPolygonFixture);
    lua_register(luaState_, "b2AddSegmentFixture", b2AddSegmentFixture);
    lua_register(luaState_, "b2ClearAllFixtures", b2ClearAllFixtures);
    lua_register(luaState_, "b2SetBodyPosition", b2SetBodyPosition);
    lua_register(luaState_, "b2SetBodyAngle", b2SetBodyAngle);
    lua_register(luaState_, "b2SetBodyLinearVelocity", b2SetBodyLinearVelocity);
    lua_register(luaState_, "b2SetBodyAngularVelocity", b2SetBodyAngularVelocity);
    lua_register(luaState_, "b2SetBodyAwake", b2SetBodyAwake);
    lua_register(luaState_, "b2EnableBody", b2EnableBody);
    lua_register(luaState_, "b2DisableBody", b2DisableBody);
    lua_register(luaState_, "b2GetBodyPosition", b2GetBodyPosition);
    lua_register(luaState_, "b2GetBodyAngle", b2GetBodyAngle);
    lua_register(luaState_, "b2EnableDebugDraw", b2EnableDebugDraw);
    lua_register(luaState_, "b2CreateRevoluteJoint", b2CreateRevoluteJoint);
    lua_register(luaState_, "b2DestroyJoint", b2DestroyJoint);
    lua_register(luaState_, "b2QueryBodyAtPoint", b2QueryBodyAtPoint);
    lua_register(luaState_, "b2CreateMouseJoint", b2CreateMouseJoint);
    lua_register(luaState_, "b2UpdateMouseJointTarget", b2UpdateMouseJointTarget);
    lua_register(luaState_, "b2DestroyMouseJoint", b2DestroyMouseJoint);
    lua_register(luaState_, "b2SetBodyDestructible", b2SetBodyDestructible);
    lua_register(luaState_, "b2SetBodyDestructibleLayer", b2SetBodyDestructibleLayer);
    lua_register(luaState_, "b2ClearBodyDestructible", b2ClearBodyDestructible);
    lua_register(luaState_, "b2CleanupAllFragments", b2CleanupAllFragments);

    // Register body type system functions
    lua_register(luaState_, "b2AddBodyType", b2AddBodyType);
    lua_register(luaState_, "b2RemoveBodyType", b2RemoveBodyType);
    lua_register(luaState_, "b2ClearBodyTypes", b2ClearBodyTypes);
    lua_register(luaState_, "b2BodyHasType", b2BodyHasType);
    lua_register(luaState_, "b2GetBodyTypes", b2GetBodyTypes);
    lua_register(luaState_, "b2SetCollisionCallback", b2SetCollisionCallback);

    // Register force field functions
    lua_register(luaState_, "createForceField", createForceField);
    lua_register(luaState_, "createRadialForceField", createRadialForceField);
    lua_register(luaState_, "getForceFieldBodyId", getForceFieldBodyId);

    // Register scene layer functions
    lua_register(luaState_, "createLayer", createLayer);
    lua_register(luaState_, "destroyLayer", destroyLayer);
    lua_register(luaState_, "attachLayerToBody", attachLayerToBody);
    lua_register(luaState_, "setLayerOffset", setLayerOffset);
    lua_register(luaState_, "setLayerUseLocalUV", setLayerUseLocalUV);
    lua_register(luaState_, "setLayerPosition", setLayerPosition);
    lua_register(luaState_, "setLayerParallaxDepth", setLayerParallaxDepth);
    lua_register(luaState_, "setLayerScale", setLayerScale);

    // Register layer animation functions
    lua_register(luaState_, "setLayerSpin", setLayerSpin);
    lua_register(luaState_, "setLayerBlink", setLayerBlink);
    lua_register(luaState_, "setLayerWave", setLayerWave);
    lua_register(luaState_, "setLayerColor", setLayerColor);
    lua_register(luaState_, "setLayerColorCycle", setLayerColorCycle);

    // Register texture loading functions
    lua_register(luaState_, "loadTexture", loadTexture);
    lua_register(luaState_, "loadTexturedShaders", loadTexturedShaders);
    lua_register(luaState_, "loadTexturedShadersEx", loadTexturedShadersEx);
    lua_register(luaState_, "loadTexturedShadersAdditive", loadTexturedShadersAdditive);
    lua_register(luaState_, "loadAnimTexturedShaders", loadAnimTexturedShaders);
    lua_register(luaState_, "setShaderParameters", setShaderParameters);

    // Register audio functions
    lua_register(luaState_, "audioLoadOpus", audioLoadOpus);
    lua_register(luaState_, "audioCreateSource", audioCreateSource);
    lua_register(luaState_, "audioPlaySource", audioPlaySource);
    lua_register(luaState_, "audioSetSourcePosition", audioSetSourcePosition);
    lua_register(luaState_, "audioSetListenerPosition", audioSetListenerPosition);
    lua_register(luaState_, "audioSetListenerOrientation", audioSetListenerOrientation);
    lua_register(luaState_, "audioSetGlobalVolume", audioSetGlobalVolume);
    lua_register(luaState_, "audioSetGlobalEffect", audioSetGlobalEffect);

    // Register cursor position function
    lua_register(luaState_, "getCursorPosition", getCursorPosition);

    // Register camera functions
    lua_register(luaState_, "setCameraOffset", setCameraOffset);
    lua_register(luaState_, "setCameraZoom", setCameraZoom);

    // Register light management functions
    lua_register(luaState_, "addLight", addLight);
    lua_register(luaState_, "updateLight", updateLight);
    lua_register(luaState_, "removeLight", removeLight);
    lua_register(luaState_, "setAmbientLight", setAmbientLight);

    // Register particle system functions
    lua_register(luaState_, "createParticleSystem", createParticleSystem);
    lua_register(luaState_, "destroyParticleSystem", destroyParticleSystem);
    lua_register(luaState_, "setParticleSystemPosition", setParticleSystemPosition);
    lua_register(luaState_, "loadParticleShaders", loadParticleShaders);

    // Register particle editor function (available in DEBUG builds)
    lua_register(luaState_, "openParticleEditor", openParticleEditor);

    // Register particle config loading function
    lua_register(luaState_, "loadParticleConfig", loadParticleConfig);

    // Register object loading function
    lua_register(luaState_, "loadObject", loadObject);

    // Register node functions
    lua_register(luaState_, "createNode", createNode);
    lua_register(luaState_, "destroyNode", destroyNode);
    lua_register(luaState_, "getNodePosition", getNodePosition);

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
    lua_pushinteger(luaState_, ACTION_PARTICLE_EDITOR);
    lua_setglobal(luaState_, "ACTION_PARTICLE_EDITOR");
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
    lua_pushinteger(luaState_, ACTION_DRAG_START);
    lua_setglobal(luaState_, "ACTION_DRAG_START");
    lua_pushinteger(luaState_, ACTION_DRAG_END);
    lua_setglobal(luaState_, "ACTION_DRAG_END");
    lua_pushinteger(luaState_, ACTION_PAN_START);
    lua_setglobal(luaState_, "ACTION_PAN_START");
    lua_pushinteger(luaState_, ACTION_PAN_END);
    lua_setglobal(luaState_, "ACTION_PAN_END");
    lua_pushinteger(luaState_, ACTION_TOGGLE_BLADE);
    lua_setglobal(luaState_, "ACTION_TOGGLE_BLADE");

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

    // Check arguments - can be 2 or 3 (vertex, fragment, optional z-index/parallax depth)
    int numArgs = lua_gettop(L);
    assert(numArgs >= 2 && numArgs <= 3);
    assert(lua_isstring(L, 1) && lua_isstring(L, 2));
    if (numArgs >= 3) {
        assert(lua_isnumber(L, 3));
    }

    const char* vertFile = lua_tostring(L, 1);
    const char* fragFile = lua_tostring(L, 2);
    // z-index controls both draw order (lower = drawn first) and parallax depth
    // Lower z-index = background = drawn first, moves slower than camera (positive parallax)
    // Higher z-index = foreground = drawn last, moves faster than camera (negative parallax)
    // z-index 0 = no parallax (moves with objects)
    int zIndex = (numArgs >= 3) ? lua_tointeger(L, 3) : 0;
    // Invert z-index for parallax: lower z = positive parallax (slower), higher z = negative (faster)
    float parallaxDepth = (float)(-zIndex);

    // Check if this specific shader combination is already loaded for this scene
    std::cout << "LuaInterface::loadShaders: currentSceneId_=" << interface->currentSceneId_ << ", zIndex=" << zIndex << std::endl;
    Vector<std::pair<int, int>>** vecPtr = interface->scenePipelines_.find(interface->currentSceneId_);
    if (vecPtr == nullptr) {
        // Create new vector for this scene - allocate through allocator using placement new
        void* vectorMem = interface->stringAllocator_->allocate(sizeof(Vector<std::pair<int, int>>), "LuaInterface::loadShaders::Vector");
        assert(vectorMem != nullptr);
        Vector<std::pair<int, int>>* newVec = new (vectorMem) Vector<std::pair<int, int>>(*interface->stringAllocator_, "LuaInterface::loadShaders::data");
        interface->scenePipelines_.insertNew(interface->currentSceneId_, newVec);
        vecPtr = interface->scenePipelines_.find(interface->currentSceneId_);
        std::cout << "LuaInterface::loadShaders: created new vector for sceneId " << interface->currentSceneId_ << std::endl;
    }
    assert(*vecPtr != nullptr);
    Vector<std::pair<int, int>>* scenePipelines = *vecPtr;
    bool alreadyLoaded = false;
    for (const auto& pipeline : *scenePipelines) {
        // We can't easily check the actual shader files, but we can check if we already have
        // a pipeline with the same z-index (assuming each z-index should be unique)
        if (pipeline.second == zIndex) {
            alreadyLoaded = true;
            break;
        }
    }

    if (alreadyLoaded) {
        return 0; // No return values
    }

    // Hash filenames to get resource IDs
    uint64_t vertId = hashCString(vertFile);
    uint64_t fragId = hashCString(fragFile);

    std::cout << "Loading shaders: " << vertFile << ", " << fragFile << " (z-index: " << zIndex << ")" << std::endl;

    // Get shader data from pak file
    ResourceData vertShader = interface->pakResource_.getResource(vertId);
    ResourceData fragShader = interface->pakResource_.getResource(fragId);

    if (vertShader.size == 0 || fragShader.size == 0) {
        std::cerr << "Failed to load shader: " << vertFile << " or " << fragFile << std::endl;
        assert(false);
    }

    // Check if this is a debug shader (check for filename, not full path)
    String vertFileStr(vertFile, interface->stringAllocator_);
    bool isDebugPipeline = (vertFileStr.find("debug_vertex.spv") != String::npos);

    // Create pipeline
    int pipelineId = interface->pipelineIndex_;
    interface->renderer_.createPipeline(pipelineId, vertShader, fragShader, isDebugPipeline);

    // Set parallax depth based on z-index (non-zero values get parallax)
    if (parallaxDepth != 0.0f) {
        interface->renderer_.setPipelineParallaxDepth(pipelineId, parallaxDepth);
    }

    // Add to current scene's pipeline list with z-index
    scenePipelines->push_back({pipelineId, zIndex});
    interface->pipelineIndex_++;
    std::cout << "LuaInterface::loadShaders: added pipeline " << pipelineId << " with zIndex " << zIndex << std::endl;

    return 0; // No return values
}

int LuaInterface::luaPrint(lua_State* L) {
    // Get the LuaInterface instance from the Lua registry
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    int n = lua_gettop(L);  // Number of arguments
    String output(interface->stringAllocator_);

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
    std::cout << output.c_str() << std::endl;

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
    uint64_t sceneId = hashCString(sceneFile);

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

int LuaInterface::b2AddPolygonFixture(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: bodyId, vertices table, [density], [friction], [restitution]
    // vertices table format: {x1, y1, x2, y2, x3, y3, ...} (3-8 vertices)
    int numArgs = lua_gettop(L);
    assert(numArgs >= 2);
    assert(lua_isnumber(L, 1) && lua_istable(L, 2));

    int bodyId = lua_tointeger(L, 1);

    // Get vertices from table using indexed access for proper ordering
    float vertices[16]; // Max 8 vertices * 2 coords
    int tableLen = (int)lua_rawlen(L, 2);
    assert(tableLen >= 6 && tableLen <= 16); // 3-8 vertices (x,y pairs)

    for (int i = 1; i <= tableLen && i <= 16; ++i) {
        lua_rawgeti(L, 2, i);
        assert(lua_isnumber(L, -1));
        vertices[i - 1] = lua_tonumber(L, -1);
        lua_pop(L, 1);
    }

    int numVertices = tableLen / 2;

    float density = 1.0f;
    float friction = 0.3f;
    float restitution = 0.0f;

    if (numArgs >= 3) {
        assert(lua_isnumber(L, 3));
        density = lua_tonumber(L, 3);
    }
    if (numArgs >= 4) {
        assert(lua_isnumber(L, 4));
        friction = lua_tonumber(L, 4);
    }
    if (numArgs >= 5) {
        assert(lua_isnumber(L, 5));
        restitution = lua_tonumber(L, 5);
    }

    interface->physics_->addPolygonFixture(bodyId, vertices, numVertices, density, friction, restitution);
    return 0;
}

int LuaInterface::b2AddSegmentFixture(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: bodyId, x1, y1, x2, y2, [friction], [restitution]
    int numArgs = lua_gettop(L);
    assert(numArgs >= 5);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2) && lua_isnumber(L, 3) && lua_isnumber(L, 4) && lua_isnumber(L, 5));

    int bodyId = lua_tointeger(L, 1);
    float x1 = lua_tonumber(L, 2);
    float y1 = lua_tonumber(L, 3);
    float x2 = lua_tonumber(L, 4);
    float y2 = lua_tonumber(L, 5);
    float friction = 0.3f;
    float restitution = 0.0f;

    if (numArgs >= 6) {
        assert(lua_isnumber(L, 6));
        friction = lua_tonumber(L, 6);
    }
    if (numArgs >= 7) {
        assert(lua_isnumber(L, 7));
        restitution = lua_tonumber(L, 7);
    }

    interface->physics_->addSegmentFixture(bodyId, x1, y1, x2, y2, friction, restitution);
    return 0;
}

int LuaInterface::b2ClearAllFixtures(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int bodyId = lua_tointeger(L, 1);

    if (!interface->physics_->isBodyValid(bodyId)) {
        return 0;
    }

    interface->physics_->clearAllFixtures(bodyId);
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

    if (!interface->physics_->isBodyValid(bodyId)) {
        return 0;
    }

    interface->physics_->setBodyAwake(bodyId, awake);
    return 0;
}

int LuaInterface::b2EnableBody(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int bodyId = lua_tointeger(L, 1);

    if (!interface->physics_->isBodyValid(bodyId)) {
        return 0;
    }

    interface->physics_->enableBody(bodyId);
    return 0;
}

int LuaInterface::b2DisableBody(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int bodyId = lua_tointeger(L, 1);

    if (!interface->physics_->isBodyValid(bodyId)) {
        return 0;
    }

    interface->physics_->disableBody(bodyId);
    return 0;
}

int LuaInterface::b2GetBodyPosition(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int bodyId = lua_tointeger(L, 1);
    if (!interface->physics_->isBodyValid(bodyId)) {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }
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
    if (!interface->physics_->isBodyValid(bodyId)) {
        lua_pushnil(L);
        return 1;
    }
    float angle = interface->physics_->getBodyAngle(bodyId);

    lua_pushnumber(L, angle);
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

int LuaInterface::b2QueryBodyAtPoint(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 2);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2));

    float x = lua_tonumber(L, 1);
    float y = lua_tonumber(L, 2);

    int bodyId = interface->physics_->queryBodyAtPoint(x, y);
    lua_pushinteger(L, bodyId);
    return 1;
}

int LuaInterface::b2CreateMouseJoint(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    int numArgs = lua_gettop(L);
    assert(numArgs >= 3 && numArgs <= 4);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2) && lua_isnumber(L, 3));

    int bodyId = lua_tointeger(L, 1);
    float targetX = lua_tonumber(L, 2);
    float targetY = lua_tonumber(L, 3);
    float maxForce = 1000.0f;

    if (numArgs >= 4) {
        assert(lua_isnumber(L, 4));
        maxForce = lua_tonumber(L, 4);
    }

    int jointId = interface->physics_->createMouseJoint(bodyId, targetX, targetY, maxForce);
    lua_pushinteger(L, jointId);
    return 1;
}

int LuaInterface::b2UpdateMouseJointTarget(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 3);
    assert(lua_isnumber(L, 1) && lua_isnumber(L, 2) && lua_isnumber(L, 3));

    int jointId = lua_tointeger(L, 1);
    float targetX = lua_tonumber(L, 2);
    float targetY = lua_tonumber(L, 3);

    interface->physics_->updateMouseJointTarget(jointId, targetX, targetY);
    return 0;
}

int LuaInterface::b2DestroyMouseJoint(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int jointId = lua_tointeger(L, 1);
    interface->physics_->destroyMouseJoint(jointId);
    return 0;
}

int LuaInterface::b2SetBodyDestructible(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: bodyId, strength, brittleness, vertices table, textureId, normalMapId, pipelineId
    int numArgs = lua_gettop(L);
    assert(numArgs >= 5);
    assert(lua_isnumber(L, 1));  // bodyId
    assert(lua_isnumber(L, 2));  // strength
    assert(lua_isnumber(L, 3));  // brittleness
    assert(lua_istable(L, 4));   // vertices

    int bodyId = lua_tointeger(L, 1);
    float strength = lua_tonumber(L, 2);
    float brittleness = lua_tonumber(L, 3);

    // Get vertices from table - expects 6-16 elements (3-8 x,y vertex pairs)
    float vertices[16];
    int tableLen = (int)lua_rawlen(L, 4);
    assert(tableLen >= 6 && tableLen <= 16);

    for (int i = 1; i <= tableLen && i <= 16; ++i) {
        lua_rawgeti(L, 4, i);
        assert(lua_isnumber(L, -1));
        vertices[i - 1] = lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    int vertexCount = tableLen / 2;

    uint64_t textureId = 0;
    uint64_t normalMapId = 0;
    int pipelineId = -1;

    if (numArgs >= 5) {
        assert(lua_isnumber(L, 5));
        textureId = (uint64_t)lua_tointeger(L, 5);
    }
    if (numArgs >= 6) {
        assert(lua_isnumber(L, 6));
        normalMapId = (uint64_t)lua_tointeger(L, 6);
    }
    if (numArgs >= 7) {
        assert(lua_isnumber(L, 7));
        pipelineId = (int)lua_tointeger(L, 7);
    }

    interface->physics_->setBodyDestructible(bodyId, strength, brittleness,
                                              vertices, vertexCount,
                                              textureId, normalMapId, pipelineId);

    // Check if texture uses atlas and set atlas UV info if so
    AtlasUV atlasUV;
    if (interface->pakResource_.getAtlasUV(textureId, atlasUV)) {
        interface->physics_->setBodyDestructibleAtlasUV(
            bodyId,
            atlasUV.atlasId,
            atlasUV.u0, atlasUV.v0, atlasUV.u1, atlasUV.v1
        );
    }

    // Check if normal map uses atlas and set atlas UV info if so
    if (normalMapId > 0) {
        AtlasUV normalAtlasUV;
        if (interface->pakResource_.getAtlasUV(normalMapId, normalAtlasUV)) {
            interface->physics_->setBodyDestructibleNormalMapAtlasUV(
                bodyId,
                normalAtlasUV.atlasId,
                normalAtlasUV.u0, normalAtlasUV.v0, normalAtlasUV.u1, normalAtlasUV.v1
            );
        }
    }

    return 0;
}

int LuaInterface::b2ClearBodyDestructible(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int bodyId = lua_tointeger(L, 1);
    interface->physics_->clearBodyDestructible(bodyId);
    return 0;
}

int LuaInterface::b2SetBodyDestructibleLayer(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 2);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));

    int bodyId = lua_tointeger(L, 1);
    int layerId = lua_tointeger(L, 2);
    interface->physics_->setBodyDestructibleLayer(bodyId, layerId);
    return 0;
}

int LuaInterface::b2CleanupAllFragments(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 0);

    interface->physics_->cleanupAllFragments();
    return 0;
}

// Force field Lua binding implementations

int LuaInterface::createForceField(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: vertices table, forceX (number), forceY (number), [water (boolean)], [damping (number)]
    // vertices table format: {x1, y1, x2, y2, x3, y3, ...} (3-8 vertices)
    int numArgs = lua_gettop(L);
    assert(numArgs >= 3 && numArgs <= 5);
    assert(lua_istable(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));

    // Get vertices from table
    float vertices[16]; // Max 8 vertices * 2 coords
    int tableLen = (int)lua_rawlen(L, 1);
    assert(tableLen >= 6 && tableLen <= 16); // 3-8 vertices (x,y pairs)

    for (int i = 0; i < tableLen; ++i) {
        lua_rawgeti(L, 1, i + 1);
        assert(lua_isnumber(L, -1));
        vertices[i] = lua_tonumber(L, -1);
        lua_pop(L, 1);
    }

    int vertexCount = tableLen / 2;
    float forceX = lua_tonumber(L, 2);
    float forceY = lua_tonumber(L, 3);

    // Optional water parameter (4th argument)
    bool water = false;
    if (numArgs >= 4 && lua_isboolean(L, 4)) {
        water = lua_toboolean(L, 4);
    }

    // Optional damping parameter (5th argument)
    // Default damping value provides subtle water drag
    float damping = 0.0f;
    if (numArgs >= 5 && lua_isnumber(L, 5)) {
        damping = lua_tonumber(L, 5);
    }

    int forceFieldId = interface->physics_->createForceField(vertices, vertexCount, forceX, forceY, damping, water);

    if (water) {
        // Calculate bounds from vertices
        float minX = vertices[0], maxX = vertices[0];
        float minY = vertices[1], maxY = vertices[1];
        for (int i = 1; i < vertexCount; ++i) {
            float x = vertices[i * 2];
            float y = vertices[i * 2 + 1];
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
        }

        // Default water visual parameters
        float alpha = 0.75f;
        float rippleAmplitude = 0.025f;
        float rippleSpeed = 2.0f;

        // Create water visual effect
        int waterFieldId = interface->waterEffectManager_->createWaterForceField(
            forceFieldId, minX, minY, maxX, maxY, alpha, rippleAmplitude, rippleSpeed);

        // Set up all water visuals automatically
        interface->setupWaterVisuals(forceFieldId, waterFieldId,
                                      minX, minY, maxX, maxY,
                                      alpha, rippleAmplitude, rippleSpeed);
    }

    lua_pushinteger(L, forceFieldId);
    return 1;
}

int LuaInterface::createRadialForceField(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: centerX, centerY, radius, forceAtCenter, forceAtEdge
    assert(lua_gettop(L) == 5);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));
    assert(lua_isnumber(L, 4));
    assert(lua_isnumber(L, 5));

    float centerX = lua_tonumber(L, 1);
    float centerY = lua_tonumber(L, 2);
    float radius = lua_tonumber(L, 3);
    float forceAtCenter = lua_tonumber(L, 4);
    float forceAtEdge = lua_tonumber(L, 5);

    int forceFieldId = interface->physics_->createRadialForceField(centerX, centerY, radius, forceAtCenter, forceAtEdge);
    lua_pushinteger(L, forceFieldId);
    return 1;
}

int LuaInterface::getForceFieldBodyId(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    int forceFieldId = luaL_checkinteger(L, 1);

    const ForceField* field = interface->physics_->getForceField(forceFieldId);
    if (field) {
        lua_pushinteger(L, field->bodyId);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

// Water force field Lua binding implementations

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

    // Check if this texture uses atlas
    AtlasUV atlasUV;
    bool usesAtlas = interface->pakResource_.getAtlasUV(textureId, atlasUV);

    // Get texture dimensions and calculate width/height based on aspect ratio
    uint32_t texWidth, texHeight;
    float width, height;

    if (usesAtlas && atlasUV.width > 0 && atlasUV.height > 0) {
        // Use atlas entry dimensions
        texWidth = atlasUV.width;
        texHeight = atlasUV.height;
    } else if (!interface->renderer_.getTextureDimensions(textureId, &texWidth, &texHeight)) {
        // Texture not found, default to square using the requested size
        // This allows the layer to be created even if texture is missing
        texWidth = texHeight = 1;
    }

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

    // Set atlas UV coordinates if applicable
    if (usesAtlas) {
        interface->layerManager_->setLayerAtlasUV(layerId, atlasUV.atlasId, atlasUV.u0, atlasUV.v0, atlasUV.u1, atlasUV.v1);
    }

    // Check if normal map uses atlas
    if (normalMapId != 0) {
        AtlasUV normalAtlasUV;
        if (interface->pakResource_.getAtlasUV(normalMapId, normalAtlasUV)) {
            interface->layerManager_->setLayerNormalMapAtlasUV(layerId, normalAtlasUV.atlasId,
                normalAtlasUV.u0, normalAtlasUV.v0, normalAtlasUV.u1, normalAtlasUV.v1);
        }
    }

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

int LuaInterface::setLayerOffset(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 3);
    assert(lua_isinteger(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));

    int layerId = (int)lua_tointeger(L, 1);
    float offsetX = lua_tonumber(L, 2);
    float offsetY = lua_tonumber(L, 3);

    interface->layerManager_->setLayerOffset(layerId, offsetX, offsetY);
    return 0;
}

int LuaInterface::setLayerUseLocalUV(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 2);
    assert(lua_isinteger(L, 1));
    assert(lua_isboolean(L, 2));

    int layerId = (int)lua_tointeger(L, 1);
    bool useLocal = lua_toboolean(L, 2);

    interface->layerManager_->setLayerUseLocalUV(layerId, useLocal);
    return 0;
}

int LuaInterface::setLayerPosition(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: layerId (integer), x (number), y (number), [angle (number)]
    int numArgs = lua_gettop(L);
    assert(numArgs >= 3 && numArgs <= 4);
    assert(lua_isinteger(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));

    int layerId = (int)lua_tointeger(L, 1);
    float x = (float)lua_tonumber(L, 2);
    float y = (float)lua_tonumber(L, 3);
    float angle = 0.0f;

    if (numArgs >= 4) {
        assert(lua_isnumber(L, 4));
        angle = (float)lua_tonumber(L, 4);
    }

    interface->layerManager_->setLayerPosition(layerId, x, y, angle * M_PI / 180.0f);
    return 0;
}

int LuaInterface::setLayerParallaxDepth(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: layerId (integer), depth (number)
    // depth < 0: foreground (moves faster than camera)
    // depth > 0: background (moves slower than camera)
    assert(lua_gettop(L) == 2);
    assert(lua_isinteger(L, 1));
    assert(lua_isnumber(L, 2));

    int layerId = (int)lua_tointeger(L, 1);
    float depth = (float)lua_tonumber(L, 2);

    interface->layerManager_->setLayerParallaxDepth(layerId, depth);
    return 0;
}

int LuaInterface::setLayerScale(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: layerId (integer), scaleX (number), scaleY (number)
    assert(lua_gettop(L) == 3);
    assert(lua_isinteger(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));

    int layerId = (int)lua_tointeger(L, 1);
    float scaleX = (float)lua_tonumber(L, 2);
    float scaleY = (float)lua_tonumber(L, 3);

    interface->layerManager_->setLayerScale(layerId, scaleX, scaleY);
    return 0;
}

int LuaInterface::setLayerSpin(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: layerId (integer), degreesPerSecond (number)
    assert(lua_gettop(L) == 2);
    assert(lua_isinteger(L, 1));
    assert(lua_isnumber(L, 2));

    int layerId = (int)lua_tointeger(L, 1);
    float degreesPerSecond = (float)lua_tonumber(L, 2);

    interface->layerManager_->setLayerSpin(layerId, degreesPerSecond);
    return 0;
}

int LuaInterface::setLayerBlink(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: layerId (integer), secondsOn, secondsOff, riseTime, fallTime
    assert(lua_gettop(L) == 5);
    assert(lua_isinteger(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));
    assert(lua_isnumber(L, 4));
    assert(lua_isnumber(L, 5));

    int layerId = (int)lua_tointeger(L, 1);
    float secondsOn = (float)lua_tonumber(L, 2);
    float secondsOff = (float)lua_tonumber(L, 3);
    float riseTime = (float)lua_tonumber(L, 4);
    float fallTime = (float)lua_tonumber(L, 5);

    interface->layerManager_->setLayerBlink(layerId, secondsOn, secondsOff, riseTime, fallTime);
    return 0;
}

int LuaInterface::setLayerWave(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: layerId (integer), wavelength, speed, angle, amplitude
    assert(lua_gettop(L) == 5);
    assert(lua_isinteger(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));
    assert(lua_isnumber(L, 4));
    assert(lua_isnumber(L, 5));

    int layerId = (int)lua_tointeger(L, 1);
    float wavelength = (float)lua_tonumber(L, 2);
    float speed = (float)lua_tonumber(L, 3);
    float angle = (float)lua_tonumber(L, 4);
    float amplitude = (float)lua_tonumber(L, 5);

    interface->layerManager_->setLayerWave(layerId, wavelength, speed, angle, amplitude);
    return 0;
}

int LuaInterface::setLayerColor(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: layerId (integer), r, g, b, a
    assert(lua_gettop(L) == 5);
    assert(lua_isinteger(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));
    assert(lua_isnumber(L, 4));
    assert(lua_isnumber(L, 5));

    int layerId = (int)lua_tointeger(L, 1);
    float r = (float)lua_tonumber(L, 2);
    float g = (float)lua_tonumber(L, 3);
    float b = (float)lua_tonumber(L, 4);
    float a = (float)lua_tonumber(L, 5);

    interface->layerManager_->setLayerColor(layerId, r, g, b, a);
    return 0;
}

int LuaInterface::setLayerColorCycle(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: layerId (integer), r1, g1, b1, a1, r2, g2, b2, a2, cycleTime
    assert(lua_gettop(L) == 10);
    assert(lua_isinteger(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));
    assert(lua_isnumber(L, 4));
    assert(lua_isnumber(L, 5));
    assert(lua_isnumber(L, 6));
    assert(lua_isnumber(L, 7));
    assert(lua_isnumber(L, 8));
    assert(lua_isnumber(L, 9));
    assert(lua_isnumber(L, 10));

    int layerId = (int)lua_tointeger(L, 1);
    float r1 = (float)lua_tonumber(L, 2);
    float g1 = (float)lua_tonumber(L, 3);
    float b1 = (float)lua_tonumber(L, 4);
    float a1 = (float)lua_tonumber(L, 5);
    float r2 = (float)lua_tonumber(L, 6);
    float g2 = (float)lua_tonumber(L, 7);
    float b2 = (float)lua_tonumber(L, 8);
    float a2 = (float)lua_tonumber(L, 9);
    float cycleTime = (float)lua_tonumber(L, 10);

    interface->layerManager_->setLayerColorCycle(layerId, r1, g1, b1, a1, r2, g2, b2, a2, cycleTime);
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
    uint64_t textureId = hashCString(filename);

    std::cout << "Loading texture: " << filename << " (id: " << textureId << ")" << std::endl;

    // Load the texture from the pak file
    ResourceData imageData = interface->pakResource_.getResource(textureId);
    if (imageData.data == nullptr) {
        std::cerr << "Texture not found in pak file: " << filename << std::endl;
        assert(false);
    }

    // Check if this is an atlas reference (TextureHeader) or a standalone image (ImageHeader)
    AtlasUV atlasUV;
    if (interface->pakResource_.getAtlasUV(textureId, atlasUV)) {
        // This is an atlas reference - load the atlas texture instead
        std::cout << "  -> Atlas reference (atlas id: " << atlasUV.atlasId << ", UV: " << atlasUV.u0 << "," << atlasUV.v0 << " - " << atlasUV.u1 << "," << atlasUV.v1 << ")" << std::endl;
        ResourceData atlasData = interface->pakResource_.getResource(atlasUV.atlasId);
        if (atlasData.data == nullptr) {
            std::cerr << "Atlas not found in pak file for texture: " << filename << std::endl;
            assert(false);
        }

        // Load the atlas texture (if not already loaded)
        interface->renderer_.loadAtlasTexture(atlasUV.atlasId, atlasData);

        // For now, we use the atlas ID for rendering
        // The UV coordinates are stored in the atlas entry and will be used by SceneLayer
    } else {
        // Standalone image - load directly
        std::cout << "  -> Standalone texture" << std::endl;
        interface->renderer_.loadTexture(textureId, imageData);
    }

    // Return the texture ID so it can be used in createLayer
    lua_pushinteger(L, textureId);
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

    uint64_t vertId = hashCString(vertShaderName);
    uint64_t fragId = hashCString(fragShaderName);

    ResourceData vertShader = interface->pakResource_.getResource(vertId);
    ResourceData fragShader = interface->pakResource_.getResource(fragId);

    assert(vertShader.data != nullptr);
    assert(fragShader.data != nullptr);

    int pipelineId = interface->pipelineIndex_++;
    std::cout << "LuaInterface::loadTexturedShaders: currentSceneId_=" << interface->currentSceneId_ << ", zIndex=" << zIndex << std::endl;
    Vector<std::pair<int, int>>** vecPtr = interface->scenePipelines_.find(interface->currentSceneId_);
    if (vecPtr == nullptr) {
        void* vectorMem = interface->stringAllocator_->allocate(sizeof(Vector<std::pair<int, int>>), "LuaInterface::loadTexturedShaders::Vector");
        assert(vectorMem != nullptr);
        Vector<std::pair<int, int>>* newVec = new (vectorMem) Vector<std::pair<int, int>>(*interface->stringAllocator_, "LuaInterface::loadTexturedShaders::data");
        interface->scenePipelines_.insertNew(interface->currentSceneId_, newVec);
        vecPtr = interface->scenePipelines_.find(interface->currentSceneId_);
        std::cout << "LuaInterface::loadTexturedShaders: created new vector for sceneId " << interface->currentSceneId_ << std::endl;
    }
    assert(*vecPtr != nullptr);
    (*vecPtr)->push_back({pipelineId, zIndex});
    std::cout << "LuaInterface::loadTexturedShaders: added pipeline " << pipelineId << " with zIndex " << zIndex << std::endl;

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

    uint64_t vertId = hashCString(vertShaderName);
    uint64_t fragId = hashCString(fragShaderName);

    ResourceData vertShader = interface->pakResource_.getResource(vertId);
    ResourceData fragShader = interface->pakResource_.getResource(fragId);

    assert(vertShader.data != nullptr);
    assert(fragShader.data != nullptr);

    int pipelineId = interface->pipelineIndex_++;
    std::cout << "LuaInterface::loadTexturedShadersEx: currentSceneId_=" << interface->currentSceneId_ << ", zIndex=" << zIndex << std::endl;
    Vector<std::pair<int, int>>** vecPtr = interface->scenePipelines_.find(interface->currentSceneId_);
    if (vecPtr == nullptr) {
        void* vectorMem = interface->stringAllocator_->allocate(sizeof(Vector<std::pair<int, int>>), "LuaInterface::loadTexturedShadersEx::Vector");
        assert(vectorMem != nullptr);
        Vector<std::pair<int, int>>* newVec = new (vectorMem) Vector<std::pair<int, int>>(*interface->stringAllocator_, "LuaInterface::loadTexturedShadersEx::data");
        interface->scenePipelines_.insertNew(interface->currentSceneId_, newVec);
        vecPtr = interface->scenePipelines_.find(interface->currentSceneId_);
        std::cout << "LuaInterface::loadTexturedShadersEx: created new vector for sceneId " << interface->currentSceneId_ << std::endl;
    }
    assert(*vecPtr != nullptr);
    (*vecPtr)->push_back({pipelineId, zIndex});
    std::cout << "LuaInterface::loadTexturedShadersEx: added pipeline " << pipelineId << " with zIndex " << zIndex << std::endl;

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

    uint64_t vertId = hashCString(vertShaderName);
    uint64_t fragId = hashCString(fragShaderName);

    ResourceData vertShader = interface->pakResource_.getResource(vertId);
    ResourceData fragShader = interface->pakResource_.getResource(fragId);

    assert(vertShader.data != nullptr);
    assert(fragShader.data != nullptr);

    int pipelineId = interface->pipelineIndex_++;
    std::cout << "LuaInterface::loadTexturedShadersAdditive: currentSceneId_=" << interface->currentSceneId_ << ", zIndex=" << zIndex << std::endl;
    Vector<std::pair<int, int>>** vecPtr = interface->scenePipelines_.find(interface->currentSceneId_);
    if (vecPtr == nullptr) {
        void* vectorMem = interface->stringAllocator_->allocate(sizeof(Vector<std::pair<int, int>>), "LuaInterface::loadTexturedShadersAdditive::Vector");
        assert(vectorMem != nullptr);
        Vector<std::pair<int, int>>* newVec = new (vectorMem) Vector<std::pair<int, int>>(*interface->stringAllocator_, "LuaInterface::loadTexturedShadersAdditive::data");
        interface->scenePipelines_.insertNew(interface->currentSceneId_, newVec);
        vecPtr = interface->scenePipelines_.find(interface->currentSceneId_);
        std::cout << "LuaInterface::loadTexturedShadersAdditive: created new vector for sceneId " << interface->currentSceneId_ << std::endl;
    }
    assert(*vecPtr != nullptr);
    (*vecPtr)->push_back({pipelineId, zIndex});
    std::cout << "LuaInterface::loadTexturedShadersAdditive: added pipeline " << pipelineId << " with zIndex " << zIndex << std::endl;

    // Create textured pipeline with additive blending
    interface->renderer_.createTexturedPipelineAdditive(pipelineId, vertShader, fragShader, numTextures);

    // Return the pipeline ID so it can be used in createLayer
    lua_pushinteger(L, pipelineId);
    return 1;
}

int LuaInterface::loadAnimTexturedShaders(lua_State* L) {
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

    uint64_t vertId = hashCString(vertShaderName);
    uint64_t fragId = hashCString(fragShaderName);

    ResourceData vertShader = interface->pakResource_.getResource(vertId);
    ResourceData fragShader = interface->pakResource_.getResource(fragId);

    assert(vertShader.data != nullptr);
    assert(fragShader.data != nullptr);

    int pipelineId = interface->pipelineIndex_++;
    std::cout << "LuaInterface::loadAnimTexturedShaders: currentSceneId_=" << interface->currentSceneId_ << ", zIndex=" << zIndex << std::endl;
    Vector<std::pair<int, int>>** vecPtr = interface->scenePipelines_.find(interface->currentSceneId_);
    if (vecPtr == nullptr) {
        void* vectorMem = interface->stringAllocator_->allocate(sizeof(Vector<std::pair<int, int>>), "LuaInterface::loadAnimTexturedShaders::Vector");
        assert(vectorMem != nullptr);
        Vector<std::pair<int, int>>* newVec = new (vectorMem) Vector<std::pair<int, int>>(*interface->stringAllocator_, "LuaInterface::loadAnimTexturedShaders::data");
        interface->scenePipelines_.insertNew(interface->currentSceneId_, newVec);
        vecPtr = interface->scenePipelines_.find(interface->currentSceneId_);
        std::cout << "LuaInterface::loadAnimTexturedShaders: created new vector for sceneId " << interface->currentSceneId_ << std::endl;
    }
    assert(*vecPtr != nullptr);
    (*vecPtr)->push_back({pipelineId, zIndex});
    std::cout << "LuaInterface::loadAnimTexturedShaders: added pipeline " << pipelineId << " with zIndex " << zIndex << std::endl;

    // Create animated textured pipeline with extended push constants
    interface->renderer_.createAnimTexturedPipeline(pipelineId, vertShader, fragShader, numTextures);

    // Return the pipeline ID so it can be used in createLayer
    lua_pushinteger(L, pipelineId);
    return 1;
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

int LuaInterface::audioLoadOpus(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Argument: resource name (string)
    assert(lua_gettop(L) == 1);
    assert(lua_isstring(L, 1));

    const char* resourceName = lua_tostring(L, 1);

    // Hash the resource name to get its ID
    uint64_t resourceId = hashCString(resourceName);

    // Load resource from pak
    ResourceData resourceData = interface->pakResource_.getResource(resourceId);
    if (!resourceData.data || resourceData.size == 0) {
        std::cerr << "Failed to load OPUS resource: " << resourceName << std::endl;
        lua_pushinteger(L, -1);
        assert(false);
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
// vibrateTriggers(leftTrigger, rightTrigger, duration)
// Trigger DualSense trigger motor vibration with specified intensities (0.0 to 1.0) and duration in milliseconds
// Returns true if successful (controller supports trigger rumble)
// stopVibration()
// Stop all controller vibration
// getCursorPosition()
// Returns current cursor position in world coordinates (x, y)
int LuaInterface::getCursorPosition(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    lua_pushnumber(L, interface->cursorX_);
    lua_pushnumber(L, interface->cursorY_);
    return 2;
}

// getCameraOffset()
// Returns current camera offset in world coordinates (x, y)
// setCameraOffset(x, y)
// Sets camera offset in world coordinates
int LuaInterface::setCameraOffset(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 2);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));

    interface->cameraOffsetX_ = lua_tonumber(L, 1);
    interface->cameraOffsetY_ = lua_tonumber(L, 2);
    return 0;
}

// getCameraZoom()
// Returns current camera zoom level
// setCameraZoom(zoom)
// Sets camera zoom level (1.0 = normal, >1.0 = zoomed in, <1.0 = zoomed out)
int LuaInterface::setCameraZoom(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    float zoom = lua_tonumber(L, 1);
    if (zoom > 0.0f) {
        interface->cameraZoom_ = zoom;
    }
    return 0;
}

// Reflection Lua bindings

// Camera zoom constants
static const float ZOOM_SCROLL_FACTOR = 1.1f;
static const float MIN_CAMERA_ZOOM = 0.1f;
static const float MAX_CAMERA_ZOOM = 10.0f;

// applyScrollZoom applies zoom based on scroll delta
void LuaInterface::applyScrollZoom(float scrollDelta) {
    if (scrollDelta > 0) {
        cameraZoom_ *= ZOOM_SCROLL_FACTOR;
    } else if (scrollDelta < 0) {
        cameraZoom_ /= ZOOM_SCROLL_FACTOR;
    } else {
        return; // No change needed for zero scroll
    }
    // Clamp zoom to reasonable values
    if (cameraZoom_ < MIN_CAMERA_ZOOM) cameraZoom_ = MIN_CAMERA_ZOOM;
    if (cameraZoom_ > MAX_CAMERA_ZOOM) cameraZoom_ = MAX_CAMERA_ZOOM;
}


// Light management Lua bindings
int LuaInterface::addLight(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: x, y, z, r, g, b, intensity
    assert(lua_gettop(L) == 7);
    float x = (float)lua_tonumber(L, 1);
    float y = (float)lua_tonumber(L, 2);
    float z = (float)lua_tonumber(L, 3);
    float r = (float)lua_tonumber(L, 4);
    float g = (float)lua_tonumber(L, 5);
    float b = (float)lua_tonumber(L, 6);
    float intensity = (float)lua_tonumber(L, 7);

    int lightId = interface->renderer_.addLight(x, y, z, r, g, b, intensity);
    lua_pushinteger(L, lightId);
    return 1;
}

int LuaInterface::updateLight(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: lightId, x, y, z, r, g, b, intensity
    assert(lua_gettop(L) == 8);
    int lightId = (int)lua_tointeger(L, 1);
    float x = (float)lua_tonumber(L, 2);
    float y = (float)lua_tonumber(L, 3);
    float z = (float)lua_tonumber(L, 4);
    float r = (float)lua_tonumber(L, 5);
    float g = (float)lua_tonumber(L, 6);
    float b = (float)lua_tonumber(L, 7);
    float intensity = (float)lua_tonumber(L, 8);

    interface->renderer_.updateLight(lightId, x, y, z, r, g, b, intensity);
    return 0;
}

int LuaInterface::removeLight(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: lightId
    assert(lua_gettop(L) == 1);
    int lightId = (int)lua_tointeger(L, 1);

    interface->renderer_.removeLight(lightId);
    return 0;
}

int LuaInterface::setAmbientLight(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: r, g, b
    assert(lua_gettop(L) == 3);
    float r = (float)lua_tonumber(L, 1);
    float g = (float)lua_tonumber(L, 2);
    float b = (float)lua_tonumber(L, 3);

    interface->renderer_.setAmbientLight(r, g, b);
    return 0;
}

// Particle system Lua bindings

int LuaInterface::loadParticleShaders(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: vertexShaderName (string), fragmentShaderName (string), blendMode (integer), useTexture (boolean, optional)
    int numArgs = lua_gettop(L);
    assert(numArgs >= 3 && numArgs <= 4);
    assert(lua_isstring(L, 1));
    assert(lua_isstring(L, 2));
    assert(lua_isnumber(L, 3));

    const char* vertShaderName = lua_tostring(L, 1);
    const char* fragShaderName = lua_tostring(L, 2);
    int blendMode = (int)lua_tointeger(L, 3);
    int zIndex = 0;  // Default zIndex for particles

    uint64_t vertId = hashCString(vertShaderName);
    uint64_t fragId = hashCString(fragShaderName);

    ResourceData vertShader = interface->pakResource_.getResource(vertId);
    ResourceData fragShader = interface->pakResource_.getResource(fragId);

    assert(vertShader.data != nullptr);
    assert(fragShader.data != nullptr);

    int pipelineId = interface->pipelineIndex_++;
    std::cout << "LuaInterface::loadParticleShaders: currentSceneId_=" << interface->currentSceneId_ << ", zIndex=" << zIndex << std::endl;
    Vector<std::pair<int, int>>** vecPtr = interface->scenePipelines_.find(interface->currentSceneId_);
    if (vecPtr == nullptr) {
        void* vectorMem = interface->stringAllocator_->allocate(sizeof(Vector<std::pair<int, int>>), "LuaInterface::loadParticleShaders::Vector");
        assert(vectorMem != nullptr);
        Vector<std::pair<int, int>>* newVec = new (vectorMem) Vector<std::pair<int, int>>(*interface->stringAllocator_, "LuaInterface::loadParticleShaders::data");
        interface->scenePipelines_.insertNew(interface->currentSceneId_, newVec);
        vecPtr = interface->scenePipelines_.find(interface->currentSceneId_);
        std::cout << "LuaInterface::loadParticleShaders: created new vector for sceneId " << interface->currentSceneId_ << std::endl;
    }
    assert(*vecPtr != nullptr);
    (*vecPtr)->push_back({pipelineId, zIndex});
    std::cout << "LuaInterface::loadParticleShaders: added pipeline " << pipelineId << " with zIndex " << zIndex << std::endl;

    // Create particle pipeline
    interface->renderer_.createParticlePipeline(pipelineId, vertShader, fragShader, blendMode);

    // Return the pipeline ID so it can be used in createParticleSystem
    lua_pushinteger(L, pipelineId);
    return 1;
}

int LuaInterface::createParticleSystem(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: config table, pipelineId
    assert(lua_gettop(L) == 2);
    assert(lua_istable(L, 1));
    assert(lua_isnumber(L, 2));

    int pipelineId = lua_tointeger(L, 2);

    ParticleEmitterConfig config;
    memset(&config, 0, sizeof(config));

    // Parse config table
    lua_getfield(L, 1, "maxParticles");
    config.maxParticles = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 100;
    lua_pop(L, 1);

    lua_getfield(L, 1, "emissionRate");
    config.emissionRate = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 10.0f;
    lua_pop(L, 1);

    lua_getfield(L, 1, "blendMode");
    config.blendMode = lua_isinteger(L, -1) ? (ParticleBlendMode)lua_tointeger(L, -1) : PARTICLE_BLEND_ADDITIVE;
    lua_pop(L, 1);

    // Parse emission area vertices (optional)
    lua_getfield(L, 1, "emissionVertices");
    if (lua_istable(L, -1)) {
        int len = (int)lua_rawlen(L, -1);
        config.emissionVertexCount = len / 2;
        if (config.emissionVertexCount > 8) config.emissionVertexCount = 8;
        for (int i = 0; i < config.emissionVertexCount * 2 && i < 16; ++i) {
            lua_rawgeti(L, -1, i + 1);
            config.emissionVertices[i] = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    // Parse texture names (optional)
    lua_getfield(L, 1, "textureNames");
    if (lua_istable(L, -1)) {
        int len = (int)lua_rawlen(L, -1);
        config.textureCount = len > 8 ? 8 : len;
        for (int i = 0; i < config.textureCount; ++i) {
            lua_rawgeti(L, -1, i + 1);
            if (lua_isstring(L, -1)) {
                const char* name = lua_tostring(L, -1);
                config.textureIds[i] = hashCString(name);
            } else {
                config.textureIds[i] = 0;
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    // Position variance
    lua_getfield(L, 1, "positionVariance");
    config.positionVariance = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);

    // Velocity
    lua_getfield(L, 1, "velocityMinX");
    config.velocityMinX = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "velocityMaxX");
    config.velocityMaxX = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "velocityMinY");
    config.velocityMinY = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "velocityMaxY");
    config.velocityMaxY = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);

    // Acceleration
    lua_getfield(L, 1, "accelerationMinX");
    config.accelerationMinX = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "accelerationMaxX");
    config.accelerationMaxX = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "accelerationMinY");
    config.accelerationMinY = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "accelerationMaxY");
    config.accelerationMaxY = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);

    // Radial acceleration
    lua_getfield(L, 1, "radialAccelerationMin");
    config.radialAccelerationMin = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "radialAccelerationMax");
    config.radialAccelerationMax = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);

    // Initial radial velocity
    lua_getfield(L, 1, "radialVelocityMin");
    config.radialVelocityMin = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "radialVelocityMax");
    config.radialVelocityMax = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);

    // Size
    lua_getfield(L, 1, "startSizeMin");
    config.startSizeMin = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.1f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "startSizeMax");
    config.startSizeMax = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.1f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "endSizeMin");
    config.endSizeMin = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.1f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "endSizeMax");
    config.endSizeMax = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.1f;
    lua_pop(L, 1);

    // Color (start)
    lua_getfield(L, 1, "colorMinR");
    config.colorMinR = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "colorMaxR");
    config.colorMaxR = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "colorMinG");
    config.colorMinG = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "colorMaxG");
    config.colorMaxG = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "colorMinB");
    config.colorMinB = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "colorMaxB");
    config.colorMaxB = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "colorMinA");
    config.colorMinA = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "colorMaxA");
    config.colorMaxA = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f;
    lua_pop(L, 1);

    // Color (end)
    lua_getfield(L, 1, "endColorMinR");
    config.endColorMinR = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : config.colorMinR;
    lua_pop(L, 1);
    lua_getfield(L, 1, "endColorMaxR");
    config.endColorMaxR = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : config.colorMaxR;
    lua_pop(L, 1);
    lua_getfield(L, 1, "endColorMinG");
    config.endColorMinG = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : config.colorMinG;
    lua_pop(L, 1);
    lua_getfield(L, 1, "endColorMaxG");
    config.endColorMaxG = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : config.colorMaxG;
    lua_pop(L, 1);
    lua_getfield(L, 1, "endColorMinB");
    config.endColorMinB = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : config.colorMinB;
    lua_pop(L, 1);
    lua_getfield(L, 1, "endColorMaxB");
    config.endColorMaxB = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : config.colorMaxB;
    lua_pop(L, 1);
    lua_getfield(L, 1, "endColorMinA");
    config.endColorMinA = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : config.colorMinA;
    lua_pop(L, 1);
    lua_getfield(L, 1, "endColorMaxA");
    config.endColorMaxA = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : config.colorMaxA;
    lua_pop(L, 1);

    // Lifetime
    lua_getfield(L, 1, "lifetimeMin");
    config.lifetimeMin = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "lifetimeMax");
    config.lifetimeMax = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f;
    lua_pop(L, 1);

    // System lifetime
    lua_getfield(L, 1, "systemLifetime");
    config.systemLifetime = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);

    // Rotation (start)
    lua_getfield(L, 1, "rotationMinX");
    config.rotationMinX = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotationMaxX");
    config.rotationMaxX = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotationMinY");
    config.rotationMinY = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotationMaxY");
    config.rotationMaxY = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotationMinZ");
    config.rotationMinZ = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotationMaxZ");
    config.rotationMaxZ = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);

    // Rotational velocity
    lua_getfield(L, 1, "rotVelocityMinX");
    config.rotVelocityMinX = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotVelocityMaxX");
    config.rotVelocityMaxX = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotVelocityMinY");
    config.rotVelocityMinY = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotVelocityMaxY");
    config.rotVelocityMaxY = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotVelocityMinZ");
    config.rotVelocityMinZ = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotVelocityMaxZ");
    config.rotVelocityMaxZ = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);

    // Rotational acceleration
    lua_getfield(L, 1, "rotAccelerationMinX");
    config.rotAccelerationMinX = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotAccelerationMaxX");
    config.rotAccelerationMaxX = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotAccelerationMinY");
    config.rotAccelerationMinY = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotAccelerationMaxY");
    config.rotAccelerationMaxY = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotAccelerationMinZ");
    config.rotAccelerationMinZ = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);
    lua_getfield(L, 1, "rotAccelerationMaxZ");
    config.rotAccelerationMaxZ = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 0.0f;
    lua_pop(L, 1);

    // Rotate with velocity
    lua_getfield(L, 1, "rotateWithVelocity");
    config.rotateWithVelocity = lua_isboolean(L, -1) ? lua_toboolean(L, -1) : false;
    lua_pop(L, 1);

    int systemId = interface->particleManager_->createSystem(config, pipelineId);
    lua_pushinteger(L, systemId);
    return 1;
}

int LuaInterface::destroyParticleSystem(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int systemId = lua_tointeger(L, 1);
    interface->particleManager_->destroySystem(systemId);
    return 0;
}

int LuaInterface::setParticleSystemPosition(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 3);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));

    int systemId = lua_tointeger(L, 1);
    float x = (float)lua_tonumber(L, 2);
    float y = (float)lua_tonumber(L, 3);

    interface->particleManager_->setSystemPosition(systemId, x, y);
    return 0;
}

int LuaInterface::openParticleEditor(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: pipelineIdAdditive, pipelineIdAlpha, pipelineIdSubtractive (integers)
    assert(lua_gettop(L) == 3);
    assert(lua_isnumber(L, 1));
    assert(lua_isnumber(L, 2));
    assert(lua_isnumber(L, 3));

    interface->particleEditorPipelineIds_[0] = lua_tointeger(L, 1);
    interface->particleEditorPipelineIds_[1] = lua_tointeger(L, 2);
    interface->particleEditorPipelineIds_[2] = lua_tointeger(L, 3);

#ifdef DEBUG
    // Get SceneManager to access ImGuiManager
    if (interface->sceneManager_) {
        // Pass the additive pipeline as the default
        interface->sceneManager_->setParticleEditorActive(true, interface->particleEditorPipelineIds_[0]);
    }
#endif

    return 0;
}

int LuaInterface::loadParticleConfig(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: filename (string) - e.g., "lantern_bugs.lua"
    assert(lua_gettop(L) == 1);
    assert(lua_isstring(L, 1));

    const char* filename = lua_tostring(L, 1);

    std::cout << "Loading particle config: " << filename << std::endl;

    // Hash the filename to get resource ID
    uint64_t resourceId = hashCString(filename);

    // Load the Lua file from the pak
    ResourceData scriptData = interface->pakResource_.getResource(resourceId);
    if (!scriptData.data || scriptData.size == 0) {
        std::cerr << "Failed to load particle config: " << filename << std::endl;
        lua_pushnil(L);
        assert(false);
        return 1;
    }

    // Load and execute the Lua script, which should return a table
    if (luaL_loadbuffer(L, (char*)scriptData.data, scriptData.size, filename) != LUA_OK) {
        const char* errorMsg = lua_tostring(L, -1);
        std::cerr << "Lua load error for " << filename << ": " << (errorMsg ? errorMsg : "unknown error") << std::endl;
        lua_pop(L, 1);
        lua_pushnil(L);
        assert(false);
        return 1;
    }

    // Execute the script - it should return a table
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        const char* errorMsg = lua_tostring(L, -1);
        std::cerr << "Lua exec error for " << filename << ": " << (errorMsg ? errorMsg : "unknown error") << std::endl;
        lua_pop(L, 1);
        lua_pushnil(L);
        assert(false);
        return 1;
    }

    // The result (table) is now on top of the stack
    return 1;
}

int LuaInterface::loadObject(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    // Arguments: filename (string), params table (optional)
    // The params table typically contains x, y position and any other configuration
    int numArgs = lua_gettop(L);
    assert(numArgs >= 1 && numArgs <= 2);
    assert(lua_isstring(L, 1));

    const char* filename = lua_tostring(L, 1);

    std::cout << "Loading object: " << filename << std::endl;

    // Hash the filename to get resource ID
    uint64_t resourceId = hashCString(filename);

    // Load the Lua file from the pak
    ResourceData scriptData = interface->pakResource_.getResource(resourceId);
    if (!scriptData.data || scriptData.size == 0) {
        std::cerr << "Failed to load object: " << filename << std::endl;
        lua_pushnil(L);
        assert(false);
        return 1;
    }

    // Load and execute the Lua script, which should return a table (the object module)
    if (luaL_loadbuffer(L, (char*)scriptData.data, scriptData.size, filename) != LUA_OK) {
        const char* errorMsg = lua_tostring(L, -1);
        std::cerr << "Lua load error for " << filename << ": " << (errorMsg ? errorMsg : "unknown error") << std::endl;
        lua_pop(L, 1);
        lua_pushnil(L);
        assert(false);
        return 1;
    }

    // Execute the script - it should return a table with create/update/cleanup functions
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        const char* errorMsg = lua_tostring(L, -1);
        std::cerr << "Lua exec error for " << filename << ": " << (errorMsg ? errorMsg : "unknown error") << std::endl;
        lua_pop(L, 1);
        lua_pushnil(L);
        assert(false);
        return 1;
    }

    // The result (module table) is now on top of the stack
    // Call the create function with params (or empty table if none provided)
    lua_getfield(L, -1, "create");
    if (lua_isfunction(L, -1)) {
        if (numArgs == 2 && lua_istable(L, 2)) {
            // Push the params table
            lua_pushvalue(L, 2);
        } else {
            // Push empty table if no params
            lua_newtable(L);
        }

        // Call create(params)
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            const char* errorMsg = lua_tostring(L, -1);
            std::cerr << "Lua object create error for " << filename << ": " << (errorMsg ? errorMsg : "unknown error") << std::endl;
            lua_pop(L, 1);
            assert(false);
        }
    } else {
        lua_pop(L, 1); // Pop non-function value
    }

    // Track the object - store a reference in the registry
    lua_pushvalue(L, -1); // Duplicate the module table
    int objRef = luaL_ref(L, LUA_REGISTRYINDEX);
    interface->sceneObjects_.push_back(objRef);

    // Return the module table (allows scene to access if needed, but update/cleanup are automatic)
    return 1;
}

int LuaInterface::createNode(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    int numArgs = lua_gettop(L);
    assert(numArgs >= 2);

    // Arguments: name (string), shape (table with either {vertices={...}} or {radius=number, x=number, y=number}), [script (table)]
    assert(lua_isstring(L, 1));
    assert(lua_istable(L, 2));

    const char* nodeNameCStr = lua_tostring(L, 1);
    String nodeName(nodeNameCStr, interface->stringAllocator_);

    // Check shape type
    lua_getfield(L, 2, "vertices");
    bool isPolygon = lua_istable(L, -1);
    lua_pop(L, 1);

    int bodyId = -1;
    float centerX = 0.0f;
    float centerY = 0.0f;

    if (isPolygon) {
        // Polygon node
        lua_getfield(L, 2, "vertices");
        assert(lua_istable(L, -1));

        float vertices[16];
        int tableLen = (int)lua_rawlen(L, -1);
        assert(tableLen >= 6 && tableLen <= 16 && tableLen % 2 == 0);

        for (int i = 0; i < tableLen; ++i) {
            lua_rawgeti(L, -1, i + 1);
            assert(lua_isnumber(L, -1));
            vertices[i] = lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);

        int vertexCount = tableLen / 2;
        assert(vertexCount >= 3 && vertexCount <= 8);

        // Calculate centroid
        for (int i = 0; i < vertexCount; ++i) {
            centerX += vertices[i * 2];
            centerY += vertices[i * 2 + 1];
        }
        centerX /= vertexCount;
        centerY /= vertexCount;

        // Create static sensor body
        bodyId = interface->physics_->createBody(0, centerX, centerY, 0.0f);

        // Convert vertices to local coordinates
        float localVertices[16];
        assert(vertexCount * 2 <= 16);
        for (int i = 0; i < vertexCount; ++i) {
            localVertices[i * 2] = vertices[i * 2] - centerX;
            localVertices[i * 2 + 1] = vertices[i * 2 + 1] - centerY;
        }

        // Add polygon sensor fixture
        interface->physics_->addPolygonSensor(bodyId, localVertices, vertexCount);
    } else {
        // Circle node
        lua_getfield(L, 2, "radius");
        assert(lua_isnumber(L, -1));
        float radius = lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 2, "x");
        assert(lua_isnumber(L, -1));
        centerX = lua_tonumber(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 2, "y");
        assert(lua_isnumber(L, -1));
        centerY = lua_tonumber(L, -1);
        lua_pop(L, 1);

        // Create static sensor body
        bodyId = interface->physics_->createBody(0, centerX, centerY, 0.0f);

        // Add circle sensor fixture
        interface->physics_->addCircleSensor(bodyId, radius);
    }

    // Create node entry - allocate through allocator using placement new
    int nodeId = interface->nextNodeId_++;
    std::cout << "LuaInterface::createNode: creating node " << nodeId << " with bodyId " << bodyId << std::endl;
    void* nodeMem = interface->stringAllocator_->allocate(sizeof(Node), "LuaInterface::createNode::Node");
    assert(nodeMem != nullptr);
    Node* node = new (nodeMem) Node(interface->stringAllocator_);
    node->bodyId = bodyId;
    node->name = nodeName;
    node->centerX = centerX;
    node->centerY = centerY;
    node->luaCallbackRef = LUA_NOREF;
    node->updateFuncRef = LUA_NOREF;
    node->onEnterFuncRef = LUA_NOREF;

    // Store optional script callbacks
    if (numArgs >= 3) {
        if (lua_isstring(L, 3)) {
            // Load script directly from pak
            const char* scriptNameCStr = lua_tostring(L, 3);
            String scriptName(scriptNameCStr, interface->stringAllocator_);
            String scriptPath("res/nodes/", interface->stringAllocator_);
            scriptPath += scriptName;
            scriptPath += ".lua";
            uint64_t scriptId = hashCString(scriptPath.c_str());
            ResourceData scriptData = interface->pakResource_.getResource(scriptId);
            if (scriptData.data && scriptData.size > 0) {
                if (luaL_loadbuffer(L, (char*)scriptData.data, scriptData.size, scriptPath.c_str()) == LUA_OK) {
                    if (lua_pcall(L, 0, 1, 0) == LUA_OK) {
                        // Script table is now on top
                    } else {
                        std::cerr << "Failed to execute node script: " << scriptPath.c_str() << std::endl;
                        lua_pop(L, 1); // Pop error message
                    }
                } else {
                    std::cerr << "Failed to load node script buffer: " << scriptPath.c_str() << std::endl;
                    lua_pop(L, 1); // Pop error message
                }
            } else {
                std::cerr << "Failed to load node script: " << scriptPath.c_str() << std::endl;
            }
        } else if (lua_istable(L, 3)) {
            lua_pushvalue(L, 3);
        }
        // If neither string nor table, skip
    }

    if (lua_istable(L, -1)) {
        // Store reference to the script table
        node->luaCallbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

        // Get and store references to update and onEnter functions if they exist
        lua_rawgeti(L, LUA_REGISTRYINDEX, node->luaCallbackRef);
        lua_getfield(L, -1, "update");
        if (lua_isfunction(L, -1)) {
            node->updateFuncRef = luaL_ref(L, LUA_REGISTRYINDEX);
        } else {
            lua_pop(L, 1);
        }

        lua_getfield(L, -1, "onEnter");
        if (lua_isfunction(L, -1)) {
            node->onEnterFuncRef = luaL_ref(L, LUA_REGISTRYINDEX);
        } else {
            lua_pop(L, 1);
        }
        lua_pop(L, 1); // Pop the table
    }

    interface->nodes_.insertNew(nodeId, node);
    interface->bodyToNodeMap_.insert(bodyId, nodeId);
    std::cout << "LuaInterface::createNode: inserted node " << nodeId << std::endl;

    lua_pushinteger(L, nodeId);
    return 1;
}

int LuaInterface::destroyNode(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int nodeId = lua_tointeger(L, 1);
    std::cout << "LuaInterface::destroyNode: nodeId=" << nodeId << std::endl;

    Node** nodePtr = interface->nodes_.find(nodeId);
    if (nodePtr != nullptr) {
        assert(*nodePtr != nullptr);
        Node* node = *nodePtr;

        // Unref Lua callbacks
        if (node->luaCallbackRef != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, node->luaCallbackRef);
        }
        if (node->updateFuncRef != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, node->updateFuncRef);
        }
        if (node->onEnterFuncRef != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, node->onEnterFuncRef);
        }

        // Remove from body map
        interface->bodyToNodeMap_.remove(node->bodyId);

        // Destroy physics body
        interface->physics_->destroyBody(node->bodyId);

        // Remove node - manually destruct and free through allocator
        node->~Node();  // Call destructor
        interface->stringAllocator_->free(node);  // Free through allocator
        interface->nodes_.remove(nodeId);
        std::cout << "LuaInterface::destroyNode: deleted node " << nodeId << std::endl;
    }

    return 0;
}

int LuaInterface::getNodePosition(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    assert(lua_gettop(L) == 1);
    assert(lua_isnumber(L, 1));

    int nodeId = lua_tointeger(L, 1);

    Node** nodePtr = interface->nodes_.find(nodeId);
    if (nodePtr != nullptr) {
        assert(*nodePtr != nullptr);
        Node* node = *nodePtr;
        lua_pushnumber(L, node->centerX);
        lua_pushnumber(L, node->centerY);
        return 2;
    }

    return 0;
}

void LuaInterface::updateNodes(float deltaTime) {
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
        Node* node = it.value();
        assert(node != nullptr);
        if (node->updateFuncRef != LUA_NOREF) {
            lua_rawgeti(luaState_, LUA_REGISTRYINDEX, node->updateFuncRef);
            lua_pushnumber(luaState_, deltaTime);
            if (lua_pcall(luaState_, 1, 0, 0) != LUA_OK) {
                const char* errorMsg = lua_tostring(luaState_, -1);
                std::cerr << "Node update error: " << (errorMsg ? errorMsg : "unknown") << std::endl;
                lua_pop(luaState_, 1);
                assert(false);
            }
        }
    }
}

void LuaInterface::handleNodeSensorEvent(const SensorEvent& event) {
    if (!event.isBegin) {
        return;
    }

    int* nodeIdPtr = bodyToNodeMap_.find(event.sensorBodyId);
    if (nodeIdPtr != nullptr) {
        int nodeId = *nodeIdPtr;
        Node** nodePtr = nodes_.find(nodeId);
        if (nodePtr != nullptr) {
            Node* node = *nodePtr;
            assert(node != nullptr);
            if (node->onEnterFuncRef != LUA_NOREF) {
                lua_rawgeti(luaState_, LUA_REGISTRYINDEX, node->onEnterFuncRef);
                lua_pushinteger(luaState_, event.visitorBodyId);
                lua_pushnumber(luaState_, event.visitorX);
                lua_pushnumber(luaState_, event.visitorY);
                if (lua_pcall(luaState_, 3, 0, 0) != LUA_OK) {
                    const char* errorMsg = lua_tostring(luaState_, -1);
                    std::cerr << "Node onEnter error: " << (errorMsg ? errorMsg : "unknown") << std::endl;
                    lua_pop(luaState_, 1);
                    assert(false);
                }
            }
        }
    }
}

void LuaInterface::setupWaterVisuals(int physicsForceFieldId, int waterFieldId,
                                      float minX, float minY, float maxX, float maxY,
                                      float alpha, float rippleAmplitude, float rippleSpeed) {
    // Water visual setup constants
    static const float WAVE_BUFFER = 0.1f;
    static const int WATER_SHADER_Z_INDEX = 2;

    float surfaceY = maxY;

    // 1. Enable reflection at the water surface
    renderer_.enableReflection(surfaceY);

    // 2. Load water shaders
    const char* waterVertShaderName = "res/shaders/water_vertex.spv";
    const char* waterFragShaderName = "res/shaders/water_fragment.spv";

    uint64_t vertId = hashCString(waterVertShaderName);
    uint64_t fragId = hashCString(waterFragShaderName);

    ResourceData vertShader = pakResource_.getResource(vertId);
    ResourceData fragShader = pakResource_.getResource(fragId);

    if (vertShader.data == nullptr || fragShader.data == nullptr) {
        std::cerr << "Failed to load water shaders" << std::endl;
        assert(false);
        return;
    }

    int waterShaderId = pipelineIndex_++;
    std::cout << "LuaInterface::setupWaterVisuals: currentSceneId_=" << currentSceneId_ << ", zIndex=" << WATER_SHADER_Z_INDEX << std::endl;
    Vector<std::pair<int, int>>** vecPtr = scenePipelines_.find(currentSceneId_);
    if (vecPtr == nullptr) {
        void* vectorMem = stringAllocator_->allocate(sizeof(Vector<std::pair<int, int>>), "LuaInterface::setupWaterVisuals::Vector");
        assert(vectorMem != nullptr);
        Vector<std::pair<int, int>>* newVec = new (vectorMem) Vector<std::pair<int, int>>(*stringAllocator_, "LuaInterface::setupWaterVisuals::data");
        scenePipelines_.insertNew(currentSceneId_, newVec);
        vecPtr = scenePipelines_.find(currentSceneId_);
        std::cout << "LuaInterface::setupWaterVisuals: created new vector for sceneId " << currentSceneId_ << std::endl;
    }
    assert(*vecPtr != nullptr);
    (*vecPtr)->push_back({waterShaderId, WATER_SHADER_Z_INDEX});
    std::cout << "LuaInterface::setupWaterVisuals: added pipeline " << waterShaderId << " with zIndex " << WATER_SHADER_Z_INDEX << std::endl;

    // Create animation textured pipeline for water (uses 33 float push constants)
    // Water needs 2 textures: primary texture and reflection render target
    renderer_.createAnimTexturedPipeline(waterShaderId, vertShader, fragShader, 2);

    // Mark pipeline as water pipeline
    renderer_.markPipelineAsWater(waterShaderId);

    // 3. Load a placeholder texture (required for layer creation)
    const char* placeholderTextureName = "res/textures/rock1.png";
    uint64_t placeholderTexId = hashCString(placeholderTextureName);

    ResourceData texData = pakResource_.getResource(placeholderTexId);
    if (texData.data != nullptr && texData.size > 0) {
        renderer_.loadTexture(placeholderTexId, texData);
    }

    // 4. Calculate layer dimensions
    float waterWidth = maxX - minX;
    float waterHeight = maxY - minY;
    float totalHeight = waterHeight + WAVE_BUFFER;
    float centerX = (minX + maxX) / 2.0f;
    float centerY = (minY + maxY + WAVE_BUFFER) / 2.0f;

    // 5. Get texture dimensions for aspect ratio calculation
    uint32_t texWidth = 1, texHeight = 1;
    renderer_.getTextureDimensions(placeholderTexId, &texWidth, &texHeight);
    float aspectRatio = (texHeight > 0) ? (float)texWidth / (float)texHeight : 1.0f;

    float layerSize = waterWidth;
    if (aspectRatio < 1.0f) {
        layerSize = waterWidth / aspectRatio;
    }

    // Calculate layer width and height based on aspect ratio
    float width, height;
    if (aspectRatio >= 1.0f) {
        width = layerSize;
        height = layerSize / aspectRatio;
    } else {
        width = layerSize * aspectRatio;
        height = layerSize;
    }

    // 6. Get reflection texture ID (passed as normalMapId to createLayer)
    uint64_t reflectionTexId = 0;
    if (renderer_.isReflectionEnabled()) {
        reflectionTexId = renderer_.getReflectionTextureId();
    }

    // 7. Create layer with primary texture and reflection texture as normalMap
    int waterLayerId = layerManager_->createLayer(placeholderTexId, width, height,
                                                   reflectionTexId, waterShaderId);

    if (waterLayerId < 0) {
        std::cerr << "Failed to create water layer" << std::endl;
        assert(false);
        return;
    }

    // Check if placeholder texture uses atlas
    AtlasUV atlasUV;
    bool usesAtlas = pakResource_.getAtlasUV(placeholderTexId, atlasUV);
    if (usesAtlas) {
        layerManager_->setLayerAtlasUV(waterLayerId, atlasUV.atlasId,
                                        atlasUV.u0, atlasUV.v0, atlasUV.u1, atlasUV.v1);
    }

    // 8. Set layer properties
    layerManager_->setLayerPosition(waterLayerId, centerX, centerY, 0.0f);

    // Scale height to cover the water area plus wave buffer
    float scaleY = (totalHeight * aspectRatio) / waterWidth;
    layerManager_->setLayerScale(waterLayerId, 1.0f, scaleY);

    // Use a tiny positive parallax depth so the layer isn't skipped
    // (layers with no physics body and zero parallax are skipped in SceneLayerManager)
    layerManager_->setLayerParallaxDepth(waterLayerId, -0.001f);

    // Enable local UV mode for shader coordinates
    layerManager_->setLayerUseLocalUV(waterLayerId, true);

    // 9. Set water shader parameters: alpha, rippleAmplitude, rippleSpeed, maxY(surface), minX, minY, maxX
    float shaderParams[7] = {alpha, rippleAmplitude, rippleSpeed, surfaceY, minX, minY, maxX};
    renderer_.setShaderParameters(waterShaderId, 7, shaderParams);

    // 10. Associate the water shader with the water force field for splash ripples
    waterFieldShaderMap_.insert(waterFieldId, waterShaderId);

    std::cout << "Water visual setup complete: layer=" << waterLayerId
              << " shader=" << waterShaderId << " field=" << waterFieldId << std::endl;
}

int LuaInterface::b2AddBodyType(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    int bodyId = luaL_checkinteger(L, 1);
    const char* typeStr = luaL_checkstring(L, 2);
    interface->physics_->addBodyType(bodyId, typeStr);
    return 0;
}

int LuaInterface::b2RemoveBodyType(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    int bodyId = luaL_checkinteger(L, 1);
    const char* typeStr = luaL_checkstring(L, 2);
    interface->physics_->removeBodyType(bodyId, typeStr);
    return 0;
}

int LuaInterface::b2ClearBodyTypes(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    int bodyId = luaL_checkinteger(L, 1);
    interface->physics_->clearBodyTypes(bodyId);
    return 0;
}

int LuaInterface::b2BodyHasType(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    int bodyId = luaL_checkinteger(L, 1);
    const char* typeStr = luaL_checkstring(L, 2);
    bool hasType = interface->physics_->bodyHasType(bodyId, typeStr);
    lua_pushboolean(L, hasType);
    return 1;
}

int LuaInterface::b2GetBodyTypes(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    int bodyId = luaL_checkinteger(L, 1);
    Vector<String> types = interface->physics_->getBodyTypes(bodyId);

    lua_newtable(L);
    for (size_t i = 0; i < types.size(); ++i) {
        lua_pushstring(L, types[i].c_str());
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

int LuaInterface::b2SetCollisionCallback(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "LuaInterface");
    LuaInterface* interface = (LuaInterface*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    if (!lua_isfunction(L, 1)) {
        assert(false);
        return luaL_error(L, "Expected function as first argument");
    }

    int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

    interface->physics_->setCollisionCallback([interface, callbackRef](int bodyIdA, int bodyIdB, float pointX, float pointY, float normalX, float normalY, float approachSpeed) {
        lua_rawgeti(interface->luaState_, LUA_REGISTRYINDEX, callbackRef);
        lua_pushinteger(interface->luaState_, bodyIdA);
        lua_pushinteger(interface->luaState_, bodyIdB);
        lua_pushnumber(interface->luaState_, pointX);
        lua_pushnumber(interface->luaState_, pointY);
        lua_pushnumber(interface->luaState_, normalX);
        lua_pushnumber(interface->luaState_, normalY);
        lua_pushnumber(interface->luaState_, approachSpeed);
        if (lua_pcall(interface->luaState_, 7, 0, 0) != LUA_OK) {
            const char* errorMsg = lua_tostring(interface->luaState_, -1);
            std::cerr << "Collision callback error: " << (errorMsg ? errorMsg : "unknown") << std::endl;
            assert(false);
            lua_pop(interface->luaState_, 1);
        }
    });

    return 0;
}
