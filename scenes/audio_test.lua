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

function cleanup()
    print("Audio test scene cleanup")
    -- Release all audio sources
    for _, sourceId in ipairs(audioSources) do
        audioReleaseSource(sourceId)
    end
end
