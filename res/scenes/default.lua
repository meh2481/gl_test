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

-- Text demo (M1–M8 showcase) -- all textLayer IDs
local textLayers = {}

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

-- ---------------------------------------------------------------------------
-- Text demo — showcases M1–M8
-- ---------------------------------------------------------------------------
local function createTextDemo()
    -- M1/M2: Load fonts from the binary .font resources produced by font_extractor.
    local fontReg    = loadFont("res/fonts/aileron/Aileron-Regular.font")
    local fontBold   = loadFont("res/fonts/aileron/Aileron-Bold.font")
    local fontItalic = loadFont("res/fonts/aileron/Aileron-Italic.font")

    if not fontReg then return end   -- font not yet packed; skip demo gracefully

    -- Row 1 — M2/M3: Basic text with typewriter reveal
    local tl1 = createTextLayer(fontReg)
    textLayerSetPosition(tl1, -0.85, 0.85)
    textLayerSetSize(tl1, 36)
    textLayerSetColor(tl1, 1.0, 1.0, 1.0, 1.0)
    textLayerSetRevealSpeed(tl1, 18)   -- 18 chars/sec typewriter
    textLayerSetString(tl1, "M1-M2: Aileron Regular  |  typewriter reveal")
    textLayers[#textLayers+1] = tl1

    -- Row 2 — M4: word-wrap + alignment
    local tl2 = createTextLayer(fontReg)
    textLayerSetPosition(tl2, -0.85, 0.68)
    textLayerSetSize(tl2, 28)
    textLayerSetColor(tl2, 0.85, 0.85, 1.0, 1.0)
    textLayerSetWrapWidth(tl2, 1.7)
    textLayerSetAlignment(tl2, TEXT_ALIGN_LEFT)
    textLayerSetString(tl2, "M4: wrap + left-align")
    textLayers[#textLayers+1] = tl2

    -- Row 3 — M5: colour markup
    local tl3 = createTextLayer(fontReg)
    textLayerSetPosition(tl3, -0.85, 0.52)
    textLayerSetSize(tl3, 32)
    textLayerSetString(tl3, "M5: [color=FF4444FF]red[/color] [color=44FF88FF]green[/color] [color=4488FFFF]blue[/color]")
    textLayers[#textLayers+1] = tl3

    -- Row 4 — M5: wave + shake + rainbow effects
    local tl4 = createTextLayer(fontReg)
    textLayerSetPosition(tl4, -0.85, 0.35)
    textLayerSetSize(tl4, 32)
    textLayerSetString(tl4, "[wave amp=0.012 freq=3]wave[/wave]  [shake mag=0.008]shake[/shake]  [rainbow speed=0.4]rainbow[/rainbow]")
    textLayers[#textLayers+1] = tl4

    -- Row 5 — M6: multi-font runs
    if fontBold and fontItalic then
        local tl5 = createTextLayer(fontReg)
        textLayerSetPosition(tl5, -0.85, 0.19)
        textLayerSetSize(tl5, 32)
        textLayerSetFontFamily(tl5, fontBold, fontItalic, -1)
        textLayerSetString(tl5, "M6: [font=bold]Bold[/font]  [font=italic]Italic[/font]  Regular")
        textLayers[#textLayers+1] = tl5
    end

    -- Row 6 — M8: drop shadow
    local tl6 = createTextLayer(fontBold or fontReg)
    textLayerSetPosition(tl6, -0.85, 0.02)
    textLayerSetSize(tl6, 38)
    textLayerSetColor(tl6, 1.0, 0.92, 0.3, 1.0)
    textLayerSetShadow(tl6, 0.012, -0.012, 0.0, 0.0, 0.0, 0.75)
    textLayerSetString(tl6, "M8: Drop Shadow")
    textLayers[#textLayers+1] = tl6

    -- Row 7 — M6+M8: bold + rainbow + shadow combo
    local tl7 = createTextLayer(fontBold or fontReg)
    textLayerSetPosition(tl7, -0.85, -0.17)
    textLayerSetSize(tl7, 30)
    textLayerSetShadow(tl7, 0.008, -0.008, 0.05, 0.0, 0.1, 0.7)
    textLayerSetString(tl7, "[rainbow speed=0.5]M6+M8: rainbow bold with shadow[/rainbow]")
    textLayers[#textLayers+1] = tl7
end

local function destroyTextDemo()
    for _, id in ipairs(textLayers) do
        destroyTextLayer(id)
    end
    textLayers = {}
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

    -- M1–M8 text demo
    createTextDemo()

    createButtons()
end

-- Called when this scene becomes the active (top) scene again after a sub-scene is popped.
-- Sub-scene cleanup wipes all layers, so we recreate button nodes here.
function onResume()
    createTextDemo()
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
        destroyTextDemo()
        popScene()
    elseif action == ACTION_MENU then
        destroyButtons()
        destroyTextDemo()
        pushScene("res/scenes/menu.lua")
    elseif action == ACTION_PHYSICS_DEMO then
        destroyButtons()
        destroyTextDemo()
        pushScene("res/scenes/physics.lua")
    elseif action == ACTION_AUDIO_TEST then
        destroyButtons()
        destroyTextDemo()
        pushScene("res/scenes/audio_test.lua")
    elseif action == ACTION_PARTICLE_EDITOR then
        destroyButtons()
        destroyTextDemo()
        pushScene("res/scenes/particle_editor.lua")
    end
end
