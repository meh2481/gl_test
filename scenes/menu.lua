-- Menu scene initialization
function init()
    -- Load menu plasma background shader (z-index 0 - drawn first)
    loadShaders("vertex.spv", "menu_fragment.spv", 0)
    -- Load cloud layer shader (z-index 1 - drawn on top)
    loadShaders("vertex.spv", "cloud_fragment.spv", 1)
    print("Menu scene initialized")
    time = 0
end

-- Menu scene update
function update(deltaTime)
    -- No timer logic needed
end

-- Handle key down events (backwards compatibility)
function onKeyDown(keyCode)
    if keyCode == SDLK_ESCAPE then
        popScene()
    end
end

-- Handle actions (new action-based system)
function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    end
end