-- Scene initialization function
function init()
    -- Load the nebula shaders (z-index 0)
    loadShaders("res/shaders/vertex.spv", "res/shaders/nebula_fragment.spv", 0)
    -- Significantly decrease transition fade
    setTransitionFadeTime(0.0625, 0.0625)
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
        pushScene("res/scenes/menu.lua")
    end
    if action == ACTION_PHYSICS_DEMO then
        pushScene("res/scenes/physics.lua")
    end
    if action == ACTION_AUDIO_TEST then
        pushScene("res/scenes/audio_test.lua")
    end
    if action == ACTION_PARTICLE_EDITOR then
        pushScene("res/scenes/particle_editor.lua")
    end
end