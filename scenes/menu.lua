-- Menu scene initialization
function init()
    -- Load menu plasma background shader
    loadShaders("menu_vertex.spv", "menu_fragment.spv")
    -- Load cloud layer shader
    loadShaders("cloud_vertex.spv", "cloud_fragment.spv")
    print("Menu scene initialized")
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