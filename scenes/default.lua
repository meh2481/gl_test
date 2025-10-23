-- Scene initialization function
function init()
    -- Load the nebula shaders (z-index 0)
    loadShaders("nebula_vertex.spv", "nebula_fragment.spv", 0)
    print("Default scene initialized")
    time = 0
    menuPushed = false
end

-- Scene update function called every frame
function update(deltaTime)
    -- No timer logic needed
end

-- Handle key down events
function onKeyDown(keyCode)
    if keyCode == SDLK_ESCAPE then
        popScene()
    end
    if keyCode == SDLK_RETURN then
        pushScene("menu.lua")
    end
end