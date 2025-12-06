-- Particle System Editor Scene
-- This scene opens the ImGui-based particle editor for creating and tweaking particle systems

particlePipelineAdditive = nil
particlePipelineAlpha = nil
particlePipelineSubtractive = nil
previewSystemId = nil
bloomTexId = nil

function init()
    -- Load particle shaders with different blend modes
    -- Blend modes: 0=Additive, 1=Alpha, 2=Subtractive
    particlePipelineAdditive = loadParticleShaders("res/shaders/particle_vertex.spv", "res/shaders/particle_fragment.spv", 1, 0)
    particlePipelineAlpha = loadParticleShaders("res/shaders/particle_vertex.spv", "res/shaders/particle_fragment.spv", 1, 1)
    particlePipelineSubtractive = loadParticleShaders("res/shaders/particle_vertex.spv", "res/shaders/particle_fragment.spv", 1, 2)

    -- Load bloom texture for default particle texture
    bloomTexId = loadTexture("res/fx/bloom.png")

    -- Open the particle editor UI (ImGui-based, DEBUG builds only)
    -- Pass all three pipeline IDs
    openParticleEditor(particlePipelineAdditive, particlePipelineAlpha, particlePipelineSubtractive)

end

function update(deltaTime)
    -- The particle editor updates are handled by the C++ side
    -- This scene just provides a backdrop and the particle shaders
end

function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    end
end