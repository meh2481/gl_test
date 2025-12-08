-- Lightsaber object
-- A fully self-contained lightsaber with hilt, glowing blade, physics, and dynamic light

local Lightsaber = {}

-- Object state
Lightsaber.hiltBody = nil
Lightsaber.bladeBody = nil
Lightsaber.hiltLayer = nil
Lightsaber.bladeLayer = nil
Lightsaber.lightId = nil
Lightsaber.bodies = {}
Lightsaber.layers = {}
Lightsaber.joints = {}
Lightsaber.bladeExtension = 1.0
Lightsaber.targetExtension = 1.0
Lightsaber.bladeEnabled = true

-- Loaded resources (loaded once on first create)
Lightsaber.hiltTexId = nil
Lightsaber.hiltNormId = nil
Lightsaber.bloomTexId = nil
Lightsaber.phongShaderId = nil
Lightsaber.saberShaderId = nil
Lightsaber.resourcesLoaded = false
Lightsaber.togglingBlade = false

-- Default configuration
local config = {
    x = 0,
    y = 0.2,
    bladeLength = 0.35,
    bladeWidth = 0.015,
    hiltWidth = 0.025,
    hiltLength = 0.10,
    colorR = 0.3,
    colorG = 0.7,
    colorB = 1.0,
    glowIntensity = 1.5,
    lightZ = 0.25,
    lightIntensity = 1.2
}

-- Constants
local BLADE_CORE_SCALE = 1.2
local EXTENSION_SPEED = 6.0

-- Load all required resources internally
local function loadResources()
    if Lightsaber.resourcesLoaded then
        return
    end

    -- Load textures (use full relative paths from project root)
    Lightsaber.hiltTexId = loadTexture("res/objects/lightsaber/hilt.png")
    Lightsaber.hiltNormId = loadTexture("res/objects/lightsaber/hilt.norm.png")
    Lightsaber.bloomTexId = loadTexture("res/fx/bloom.png")

    -- Load shaders
    Lightsaber.phongShaderId = loadTexturedShadersEx("res/shaders/phong_multilight_vertex.spv", "res/shaders/phong_multilight_fragment.spv", 1, 2)
    setShaderParameters(Lightsaber.phongShaderId, 0.3, 0.7, 0.5, 32.0)

    Lightsaber.saberShaderId = loadTexturedShadersAdditive("res/shaders/lightsaber_vertex.spv", "res/shaders/lightsaber_fragment.spv", 2, 1)

    Lightsaber.resourcesLoaded = true
end

function Lightsaber.create(params)
    params = params or {}

    -- Load resources if not already loaded
    loadResources()

    -- Merge params with defaults
    for k, v in pairs(params) do
        config[k] = v
    end

    local startX = config.x
    local startY = config.y

    -- Set shader parameters for this lightsaber's color
    setShaderParameters(Lightsaber.saberShaderId, config.colorR, config.colorG, config.colorB, config.glowIntensity)

    -- Create hilt body (dynamic, can be picked up and dragged)
    Lightsaber.hiltBody = b2CreateBody(B2_DYNAMIC_BODY, startX, startY, 0)
    local hiltHalfW = config.hiltWidth / 2
    local hiltHalfH = config.hiltLength / 2
    b2AddBoxFixture(Lightsaber.hiltBody, hiltHalfW, hiltHalfH, 0.75, 0.5, 0.2)
    table.insert(Lightsaber.bodies, Lightsaber.hiltBody)

    -- Create blade body (attached to hilt via revolute joint)
    local bladeOffsetY = config.hiltLength / 2 + config.bladeLength / 2
    Lightsaber.bladeBody = b2CreateBody(B2_DYNAMIC_BODY, startX, startY + bladeOffsetY, 0)
    local bladeHalfW = config.bladeWidth / 2
    local bladeHalfH = config.bladeLength / 2
    b2AddBoxFixture(Lightsaber.bladeBody, bladeHalfW, bladeHalfH, 0.1, 0.1, 0.0)
    table.insert(Lightsaber.bodies, Lightsaber.bladeBody)

    -- Connect blade to hilt with a stiff revolute joint
    local bladeJointId = b2CreateRevoluteJoint(
        Lightsaber.hiltBody,
        Lightsaber.bladeBody,
        0.0, config.hiltLength / 2,
        0.0, -config.bladeLength / 2,
        true, 0.0, 0.0
    )
    table.insert(Lightsaber.joints, bladeJointId)

    -- Hilt layer
    Lightsaber.hiltLayer = createLayer(Lightsaber.hiltTexId, config.hiltLength, Lightsaber.hiltNormId, Lightsaber.phongShaderId)
    attachLayerToBody(Lightsaber.hiltLayer, Lightsaber.hiltBody)
    table.insert(Lightsaber.layers, Lightsaber.hiltLayer)

    -- Blade core layer (use bloom texture with lightsaber shader for colored glow)
    Lightsaber.bladeLayer = createLayer(Lightsaber.bloomTexId, config.bladeLength * BLADE_CORE_SCALE, Lightsaber.saberShaderId)
    attachLayerToBody(Lightsaber.bladeLayer, Lightsaber.bladeBody)
    table.insert(Lightsaber.layers, Lightsaber.bladeLayer)
    setLayerUseLocalUV(Lightsaber.bladeLayer, true)

    -- Create the light
    Lightsaber.lightId = addLight(startX, startY + bladeOffsetY, config.lightZ, config.colorR, config.colorG, config.colorB, config.lightIntensity)

    return Lightsaber
end

function Lightsaber.update(deltaTime)
    if Lightsaber.togglingBlade then
        -- Update blade extension towards target
        if Lightsaber.bladeExtension < Lightsaber.targetExtension then
            Lightsaber.bladeExtension = math.min(Lightsaber.targetExtension, Lightsaber.bladeExtension + EXTENSION_SPEED * deltaTime)
        elseif Lightsaber.bladeExtension > Lightsaber.targetExtension then
            Lightsaber.bladeExtension = math.max(Lightsaber.targetExtension, Lightsaber.bladeExtension - EXTENSION_SPEED * deltaTime)
        end

        -- Check if toggling is complete
        if Lightsaber.bladeExtension == Lightsaber.targetExtension then
            Lightsaber.togglingBlade = false
        end

        -- Update blade visuals based on extension
        if Lightsaber.bladeLayer then
            local scaleY = Lightsaber.bladeExtension
            setLayerScale(Lightsaber.bladeLayer, 1.0, scaleY)

            local offsetY = -config.bladeLength * BLADE_CORE_SCALE * (1.0 - scaleY) / 2.0
            setLayerOffset(Lightsaber.bladeLayer, 0.0, offsetY)
        end

        -- Update blade body position and size based on extension
        if Lightsaber.bladeBody and Lightsaber.hiltBody then
            if Lightsaber.bladeExtension <= 0.01 then
                if Lightsaber.bladeEnabled then
                    b2DisableBody(Lightsaber.bladeBody)
                    Lightsaber.bladeEnabled = false
                end

                local hiltX, hiltY = b2GetBodyPosition(Lightsaber.hiltBody)
                local hiltAngle = b2GetBodyAngle(Lightsaber.hiltBody)
                local bladeOffsetY = config.hiltLength / 2 + config.bladeLength / 2

                local cosAngle = math.cos(hiltAngle)
                local sinAngle = math.sin(hiltAngle)
                local bladeX = hiltX + bladeOffsetY * sinAngle
                local bladeY = hiltY + bladeOffsetY * cosAngle

                b2SetBodyPosition(Lightsaber.bladeBody, bladeX, bladeY)
                b2SetBodyAngle(Lightsaber.bladeBody, hiltAngle)
            else
                if not Lightsaber.bladeEnabled then
                    Lightsaber.bladeEnabled = true
                    b2EnableBody(Lightsaber.bladeBody)
                end

                b2ClearAllFixtures(Lightsaber.bladeBody)

                local currentBladeLength = config.bladeLength * Lightsaber.bladeExtension
                local bladeHalfW = config.bladeWidth / 2

                local vertices = {
                    -bladeHalfW, -config.bladeLength / 2,
                    bladeHalfW, -config.bladeLength / 2,
                    bladeHalfW, -config.bladeLength / 2 + currentBladeLength,
                    -bladeHalfW, -config.bladeLength / 2 + currentBladeLength
                }
                b2AddPolygonFixture(Lightsaber.bladeBody, vertices, 0.1, 0.1, 0.0)
            end
        end

        -- Update light position and intensity
        if Lightsaber.bladeBody then
            local x, y = b2GetBodyPosition(Lightsaber.bladeBody)
            if x ~= nil and y ~= nil then
                local intensity = config.lightIntensity * Lightsaber.bladeExtension
                updateLight(Lightsaber.lightId, x, y, config.lightZ, config.colorR, config.colorG, config.colorB, intensity)
            end
        end
    end
end

function Lightsaber.getBladePosition()
    if Lightsaber.bladeBody then
        return b2GetBodyPosition(Lightsaber.bladeBody)
    end
    return nil, nil
end

function Lightsaber.getHiltPosition()
    if Lightsaber.hiltBody then
        return b2GetBodyPosition(Lightsaber.hiltBody)
    end
    return nil, nil
end

function Lightsaber.getLightId()
    return Lightsaber.lightId
end

function Lightsaber.getHiltBody()
    return Lightsaber.hiltBody
end

function Lightsaber.getBladeBody()
    return Lightsaber.bladeBody
end

function Lightsaber.toggleBlade()
    if Lightsaber.targetExtension > 0.5 then
        Lightsaber.targetExtension = 0.0
    else
        Lightsaber.targetExtension = 1.0
    end
end

function Lightsaber.onAction(action)
    if action == ACTION_TOGGLE_BLADE then
        Lightsaber.toggleBlade()
        Lightsaber.togglingBlade = true
    end
end

function Lightsaber.reset()
    local startX = config.x
    local startY = config.y
    local bladeOffsetY = config.hiltLength / 2 + config.bladeLength / 2

    -- Reset blade extension state
    Lightsaber.bladeExtension = 1.0
    Lightsaber.targetExtension = 1.0
    Lightsaber.bladeEnabled = true

    Lightsaber.togglingBlade = false

    -- Reset hilt
    if Lightsaber.hiltBody then
        b2SetBodyPosition(Lightsaber.hiltBody, startX, startY)
        b2SetBodyAngle(Lightsaber.hiltBody, 0)
        b2SetBodyLinearVelocity(Lightsaber.hiltBody, 0, 0)
        b2SetBodyAngularVelocity(Lightsaber.hiltBody, 0)
        b2SetBodyAwake(Lightsaber.hiltBody, true)
    end

    -- Reset blade
    if Lightsaber.bladeBody then
        b2SetBodyPosition(Lightsaber.bladeBody, startX, startY + bladeOffsetY)
        b2SetBodyAngle(Lightsaber.bladeBody, 0)
        b2SetBodyLinearVelocity(Lightsaber.bladeBody, 0, 0)
        b2SetBodyAngularVelocity(Lightsaber.bladeBody, 0)
        b2SetBodyAwake(Lightsaber.bladeBody, true)
        b2EnableBody(Lightsaber.bladeBody)
    end
end

function Lightsaber.cleanup()
    -- Remove light
    if Lightsaber.lightId then
        removeLight(Lightsaber.lightId)
        Lightsaber.lightId = nil
    end

    -- Destroy layers
    for _, layerId in ipairs(Lightsaber.layers) do
        destroyLayer(layerId)
    end
    Lightsaber.layers = {}

    -- Destroy joints
    for _, jointId in ipairs(Lightsaber.joints) do
        b2DestroyJoint(jointId)
    end
    Lightsaber.joints = {}

    -- Destroy bodies
    for _, bodyId in ipairs(Lightsaber.bodies) do
        b2DestroyBody(bodyId)
    end
    Lightsaber.bodies = {}
    Lightsaber.hiltBody = nil
    Lightsaber.bladeBody = nil
    Lightsaber.hiltLayer = nil
    Lightsaber.bladeLayer = nil
end

return Lightsaber
