#include "SceneManager.h"
#include "LuaInterface.h"
#include <cassert>

SceneManager::SceneManager(PakResource& pakResource, VulkanRenderer& renderer)
    : pakResource_(pakResource), renderer_(renderer), luaInterface_(std::make_unique<LuaInterface>(pakResource, renderer, this)), pendingPop_(false) {
}

SceneManager::~SceneManager() {
    // LuaInterface will be automatically cleaned up
}

void SceneManager::pushScene(uint64_t sceneId) {
    // Load the scene if not already loaded
    if (loadedScenes_.find(sceneId) == loadedScenes_.end()) {
        ResourceData sceneScript = pakResource_.getResource(sceneId);
        luaInterface_->loadScene(sceneId, sceneScript);
        loadedScenes_.insert(sceneId);
    }

    // Push scene onto stack
    sceneStack_.push(sceneId);

    // Initialize the scene
    luaInterface_->initScene(sceneId);

    // Set the pipelines for this scene
    luaInterface_->switchToScenePipeline(sceneId);
}

void SceneManager::popScene() {
    if (!sceneStack_.empty()) {
        pendingPop_ = true;
    }
}

bool SceneManager::isEmpty() const {
    return sceneStack_.empty();
}

uint64_t SceneManager::getActiveSceneId() const {
    if (!sceneStack_.empty()) {
        return sceneStack_.top();
    }
    return 0; // Or some invalid ID
}

void SceneManager::reloadCurrentScene() {
    if (!sceneStack_.empty()) {
        uint64_t currentSceneId = sceneStack_.top();
        // Remove from loaded scenes so it will reload
        loadedScenes_.erase(currentSceneId);
        // Reinitialize the scene
        luaInterface_->loadScene(currentSceneId, pakResource_.getResource(currentSceneId));
        luaInterface_->initScene(currentSceneId);
        luaInterface_->switchToScenePipeline(currentSceneId);
    }
}

void SceneManager::initActiveScene() {
    // initActiveScene is called after pushing, so this might not be needed
    // But keeping for consistency
    if (!sceneStack_.empty()) {
        // Scenes are initialized when pushed
    }
}

bool SceneManager::updateActiveScene(float deltaTime) {
    if (!sceneStack_.empty()) {
        uint64_t activeSceneId = sceneStack_.top();
        luaInterface_->updateScene(activeSceneId, deltaTime);

        // Pop the scene after Lua execution is complete
        if (pendingPop_) {
            sceneStack_.pop();
            pendingPop_ = false;

            // Switch to the new active scene's pipeline
            if (!sceneStack_.empty()) {
                uint64_t newActiveSceneId = sceneStack_.top();
                luaInterface_->switchToScenePipeline(newActiveSceneId);
            }
        }
    }

    return !sceneStack_.empty();
}

void SceneManager::handleKeyDown(int keyCode) {
    if (!sceneStack_.empty()) {
        uint64_t activeSceneId = sceneStack_.top();
        luaInterface_->handleKeyDown(activeSceneId, keyCode);
    }
}

void SceneManager::handleKeyUp(int keyCode) {
    if (!sceneStack_.empty()) {
        uint64_t activeSceneId = sceneStack_.top();
        luaInterface_->handleKeyUp(activeSceneId, keyCode);
    }
}