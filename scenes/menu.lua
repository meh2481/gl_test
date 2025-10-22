-- Menu scene initialization
function init()
    -- Load menu-specific shaders
    loadShaders("menu_vertex.spv", "menu_fragment.spv")
    time = 0
    print("Menu scene initialized")
end

-- Menu scene update
function update(deltaTime)
    time = time + deltaTime

    -- Return to main scene after 3 seconds
    if time > 3 then
        print("Calling popScene()")
        popScene()
    end
end