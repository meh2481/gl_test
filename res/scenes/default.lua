-- Scene initialization function

-- Shared button resources (loaded once, persist across sub-scene pushes/pops)
local btnTexId    = nil
local btnShaderId = nil

-- Button node instances
local physicsBtn = nil
local audioBtn   = nil

-- Vector shape handle and its persistent layer id for the "OhHai" glyph demo
local vectorShapeHandle = nil
local vectorLayerId     = nil

-- (Re)create button nodes — called from init() and onResume()
local function createButtons()
    physicsBtn = loadObject("res/nodes/button.lua", {
        texId    = btnTexId,
        shaderId = btnShaderId,
        x = -0.25, y = -0.35, size = 0.22,
        action   = ACTION_PHYSICS_DEMO,
        r = 1.0, g = 0.55, b = 0.1,
    })
    audioBtn = loadObject("res/nodes/button.lua", {
        texId    = btnTexId,
        shaderId = btnShaderId,
        x = 0.25, y = -0.35, size = 0.22,
        action   = ACTION_AUDIO_TEST,
        r = 0.3, g = 0.6, b = 1.0,
    })
end

local function destroyButtons()
    if physicsBtn then physicsBtn.cleanup() end
    if audioBtn   then audioBtn.cleanup()   end
    physicsBtn = nil
    audioBtn   = nil
end

function init()
    -- Load the nebula shaders (z-index 0)
    loadShaders("res/shaders/vertex.spv", "res/shaders/nebula_fragment.spv", 0)
    -- Significantly decrease transition fade
    setTransitionFadeTime(0.0625, 0.0625)

    -- Load shared button resources (texture and shader persist even when sub-scenes are cleaned up)
    btnTexId    = loadTexture("res/objects/rock/rock.png")
    btnShaderId = loadAnimTexturedShaders("res/shaders/anim_sprite_vertex.spv", "res/shaders/anim_sprite_fragment.spv", 1, 1)

    -- Load the vector shape for the "OhHai" glyph demo
    vectorShapeHandle = loadVectorShape("res/test.svg")

    -- Create a persistent vector layer (rendered automatically every frame, no per-frame call needed).
    -- createVectorLayer(handle, x, y, scale, r, g, b, a)
    if vectorShapeHandle then
        vectorLayerId = createVectorLayer(vectorShapeHandle, 0.0, 0.65, 0.35, 0.1, 0.8, 1.0, 1.0)
    end

    createButtons()
end

-- Called when this scene becomes the active (top) scene again after a sub-scene is popped.
-- Sub-scene cleanup wipes all layers, so we recreate button nodes here.
function onResume()
    createButtons()
end

-- Scene update function called every frame
function update(deltaTime)
    if physicsBtn then physicsBtn.update(deltaTime) end
    if audioBtn   then audioBtn.update(deltaTime)   end
    -- The vector shape is rendered via a persistent layer set up in init();
    -- no per-frame draw call is needed here.
end

-- Handle actions
function onAction(action)
    -- Forward input actions to button nodes first (so they can fire their own actions)
    if physicsBtn then physicsBtn.onAction(action) end
    if audioBtn   then audioBtn.onAction(action)   end

    if action == ACTION_EXIT then
        if vectorLayerId then destroyVectorLayer(vectorLayerId) end
        popScene()
    elseif action == ACTION_MENU then
        destroyButtons()
        pushScene("res/scenes/menu.lua")
    elseif action == ACTION_PHYSICS_DEMO then
        destroyButtons()
        pushScene("res/scenes/physics.lua")
    elseif action == ACTION_AUDIO_TEST then
        destroyButtons()
        pushScene("res/scenes/audio_test.lua")
    elseif action == ACTION_PARTICLE_EDITOR then
        destroyButtons()
        pushScene("res/scenes/particle_editor.lua")
    end
end