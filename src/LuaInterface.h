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
#include "AudioManager.h"
#include "VibrationManager.h"

class SceneManager;

class LuaInterface {
public:
    LuaInterface(PakResource& pakResource, VulkanRenderer& renderer, SceneManager* sceneManager = nullptr, VibrationManager* vibrationManager = nullptr);
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

    // Audio access
    AudioManager& getAudioManager() { return *audioManager_; }

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
    static int isKeyPressed(lua_State* L);
    static int luaPrint(lua_State* L);  // Custom print for console capture

    // Box2D Lua bindings
    static int b2SetGravity(lua_State* L);
    static int b2SetFixedTimestep(lua_State* L);
    static int b2Step(lua_State* L);
    static int b2CreateBody(lua_State* L);
    static int b2DestroyBody(lua_State* L);
    static int b2AddBoxFixture(lua_State* L);
    static int b2AddCircleFixture(lua_State* L);
    static int b2AddPolygonFixture(lua_State* L);
    static int b2AddSegmentFixture(lua_State* L);
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
    static int b2CreateRevoluteJoint(lua_State* L);
    static int b2DestroyJoint(lua_State* L);
    static int b2QueryBodyAtPoint(lua_State* L);
    static int b2CreateMouseJoint(lua_State* L);
    static int b2UpdateMouseJointTarget(lua_State* L);
    static int b2DestroyMouseJoint(lua_State* L);
    static int b2GetCollisionHitEvents(lua_State* L);
    static int b2SetBodyDestructible(lua_State* L);
    static int b2SetBodyDestructibleLayer(lua_State* L);
    static int b2ClearBodyDestructible(lua_State* L);
    static int b2CleanupAllFragments(lua_State* L);

    // Scene layer Lua bindings
    static int createLayer(lua_State* L);
    static int destroyLayer(lua_State* L);
    static int attachLayerToBody(lua_State* L);
    static int detachLayer(lua_State* L);
    static int setLayerEnabled(lua_State* L);
    static int setLayerOffset(lua_State* L);
    static int setLayerUseLocalUV(lua_State* L);
    static int setLayerPolygon(lua_State* L);
    static int setLayerScale(lua_State* L);
    static int setLayerAlpha(lua_State* L);
    static int setLayerTransform(lua_State* L);

    // Texture loading
    static int loadTexture(lua_State* L);
    static int getTextureDimensions(lua_State* L);
    static int loadTexturedShaders(lua_State* L);
    static int loadTexturedShadersEx(lua_State* L);
    static int loadTexturedShadersAdditive(lua_State* L);
    static int setShaderUniform3f(lua_State* L);
    static int setShaderParameters(lua_State* L);

    // Audio Lua bindings
    static int audioLoadBuffer(lua_State* L);
    static int audioLoadOpus(lua_State* L);  // Load OPUS audio from resource
    static int audioCreateSource(lua_State* L);
    static int audioPlaySource(lua_State* L);
    static int audioStopSource(lua_State* L);
    static int audioPauseSource(lua_State* L);
    static int audioSetSourcePosition(lua_State* L);
    static int audioSetSourceVelocity(lua_State* L);
    static int audioSetSourceVolume(lua_State* L);
    static int audioSetSourcePitch(lua_State* L);
    static int audioSetSourceLooping(lua_State* L);
    static int audioReleaseSource(lua_State* L);
    static int audioIsSourcePlaying(lua_State* L);
    static int audioSetListenerPosition(lua_State* L);
    static int audioSetListenerVelocity(lua_State* L);
    static int audioSetListenerOrientation(lua_State* L);
    static int audioSetGlobalVolume(lua_State* L);
    static int audioSetGlobalEffect(lua_State* L);

    // Vibration Lua bindings
    static int vibrate(lua_State* L);
    static int vibrateTriggers(lua_State* L);
    static int stopVibration(lua_State* L);

    // Cursor position Lua bindings
    static int getCursorPosition(lua_State* L);

    // Camera Lua bindings
    static int getCameraOffset(lua_State* L);
    static int setCameraOffset(lua_State* L);
    static int getCameraZoom(lua_State* L);
    static int setCameraZoom(lua_State* L);

    // Light management Lua bindings
    static int addLight(lua_State* L);
    static int updateLight(lua_State* L);
    static int removeLight(lua_State* L);
    static int clearLights(lua_State* L);
    static int setAmbientLight(lua_State* L);

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
    std::unique_ptr<AudioManager> audioManager_;
    VibrationManager* vibrationManager_;
    float cursorX_;
    float cursorY_;
    float cameraOffsetX_;
    float cameraOffsetY_;
    float cameraZoom_;
};