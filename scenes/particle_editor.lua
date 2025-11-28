-- Particle System Editor Scene
-- This scene opens the ImGui-based particle editor for creating and tweaking particle systems

particlePipelineId = nil
previewSystemId = nil
bloomTexId = nil

function init()
    -- Load the nebula background shader (z-index -1 = background)
    loadShaders("vertex.spv", "nebula_fragment.spv", -1)

    -- Load particle shaders (z-index 1, additive blending)
    particlePipelineId = loadParticleShaders("particle_vertex.spv", "particle_fragment.spv", 1, true)

    -- Load bloom texture for default particle texture
    bloomTexId = loadTexture("bloom.png")

    -- Open the particle editor UI (ImGui-based, DEBUG builds only)
    -- The editor will create its own preview particle system
    openParticleEditor(particlePipelineId)

    print("Particle System Editor initialized")
    print("Use the ImGui window to edit particle system properties")
    print("Press ESC to return to the previous scene")
end

function update(deltaTime)
    -- The particle editor updates are handled by the C++ side
    -- This scene just provides a backdrop and the particle shaders
end

function cleanup()
    -- Cleanup is handled by the C++ particle editor
    print("Particle Editor scene cleaned up")
end

function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    end
end
