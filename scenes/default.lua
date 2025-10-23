-- Scene initialization function
function init()
    -- Load the nebula shaders
    loadShaders("nebula_vertex.spv", "nebula_fragment.spv")
end

-- Scene update function called every frame
function update(deltaTime)
    -- No timer logic needed
end

-- Handle key down events
function onKeyDown(keyCode)
    if keyCode == 27 then  -- SDLK_ESCAPE
        popScene()
    end
    if keyCode == 13 then  -- SDLK_RETURN
        pushScene("menu.lua")
    end
end