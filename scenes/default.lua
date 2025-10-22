-- Scene initialization function
function init()
    -- Load the nebula shaders
    loadShaders("nebula_vertex.spv", "nebula_fragment.spv")
    time = 0
    menuPushed = false
end

-- Scene update function called every frame
function update(deltaTime)
    time = time + deltaTime

    -- After 3 seconds, push the menu scene (only once)
    if time > 3 and not menuPushed then
        pushScene("menu.lua")
        menuPushed = true
    end
end

-- Handle key down events
function onKeyDown(keyCode)
    if keyCode == 27 then  -- SDLK_ESCAPE
        popScene()
    end
end