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

    -- Load OPUS audio from pak resource
    print("Loading OPUS audio from resource...")
    audioBuffer = audioLoadOpus("res/sfx/sfx.opus")

    if audioBuffer >= 0 then
        print("Successfully loaded OPUS audio buffer with ID: " .. audioBuffer)
    else
        print("Failed to load OPUS audio")
    end
end

function update(deltaTime)
    -- Nothing to update per frame for now
end

function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    elseif action == ACTION_APPLY_FORCE then
        -- Key 1: Play sound at left position
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
        -- Key 2: Play sound at center
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
        -- Key 3: Play sound at right position
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
        -- Key 4: Toggle lowpass effect
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
        -- +: Increase volume
        globalVolume = math.min(globalVolume + 0.1, 1.0)
        audioSetGlobalVolume(globalVolume)
        print("Global volume: " .. string.format("%.1f", globalVolume))
    elseif action == ACTION_PHYSICS_DEMO then
        -- -: Decrease volume
        globalVolume = math.max(globalVolume - 0.1, 0.0)
        audioSetGlobalVolume(globalVolume)
        print("Global volume: " .. string.format("%.1f", globalVolume))
    end
end
