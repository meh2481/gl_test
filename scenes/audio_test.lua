-- Audio test scene
-- Demonstrates spatial 3D audio, channel mixing, and global effects

-- Scene state
local audioSources = {}
local listenerX = 0
local listenerY = 0
local listenerZ = 0
local effectType = AUDIO_EFFECT_NONE
local effectIntensity = 1.0

function init()
    -- Load the nebula shaders as background (z-index 0)
    loadShaders("vertex.spv", "nebula_fragment.spv", 0)
    
    print("Audio test scene initialized")
    print("Controls:")
    print("  1-3: Play test sounds at different positions")
    print("  4: Toggle lowpass filter effect")
    print("  5: Toggle reverb effect")
    print("  6: Clear effects")
    print("  +/-: Adjust global volume")
    print("  Arrow keys: Move listener position")
    print("  ESC: Exit scene")
    
    -- Set listener at origin
    audioSetListenerPosition(listenerX, listenerY, listenerZ)
    audioSetListenerOrientation(0, 0, -1, 0, 1, 0) -- Looking forward, up is up
    
    -- Example: Create a simple sine wave audio buffer (440 Hz tone)
    -- In a real scenario, you would load audio from files or pak resources
    -- For now, this demonstrates the API usage
    print("Audio system ready")
    print("Note: Audio buffer loading from files not yet implemented")
    print("This scene demonstrates the audio API structure")
end

function update(deltaTime)
    -- Nothing to update per frame for now
end

function onAction(action)
    if action == ACTION_EXIT then
        -- Cleanup audio sources
        for _, sourceId in ipairs(audioSources) do
            audioReleaseSource(sourceId)
        end
        popScene()
    end
end

function onKeyDown(keyCode)
    -- Test sound 1 at left position
    if keyCode == SDLK_1 then
        print("Playing test sound at left position (-5, 0, 0)")
        -- Example API call (would work with actual audio buffers)
        -- local bufferId = audioLoadBuffer("test_sound.wav")
        -- local sourceId = audioCreateSource(bufferId, false, 1.0)
        -- audioSetSourcePosition(sourceId, -5, 0, 0)
        -- audioPlaySource(sourceId)
        -- table.insert(audioSources, sourceId)
    end
    
    -- Test sound 2 at right position
    if keyCode == SDLK_2 then
        print("Playing test sound at right position (5, 0, 0)")
        -- Example API call (would work with actual audio buffers)
    end
    
    -- Test sound 3 at front position
    if keyCode == SDLK_3 then
        print("Playing test sound at front position (0, 0, -5)")
        -- Example API call (would work with actual audio buffers)
    end
    
    -- Toggle lowpass filter
    if keyCode == SDLK_4 then
        if effectType == AUDIO_EFFECT_LOWPASS then
            effectType = AUDIO_EFFECT_NONE
            audioSetGlobalEffect(AUDIO_EFFECT_NONE)
            print("Lowpass filter disabled")
        else
            effectType = AUDIO_EFFECT_LOWPASS
            audioSetGlobalEffect(AUDIO_EFFECT_LOWPASS, effectIntensity)
            print("Lowpass filter enabled (simulates underwater/muffled sound)")
        end
    end
    
    -- Toggle reverb
    if keyCode == SDLK_5 then
        if effectType == AUDIO_EFFECT_REVERB then
            effectType = AUDIO_EFFECT_NONE
            audioSetGlobalEffect(AUDIO_EFFECT_NONE)
            print("Reverb disabled")
        else
            effectType = AUDIO_EFFECT_REVERB
            audioSetGlobalEffect(AUDIO_EFFECT_REVERB, effectIntensity)
            print("Reverb enabled (simulates large space)")
        end
    end
    
    -- Clear effects
    if keyCode == SDLK_6 then
        effectType = AUDIO_EFFECT_NONE
        audioSetGlobalEffect(AUDIO_EFFECT_NONE)
        print("All effects cleared")
    end
    
    -- Increase volume
    if keyCode == SDLK_EQUALS or keyCode == SDLK_PLUS or keyCode == SDLK_KP_PLUS then
        audioSetGlobalVolume(1.0)
        print("Volume increased")
    end
    
    -- Decrease volume
    if keyCode == SDLK_MINUS or keyCode == SDLK_KP_MINUS then
        audioSetGlobalVolume(0.5)
        print("Volume decreased")
    end
    
    -- Move listener left
    if keyCode == SDLK_LEFT then
        listenerX = listenerX - 1
        audioSetListenerPosition(listenerX, listenerY, listenerZ)
        print("Listener moved to (" .. listenerX .. ", " .. listenerY .. ", " .. listenerZ .. ")")
    end
    
    -- Move listener right
    if keyCode == SDLK_RIGHT then
        listenerX = listenerX + 1
        audioSetListenerPosition(listenerX, listenerY, listenerZ)
        print("Listener moved to (" .. listenerX .. ", " .. listenerY .. ", " .. listenerZ .. ")")
    end
    
    -- Move listener forward
    if keyCode == SDLK_UP then
        listenerZ = listenerZ - 1
        audioSetListenerPosition(listenerX, listenerY, listenerZ)
        print("Listener moved to (" .. listenerX .. ", " .. listenerY .. ", " .. listenerZ .. ")")
    end
    
    -- Move listener backward
    if keyCode == SDLK_DOWN then
        listenerZ = listenerZ + 1
        audioSetListenerPosition(listenerX, listenerY, listenerZ)
        print("Listener moved to (" .. listenerX .. ", " .. listenerY .. ", " .. listenerZ .. ")")
    end
end

function cleanup()
    print("Audio test scene cleanup")
    -- Release all audio sources
    for _, sourceId in ipairs(audioSources) do
        audioReleaseSource(sourceId)
    end
end
