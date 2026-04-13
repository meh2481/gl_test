-- Dialogue test scene
-- Showcases the full dialogue system: multi-character, portraits, all markup features.
-- Press SPACE / click to advance. The dialogue loops back to the beginning on completion.

local dlg           = nil          -- dialogue box ID
local font          = nil
local fontBold      = nil
local fontItalic    = nil
local portraitShader = nil         -- sprite pipeline for portrait rendering
local autoplayEnabled = false
local skipEnabled = false
local autoplayDelaySec = 1.5

local backBtn   = nil          -- "Back" button to return to default scene
local autoplayBtn = nil        -- autoplay toggle button (checkbox)
local skipBtn = nil            -- skip toggle button (checkbox)
local btnTexId  = nil
local btnShader = nil

local function startDialogue()
    if dlg then
        dialogueLoad(dlg, "res/dialogue/demo/demo.dlg")
        dialogueStart(dlg, function()
            -- Loop: restart from the beginning
            startDialogue()
        end)
    end
end

local function createBackButton()
    backBtn = loadObject("res/nodes/button.lua", {
        texId    = btnTexId,
        shaderId = btnShader,
        x = -0.82, y = 0.88, size = 0.16,
        action   = ACTION_EXIT,
        r = 0.6, g = 0.6, b = 0.6,
    })
end

local function createAutoplayButton()
    autoplayBtn = loadObject("res/nodes/button.lua", {
        texId      = btnTexId,
        shaderId   = btnShader,
        x          = 0.82, y = 0.88, size = 0.16,
        action     = ACTION_TOGGLE_DIALOGUE_AUTOPLAY,
        isCheckbox = true,
        r = 0.45, g = 0.9, b = 0.45,
    })
end

local function createSkipButton()
    skipBtn = loadObject("res/nodes/button.lua", {
        texId      = btnTexId,
        shaderId   = btnShader,
        x          = 0.62, y = 0.88, size = 0.16,
        action     = ACTION_TOGGLE_DIALOGUE_SKIP,
        isCheckbox = true,
        r = 0.4, g = 0.65, b = 1.0,
    })
end

function init()
    loadShaders("res/shaders/vertex.spv", "res/shaders/nebula_fragment.spv", 0)

    font        = loadFont("res/fonts/aileron/Aileron-Regular.font")
    fontBold    = loadFont("res/fonts/aileron/Aileron-Bold.font")
    fontItalic  = loadFont("res/fonts/aileron/Aileron-Italic.font")

    btnTexId  = loadTexture("res/objects/rock/rock.png")
    btnShader = loadAnimTexturedShaders(
        "res/shaders/anim_sprite_vertex.spv",
        "res/shaders/anim_sprite_fragment.spv", 1, 1)
    portraitShader = loadAnimTexturedShaders(
        "res/shaders/anim_sprite_vertex.spv",
        "res/shaders/anim_sprite_fragment.spv", 1, 1)

    createBackButton()
    createAutoplayButton()
    createSkipButton()

    if not font then
        print("dialogue_test: font not available, scene will be mostly empty")
        return
    end

    -- Create dialogue box centred near the bottom third of the screen.
    -- Portrait slots flank the text area on left and right.
    dlg = createDialogueBox({
        font               = font,
        boldFont           = fontBold  or font,
        italicFont         = fontItalic or font,
        x                  = -0.6,    -- left edge of text area
        y                  = -0.55,   -- vertical centre of text area
        width              = 1.2,     -- text wrap width
        height             = 0.4,
        textSize           = 0.07,
        speakerTextSize    = 0.055,
        revealSpeed        = 20,
        textShadowDx       = 0.006,
        textShadowDy       = -0.006,
        textShadowR        = 0.0,
        textShadowG        = 0.0,
        textShadowB        = 0.0,
        textShadowA        = 0.75,
        portraitWidth      = 0.35,
        portraitHeight     = 0.35,
        transitionDuration = 0.18,
        portraitPipelineId = portraitShader,
    })

    setDialogueAutoplay(dlg, autoplayEnabled, autoplayDelaySec)
    setDialogueSkip(dlg, skipEnabled)

    startDialogue()
end

function update(deltaTime)
    if backBtn then backBtn.update(deltaTime) end
    if autoplayBtn then autoplayBtn.update(deltaTime) end
    if skipBtn then skipBtn.update(deltaTime) end
    -- Dialogue update is called automatically by the engine each frame.
end

function onAction(action)
    if backBtn then backBtn.onAction(action) end
    if autoplayBtn then autoplayBtn.onAction(action) end
    if skipBtn then skipBtn.onAction(action) end

    if action == ACTION_EXIT then
        -- Clean up and return to the default scene.
        if dlg then
            destroyDialogueBox(dlg)
            dlg = nil
        end
        popScene()
    elseif action == ACTION_TOGGLE_DIALOGUE_AUTOPLAY then
        autoplayEnabled = not autoplayEnabled
        if autoplayEnabled then
            -- Mutually exclusive with skip
            skipEnabled = false
            if skipBtn then skipBtn.setChecked(false) end
            if dlg then setDialogueSkip(dlg, false) end
        end
        if dlg then
            setDialogueAutoplay(dlg, autoplayEnabled, autoplayDelaySec)
        end
    elseif action == ACTION_TOGGLE_DIALOGUE_SKIP then
        skipEnabled = not skipEnabled
        if skipEnabled then
            -- Mutually exclusive with autoplay
            autoplayEnabled = false
            if autoplayBtn then autoplayBtn.setChecked(false) end
            if dlg then setDialogueAutoplay(dlg, false, autoplayDelaySec) end
        end
        if dlg then
            setDialogueSkip(dlg, skipEnabled)
        end
    elseif action == ACTION_DRAG_END or action == ACTION_TOGGLE_BLADE then
        -- Advance dialogue on click / Space
        if dlg then
            dialogueAdvance(dlg)
        end
    end
end
