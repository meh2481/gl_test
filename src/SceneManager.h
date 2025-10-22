#pragma once

#include <stack>
#include <unordered_set>
#include <memory>
#include "LuaInterface.h"
#include "resource.h"

class SceneManager {
public:
    SceneManager(PakResource& pakResource, VulkanRenderer& renderer);
    ~SceneManager();

    // Scene management
    void pushScene(uint64_t sceneId);
    void popScene();
    bool isEmpty() const;

    // Active scene operations
    void initActiveScene();
    bool updateActiveScene(float deltaTime);

private:
    PakResource& pakResource_;
    VulkanRenderer& renderer_;
    std::unique_ptr<LuaInterface> luaInterface_;
    std::stack<uint64_t> sceneStack_;
    std::unordered_set<uint64_t> loadedScenes_;
    bool pendingPop_;
};