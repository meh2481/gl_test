-- Particle System Editor Scene
-- This scene opens the ImGui-based particle editor for creating and tweaking particle systems

particlePipelineId = nil
previewSystemId = nil
bloomTexId = nil

function init()
    -- Load particle shaders (z-index 1, additive blending)
    particlePipelineId = loadParticleShaders("res/shaders/particle_vertex.spv", "res/shaders/particle_fragment.spv", 1, true)

    -- Load bloom texture for default particle texture
    bloomTexId = loadTexture("res/fx/bloom.png")

    -- Open the particle editor UI (ImGui-based, DEBUG builds only)
    -- The editor will create its own preview particle system
    openParticleEditor(particlePipelineId)

end

function update(deltaTime)
    -- The particle editor updates are handled by the C++ side
    -- This scene just provides a backdrop and the particle shaders
end

function cleanup()
    -- Cleanup is handled by the C++ particle editor
end

function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    end
end