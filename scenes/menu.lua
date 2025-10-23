-- Menu scene initialization
function init()
    -- Load menu plasma background shader (z-index 0 - drawn first)
    loadShaders("menu_vertex.spv", "menu_fragment.spv", 0)
    -- Load cloud layer shader (z-index 1 - drawn on top)
    loadShaders("cloud_vertex.spv", "cloud_fragment.spv", 1)
    print("Menu scene initialized")
    time = 0
end

-- Menu scene update
function update(deltaTime)
    -- No timer logic needed
end

-- Handle key down events
function onKeyDown(keyCode)
    if keyCode == 27 then  -- SDLK_ESCAPE
        popScene()
    end
end