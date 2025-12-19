-- Scene to test transition configuration
function init()
    -- Configure transition times: 0.5s fade-out, 0.5s fade-in
    setTransitionFadeTime(0.5, 0.5)
    -- Configure transition color: white instead of black
    setTransitionColor(1.0, 1.0, 1.0)

    -- Load a simple shader for visualization
    loadShaders("res/shaders/vertex.spv", "res/shaders/nebula_fragment.spv", 0)
    time = 0
end

function update(deltaTime)
    -- No update logic needed for this test
end

function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    end
    if action == ACTION_MENU then
        -- Test transition by pushing menu scene
        -- Note: This depends on res/scenes/menu.lua existing in the project
        pushScene("res/scenes/menu.lua")
    end
end
