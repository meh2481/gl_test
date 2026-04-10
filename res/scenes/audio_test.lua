-- Audio test scene
-- Demonstrates spatial 3D audio, channel mixing, and global effects

-- Scene state
local audioSources = {}
local audioBuffer = -1
local listenerX = 0
local listenerY = 0
local listenerZ = 0
local effectType = AUDIO_EFFECT_NONE
local effectIntensity = 1.0
local globalVolume = 1.0

function init()
    -- Load the nebula shaders as background (z-index 0)
    loadShaders("res/shaders/vertex.spv", "res/shaders/nebula_fragment.spv", 0)

    -- Set listener at origin
    audioSetListenerPosition(listenerX, listenerY, listenerZ)
    audioSetListenerOrientation(0, 0, -1, 0, 1, 0) -- Looking forward, up is up

    -- Load GLA audio (IMA ADPCM) from pak resource
    print("Loading GLA audio from resource...")
    audioBuffer = audioLoadGla("res/sfx/sfx.wav")

    if audioBuffer >= 0 then
        print("Successfully loaded GLA audio buffer with ID: " .. audioBuffer)
    else
        print("Failed to load GLA audio")
    end

    -- Load shared button resources
    local texId    = loadTexture("res/objects/rock/rock.png")
    local shaderId = loadAnimTexturedShaders("res/shaders/anim_sprite_vertex.spv", "res/shaders/anim_sprite_fragment.spv", 1, 1)

    -- Row 1 (y=0.5): play-sound buttons — left / center / right
    loadObject("res/nodes/button.lua", { texId=texId, shaderId=shaderId,
        x=-0.25, y=0.5, size=0.22, action=ACTION_APPLY_FORCE,
        r=0.4, g=0.6, b=1.0 })   -- Play Left  (blue)
    loadObject("res/nodes/button.lua", { texId=texId, shaderId=shaderId,
        x=0,    y=0.5, size=0.22, action=ACTION_RESET_PHYSICS,
        r=1.0, g=1.0, b=0.4 })   -- Play Center (yellow)
    loadObject("res/nodes/button.lua", { texId=texId, shaderId=shaderId,
        x=0.25, y=0.5, size=0.22, action=ACTION_TOGGLE_DEBUG_DRAW,
        r=1.0, g=0.5, b=0.2 })   -- Play Right  (orange)

    -- Row 2 (y=0.0): volume down / up
    loadObject("res/nodes/button.lua", { texId=texId, shaderId=shaderId,
        x=-0.15, y=0.0, size=0.22, action=ACTION_PHYSICS_DEMO,
        r=1.0, g=0.35, b=0.35 }) -- Volume Down (red)
    loadObject("res/nodes/button.lua", { texId=texId, shaderId=shaderId,
        x=0.15,  y=0.0, size=0.22, action=ACTION_MENU,
        r=0.35, g=1.0, b=0.35 }) -- Volume Up   (green)

    -- Row 3 (y=-0.5): lowpass toggle (checkbox — greyscale=off, colour=on)
    loadObject("res/nodes/button.lua", { texId=texId, shaderId=shaderId,
        x=0, y=-0.5, size=0.22, action=ACTION_AUDIO_TEST,
        isCheckbox=true,
        r=0.5, g=1.0, b=1.0 })  -- Lowpass toggle (cyan when on)
end

function update(deltaTime)
    -- Nothing to update per frame for now
end

function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    elseif action == ACTION_APPLY_FORCE then
        -- Play Left button / key 1: Play sound at left position
        if audioBuffer >= 0 then
            local sourceId = audioCreateSource(audioBuffer, false, 1.0)
            if sourceId >= 0 then
                audioSetSourcePosition(sourceId, -5.0, 0.0, 0.0)
                audioPlaySource(sourceId)
                table.insert(audioSources, sourceId)
                print("Playing sound at left position")
            end
        end
    elseif action == ACTION_RESET_PHYSICS then
        -- Play Center button / key 2: Play sound at center
        if audioBuffer >= 0 then
            local sourceId = audioCreateSource(audioBuffer, false, 1.0)
            if sourceId >= 0 then
                audioSetSourcePosition(sourceId, 0.0, 0.0, 0.0)
                audioPlaySource(sourceId)
                table.insert(audioSources, sourceId)
                print("Playing sound at center")
            end
        end
    elseif action == ACTION_TOGGLE_DEBUG_DRAW then
        -- Play Right button / key 3: Play sound at right position
        if audioBuffer >= 0 then
            local sourceId = audioCreateSource(audioBuffer, false, 1.0)
            if sourceId >= 0 then
                audioSetSourcePosition(sourceId, 5.0, 0.0, 0.0)
                audioPlaySource(sourceId)
                table.insert(audioSources, sourceId)
                print("Playing sound at right position")
            end
        end
    elseif action == ACTION_AUDIO_TEST then
        -- Lowpass checkbox button / key 4: Toggle lowpass effect
        if effectType == AUDIO_EFFECT_LOWPASS then
            effectType = AUDIO_EFFECT_NONE
            audioSetGlobalEffect(AUDIO_EFFECT_NONE)
            print("Effect: None")
        else
            effectType = AUDIO_EFFECT_LOWPASS
            audioSetGlobalEffect(AUDIO_EFFECT_LOWPASS, 1.0)
            print("Effect: Lowpass")
        end
    elseif action == ACTION_TOGGLE_FULLSCREEN then
        -- Key 5: Toggle reverb effect
        if effectType == AUDIO_EFFECT_REVERB then
            effectType = AUDIO_EFFECT_NONE
            audioSetGlobalEffect(AUDIO_EFFECT_NONE)
            print("Effect: None")
        else
            effectType = AUDIO_EFFECT_REVERB
            audioSetGlobalEffect(AUDIO_EFFECT_REVERB, 1.0)
            print("Effect: Reverb")
        end
    elseif action == ACTION_HOTRELOAD then
        -- Key 6: Clear effects
        effectType = AUDIO_EFFECT_NONE
        audioSetGlobalEffect(AUDIO_EFFECT_NONE)
        print("Effects cleared")
    elseif action == ACTION_MENU then
        -- Volume Up button / +: Increase volume
        globalVolume = math.min(globalVolume + 0.1, 1.0)
        audioSetGlobalVolume(globalVolume)
        print("Global volume: " .. string.format("%.1f", globalVolume))
    elseif action == ACTION_PHYSICS_DEMO then
        -- Volume Down button / -: Decrease volume
        globalVolume = math.max(globalVolume - 0.1, 0.0)
        audioSetGlobalVolume(globalVolume)
        print("Global volume: " .. string.format("%.1f", globalVolume))
    end
end
