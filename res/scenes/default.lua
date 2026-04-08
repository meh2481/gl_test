-- Scene initialization function

-- Button helpers (managed inline – avoids loadObject lifecycle issues for the base scene)
local SCALE_NORMAL = 1.0
local SCALE_HOVER  = 1.12
local SCALE_PRESS  = 0.88

local function btnInBounds(btn, cx, cy)
    return math.abs(cx - btn.x) <= btn.half and math.abs(cy - btn.y) <= btn.half
end

local function btnAnimScale(btn, target, dur)
    animateLayerScale(btn.layer, btn.scale, btn.scale, target, target, dur, INTERPOLATION_EASE_OUT)
    btn.scale = target
end

local function btnUpdate(btn)
    if btn == nil or btn.layer == nil then return end
    local cx, cy = getCursorPosition()
    local hov = btnInBounds(btn, cx, cy)
    if hov ~= btn.hovered then
        btn.hovered = hov
        if not btn.pressed then
            if hov then
                btnAnimScale(btn, SCALE_HOVER, 0.1)
            else
                btnAnimScale(btn, SCALE_NORMAL, 0.1)
            end
        end
    end
end

local function btnOnDragStart(btn)
    if btn == nil or btn.layer == nil then return end
    local cx, cy = getCursorPosition()
    if btnInBounds(btn, cx, cy) then
        btn.pressed = true
        stopLayerAnimations(btn.layer, PROPERTY_LAYER_SCALE)
        btnAnimScale(btn, SCALE_PRESS, 0.06)
    end
end

local function btnOnDragEnd(btn)
    if btn == nil or btn.layer == nil then return end
    if btn.pressed then
        btn.pressed = false
        local cx, cy = getCursorPosition()
        local inB = btnInBounds(btn, cx, cy)
        btnAnimScale(btn, inB and SCALE_HOVER or SCALE_NORMAL, 0.1)
        if inB then
            fireAction(btn.action)
        end
    end
end

-- Shared button resources (loaded once, persist across sub-scene pushes/pops)
local btnTexId    = nil
local btnShaderId = nil

-- Button instances
local physicsBtn = nil
local audioBtn   = nil

local function makeButton(x, y, size, action, r, g, b)
    local lay = createLayer(btnTexId, size, btnShaderId)
    setLayerPosition(lay, x, y)
    setLayerScale(lay, SCALE_NORMAL, SCALE_NORMAL)
    setLayerColor(lay, r or 1.0, g or 1.0, b or 1.0, 1.0)
    return {
        layer   = lay,
        x       = x, y = y,
        half    = size / 2.0,
        action  = action,
        scale   = SCALE_NORMAL,
        hovered = false,
        pressed = false,
    }
end

-- (Re)create button layers — called from init() and onResume()
local function createButtons()
    physicsBtn = makeButton(-0.25, -0.35, 0.22, ACTION_PHYSICS_DEMO, 1.0, 0.55, 0.1)
    audioBtn   = makeButton( 0.25, -0.35, 0.22, ACTION_AUDIO_TEST,   0.3, 0.6,  1.0)
end

function init()
    -- Load the nebula shaders (z-index 0)
    loadShaders("res/shaders/vertex.spv", "res/shaders/nebula_fragment.spv", 0)
    -- Significantly decrease transition fade
    setTransitionFadeTime(0.0625, 0.0625)

    -- Load shared button resources (texture and shader persist even when sub-scenes are cleaned up)
    btnTexId    = loadTexture("res/objects/rock/rock.png")
    btnShaderId = loadTexturedShaders("res/shaders/sprite_vertex.spv", "res/shaders/sprite_fragment.spv", 1)

    createButtons()
end

-- Called when this scene becomes the active (top) scene again after a sub-scene is popped.
-- Sub-scene cleanup wipes all layers, so we recreate button layers here.
function onResume()
    createButtons()
end

-- Scene update function called every frame
function update(deltaTime)
    btnUpdate(physicsBtn)
    btnUpdate(audioBtn)
end

-- Handle actions
function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    end
    if action == ACTION_MENU then
        pushScene("res/scenes/menu.lua")
    end
    if action == ACTION_PHYSICS_DEMO then
        pushScene("res/scenes/physics.lua")
    end
    if action == ACTION_AUDIO_TEST then
        pushScene("res/scenes/audio_test.lua")
    end
    if action == ACTION_PARTICLE_EDITOR then
        pushScene("res/scenes/particle_editor.lua")
    end
    if action == ACTION_DRAG_START then
        btnOnDragStart(physicsBtn)
        btnOnDragStart(audioBtn)
    end
    if action == ACTION_DRAG_END then
        btnOnDragEnd(physicsBtn)
        btnOnDragEnd(audioBtn)
    end
end