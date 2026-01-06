-- Hanging lantern object
-- A fully self-contained lantern with chain physics, bloom effect, particle system, and dynamic light

local Lantern = {}

-- Object state
Lantern.bodies = {}
Lantern.layers = {}
Lantern.joints = {}
Lantern.chainLinks = {}
Lantern.lightBody = nil
Lantern.chainAnchor = nil
Lantern.lightId = nil
Lantern.particleSystemId = nil
Lantern.config = nil
Lantern.bloomLayerId = nil

-- Loaded resources (loaded once on first create)
Lantern.lanternTexId = nil
Lantern.lanternNormId = nil
Lantern.chainTexId = nil
Lantern.chainNormId = nil
Lantern.bloomTexId = nil
Lantern.smokeTexId = nil
Lantern.phongShaderId = nil
Lantern.bloomShaderId = nil
Lantern.particlePipelineId = nil
Lantern.resourcesLoaded = false

-- Default configuration
local config = {
    x = 0,
    y = 0.7,
    chainLength = 10,
    chainLinkHeight = 0.04,
    chainOffset = 0.0035,
    lightZ = 0.25,
    lightR = 1.0,
    lightG = 0.95,
    lightB = 0.85,
    lightIntensity = 1.5,
    enableParticles = true
}

-- Load all required resources internally
local function loadResources()
    if Lantern.resourcesLoaded then
        return
    end

    -- Load textures (use full relative paths from project root)
    Lantern.lanternTexId = loadTexture("res/objects/lantern/lantern.png")
    Lantern.lanternNormId = loadTexture("res/objects/lantern/lantern.norm.png")
    Lantern.chainTexId = loadTexture("res/objects/lantern/chain.png")
    Lantern.chainNormId = loadTexture("res/objects/lantern/chain.norm.png")
    Lantern.bloomTexId = loadTexture("res/fx/bloom.png")
    Lantern.smokeTexId = loadTexture("res/fx/sponge2.png")

    -- Load shaders
    Lantern.phongShaderId = loadTexturedShadersEx("res/shaders/phong_multilight_vertex.spv", "res/shaders/phong_multilight_fragment.spv", 1, 2)
    setShaderParameters(Lantern.phongShaderId, 0.3, 0.7, 0.5, 32.0)

    Lantern.bloomShaderId = loadTexturedShadersAdditive("res/shaders/sprite_vertex.spv", "res/shaders/sprite_fragment.spv", 2, 1)

    -- Load particle pipeline
    Lantern.particlePipelineId = loadParticleShaders("res/shaders/particle_vertex.spv", "res/shaders/particle_fragment.spv", 1, true)

    Lantern.resourcesLoaded = true
end

function Lantern.create(params)
    params = params or {}

    -- Load resources if not already loaded
    loadResources()

    -- Merge params with defaults
    for k, v in pairs(params) do
        config[k] = v
    end

    Lantern.config = config

    local startX = config.x
    local startY = config.y
    local linkHeight = config.chainLinkHeight

    -- Create anchor point (static body at the top)
    Lantern.chainAnchor = b2CreateBody(B2_STATIC_BODY, startX, startY, 0)
    b2AddCircleFixture(Lantern.chainAnchor, 0.002, 1.0, 0.3, 0.0)
    table.insert(Lantern.bodies, Lantern.chainAnchor)

    local prevBodyId = Lantern.chainAnchor

    -- Create chain links
    for i = 1, config.chainLength do
        local linkY = startY - i * linkHeight
        local linkId = b2CreateBody(B2_DYNAMIC_BODY, startX, linkY, 0)
        b2AddBoxFixture(linkId, 0.01, linkHeight / 2, 0.5, 0.3, 0.0)
        table.insert(Lantern.bodies, linkId)
        table.insert(Lantern.chainLinks, linkId)

        -- Attach sprite layer to chain link
        local layerId = createLayer(Lantern.chainTexId, linkHeight, Lantern.chainNormId, Lantern.phongShaderId)
        attachLayerToBody(layerId, linkId)
        table.insert(Lantern.layers, layerId)

        if prevBodyId ~= nil then
            -- Create revolute joint to connect to previous link/anchor
            local jointId = b2CreateRevoluteJoint(
                prevBodyId,
                linkId,
                0.0, -linkHeight / 2 + config.chainOffset,
                0.0, linkHeight / 2 - config.chainOffset,
                false, 0.0, 0.0
            )
            table.insert(Lantern.joints, jointId)
        end

        prevBodyId = linkId
    end

    -- Create light body at the end of the chain
    Lantern.lightBody = b2CreateBody(B2_DYNAMIC_BODY, startX, startY - (config.chainLength + 0.5) * linkHeight, 0)
    local smallLanternVerts = {
        0.02, -0.048,
        -0.02, -0.048,
        0.03, -0.035,
        0.024, 0.015,
        0.0, 0.045,
        -0.024, 0.015,
        -0.03, -0.035
    }
    b2AddPolygonFixture(Lantern.lightBody, smallLanternVerts, 0.2, 0.3, 0.3)
    table.insert(Lantern.bodies, Lantern.lightBody)

    -- Attach lantern sprite to the light body
    local lanternLayerId = createLayer(Lantern.lanternTexId, 0.1, Lantern.lanternNormId, Lantern.phongShaderId)
    attachLayerToBody(lanternLayerId, Lantern.lightBody)
    setLayerOffset(lanternLayerId, 0, -0.001)
    table.insert(Lantern.layers, lanternLayerId)

    -- Add bloom effect
    Lantern.bloomLayerId = createLayer(Lantern.bloomTexId, 1.6, Lantern.bloomShaderId)
    attachLayerToBody(Lantern.bloomLayerId, Lantern.lightBody)
    setLayerOffset(Lantern.bloomLayerId, 0, -0.004)
    table.insert(Lantern.layers, Lantern.bloomLayerId)

    -- Connect light to the last chain link
    local lightJointId = b2CreateRevoluteJoint(
        prevBodyId,
        Lantern.lightBody,
        0.0, -linkHeight / 2 + config.chainOffset,
        0.0, 0.05 - config.chainOffset,
        false, 0.0, 0.0
    )
    table.insert(Lantern.joints, lightJointId)
    Lantern.attached = true

    -- Create the light
    Lantern.lightId = addLight(startX, startY, config.lightZ, config.lightR, config.lightG, config.lightB, config.lightIntensity)

    -- Add "fire" type to the light body for type-based interactions
    b2AddBodyType(Lantern.lightBody, "fire")

    -- Create particle system if enabled
    if config.enableParticles and Lantern.particlePipelineId then
        local particleConfig = loadParticleConfig("res/fx/lantern_bugs.lua")
        if particleConfig then
            particleConfig.textureIds = {Lantern.bloomTexId}
            particleConfig.textureCount = 1
            Lantern.particleSystemId = createParticleSystem(particleConfig, Lantern.particlePipelineId)
            setParticleSystemPosition(Lantern.particleSystemId, startX, startY)
        end
    end

    return Lantern
end

function Lantern.extinguish()
    -- Use particle system ID to check if already extinguished
    if not Lantern.particleSystemId then return end

    -- Get lantern position for smoke effect
    local x, y = b2GetBodyPosition(Lantern.lightBody)
    if x == nil or y == nil then return end

    -- Create smoke puff effect
    local smokeConfig = loadParticleConfig("res/fx/smoke_puff.lua")
    if smokeConfig then
        smokeConfig.textureIds = {Lantern.smokeTexId}
        smokeConfig.textureCount = 1
        local smokeParticleSystemId = createParticleSystem(smokeConfig, Lantern.particlePipelineId)
        setParticleSystemPosition(smokeParticleSystemId, x, y)
        -- Note: smoke system will auto-destroy after systemLifetime (0.02 seconds)
    end

    -- Destroy existing particle system
    if Lantern.particleSystemId then
        destroyParticleSystem(Lantern.particleSystemId)
        Lantern.particleSystemId = nil
    end

    updateLightIntensity(Lantern.lightId, 0)

    if Lantern.bloomLayerId then
        setLayerScale(Lantern.bloomLayerId, 0, 0)
    end
end

function Lantern.relight()
    if Lantern.particleSystemId then return end

    local x, y = b2GetBodyPosition(Lantern.lightBody)
    if x == nil or y == nil then return end

    -- Animate light intensity from 0 to target intensity over 0.5 seconds with ease-out
    animateLightIntensity(Lantern.lightId, 0.0, Lantern.config.lightIntensity, 0.5, INTERPOLATION_EASE_OUT)

    if Lantern.bloomLayerId then
        -- Animate bloom scale from 0,0 to 1.0,1.0 over 0.5 seconds with ease-out interpolation
        animateLayerScale(Lantern.bloomLayerId, 0.0, 0.0, 1.0, 1.0, 0.5, INTERPOLATION_EASE_OUT)
    end

    if Lantern.config.enableParticles and Lantern.particlePipelineId and not Lantern.particleSystemId then
        local particleConfig = loadParticleConfig("res/fx/lantern_bugs.lua")
        if particleConfig then
            particleConfig.textureIds = {Lantern.bloomTexId}
            particleConfig.textureCount = 1
            Lantern.particleSystemId = createParticleSystem(particleConfig, Lantern.particlePipelineId)
            setParticleSystemPosition(Lantern.particleSystemId, x, y)
        end
    end
end

function Lantern.onAction(action)
    if action == ACTION_TOGGLE_BLADE then
        Lantern.toggleAttach()
    end
end

function Lantern.toggleAttach()
    if Lantern.attached then
        -- Detach the lantern
        if #Lantern.joints > 0 then
            b2DestroyJoint(Lantern.joints[#Lantern.joints])
            table.remove(Lantern.joints)
        end
        Lantern.attached = false
    else
        -- Reattach the lantern
        local prevBodyId = Lantern.chainLinks[#Lantern.chainLinks]
        if prevBodyId then
            local linkHeight = Lantern.config.chainLinkHeight
            -- Position the lantern below the last chain link
            local x, y = b2GetBodyPosition(prevBodyId)
            if x and y then
                b2SetBodyPosition(Lantern.lightBody, x, y - linkHeight)
                b2SetBodyLinearVelocity(Lantern.lightBody, 0, 0)
                b2SetBodyAngularVelocity(Lantern.lightBody, 0)
                -- Recreate the joint
                local jointId = b2CreateRevoluteJoint(
                    prevBodyId,
                    Lantern.lightBody,
                    0.0, -linkHeight / 2 + Lantern.config.chainOffset,
                    0.0, 0.05 - Lantern.config.chainOffset,
                    false, 0.0, 0.0
                )
                table.insert(Lantern.joints, jointId)
                Lantern.attached = true
            end
        end
    end
end

function Lantern.update(deltaTime)
    if Lantern.lightBody and Lantern.particleSystemId then
        local x, y = b2GetBodyPosition(Lantern.lightBody)
        if x ~= nil and y ~= nil then
            updateLightPosition(Lantern.lightId, x, y, config.lightZ)
            if Lantern.particleSystemId then
                setParticleSystemPosition(Lantern.particleSystemId, x, y)
            end
        end
    end
end

function Lantern.getPosition()
    if Lantern.lightBody then
        return b2GetBodyPosition(Lantern.lightBody)
    end
    return nil, nil
end

function Lantern.getLightId()
    return Lantern.lightId
end

function Lantern.reset()
    local startX = config.x
    local startY = config.y
    local linkHeight = config.chainLinkHeight

    -- Reset chain anchor
    if Lantern.chainAnchor then
        b2SetBodyPosition(Lantern.chainAnchor, startX, startY)
        b2SetBodyAngle(Lantern.chainAnchor, 0)
    end

    -- Reset chain links
    for i, linkId in ipairs(Lantern.chainLinks) do
        local linkY = startY - i * linkHeight
        b2SetBodyPosition(linkId, startX, linkY)
        b2SetBodyAngle(linkId, 0)
        b2SetBodyLinearVelocity(linkId, 0, 0)
        b2SetBodyAngularVelocity(linkId, 0)
        b2SetBodyAwake(linkId, true)
    end

    -- Reset light body
    if Lantern.lightBody then
        local lightY = startY - (config.chainLength + 0.5) * linkHeight
        b2SetBodyPosition(Lantern.lightBody, startX, lightY)
        b2SetBodyAngle(Lantern.lightBody, 0)
        b2SetBodyLinearVelocity(Lantern.lightBody, 0, 0)
        b2SetBodyAngularVelocity(Lantern.lightBody, 0)
        b2SetBodyAwake(Lantern.lightBody, true)
    end

    -- Ensure lantern is attached
    if not Lantern.attached then
        local prevBodyId = Lantern.chainLinks[#Lantern.chainLinks]
        local linkHeight = config.chainLinkHeight
        local jointId = b2CreateRevoluteJoint(
            prevBodyId,
            Lantern.lightBody,
            0.0, -linkHeight / 2 + config.chainOffset,
            0.0, 0.05 - config.chainOffset,
            false, 0.0, 0.0
        )
        table.insert(Lantern.joints, jointId)
        Lantern.attached = true
    end
end

function Lantern.cleanup()
    -- Destroy particle system
    if Lantern.particleSystemId then
        destroyParticleSystem(Lantern.particleSystemId)
        Lantern.particleSystemId = nil
    end

    -- Remove light
    if Lantern.lightId then
        removeLight(Lantern.lightId)
        Lantern.lightId = nil
    end

    -- Destroy layers
    for _, layerId in ipairs(Lantern.layers) do
        destroyLayer(layerId)
    end
    Lantern.layers = {}

    -- Destroy joints
    for _, jointId in ipairs(Lantern.joints) do
        b2DestroyJoint(jointId)
    end
    Lantern.joints = {}

    -- Destroy bodies
    for _, bodyId in ipairs(Lantern.bodies) do
        b2DestroyBody(bodyId)
    end
    Lantern.bodies = {}
    Lantern.chainLinks = {}
    Lantern.lightBody = nil
    Lantern.chainAnchor = nil
end

return Lantern
