-- Lantern bugs particle system
-- Sparkle effect that follows the swinging lantern in the physics scene

-- Note: textureIds must be set after loading textures in the scene
-- Usage: local config = loadParticleConfig("lantern_bugs.lua")
--        config.textureIds = {bloomTexId}
--        particleSystemId = createParticleSystem(config, particlePipelineId)

local particleConfig = {
    maxParticles = 20,
    emissionRate = 5.0,
    blendMode = 0,  -- PARTICLE_BLEND_ADDITIVE

    -- Position at the top center of the screen
    emissionVertices = {0.0, 0.0},  -- Point emitter
    emissionVertexCount = 0,  -- 0 = point emitter

    -- Velocity: upward with some spread
    velocityMinX = 0.0,
    velocityMaxX = 0.0,
    velocityMinY = 0.0,
    velocityMaxY = 0.0,

    radialVelocityMin = 0.5,
    radialVelocityMax = 1.0,

    radialAccelerationMin = 2.0,
    radialAccelerationMax = 5.0,

    -- Acceleration: gravity
    accelerationMinX = 0.0,
    accelerationMaxX = 0.0,
    accelerationMinY = 0.0,
    accelerationMaxY = 0.0,

    -- Size: small particles that grow
    startSizeMin = 0.1,
    startSizeMax = 0.2,
    endSizeMin = 0.5,
    endSizeMax = 1.0,

    -- Color: white to transparent
    colorMinR = 1.0, colorMaxR = 1.0,
    colorMinG = 1.0, colorMaxG = 1.0,
    colorMinB = 1.0, colorMaxB = 1.0,
    colorMinA = 1.0, colorMaxA = 1.0,
    endColorMinR = 1.0, endColorMaxR = 1.0,
    endColorMinG = 1.0, endColorMaxG = 1.0,
    endColorMinB = 1.0, endColorMaxB = 1.0,
    endColorMinA = 0.0, endColorMaxA = 0.0,

    -- Lifetime: 0.5-1 seconds
    lifetimeMin = 0.5,
    lifetimeMax = 1.0,

    -- Textures (stored as names for editor)
    textureNames = {"res/fx/bloom.png"}
}

return particleConfig
