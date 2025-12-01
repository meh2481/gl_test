-- Menu scene initialization
function init()
    -- Load menu plasma background shader (z-index 0 - drawn first)
    loadShaders("res/shaders/vertex.spv", "res/shaders/menu_fragment.spv", 0)
    -- Load cloud layer shader (z-index 1 - drawn on top)
    loadShaders("res/shaders/vertex.spv", "res/shaders/cloud_fragment.spv", 1)
    print("Menu scene initialized")
    time = 0
end

-- Menu scene update
function update(deltaTime)
    -- No timer logic needed
end

-- Handle actions
function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    end
end