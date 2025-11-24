-- Scene initialization function
function init()
    -- Load the nebula shaders (z-index 0)
    loadShaders("vertex.spv", "nebula_fragment.spv", 0)
    print("Default scene initialized")
    time = 0
    menuPushed = false
end

-- Scene update function called every frame
function update(deltaTime)
    -- No timer logic needed
end

-- Handle actions
function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    end
    if action == ACTION_MENU then
        pushScene("menu.lua")
    end
    if action == ACTION_PHYSICS_DEMO then
        pushScene("physics.lua")
    end
    if action == ACTION_AUDIO_TEST then
        pushScene("audio_test.lua")
    end
    if action == ACTION_ASPECT_TEST then
        pushScene("aspect_test.lua")
    end
end