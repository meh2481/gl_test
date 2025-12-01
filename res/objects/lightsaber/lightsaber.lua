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

-- Loaded resources (loaded once on first create)
Lightsaber.hiltTexId = nil
Lightsaber.hiltNormId = nil
Lightsaber.bloomTexId = nil
Lightsaber.phongShaderId = nil
Lightsaber.saberShaderId = nil
Lightsaber.resourcesLoaded = false

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

-- Load all required resources internally
local function loadResources()
    if Lightsaber.resourcesLoaded then
        return
    end

    -- Load textures (use full relative paths from project root)
    Lightsaber.hiltTexId = loadTexture("res/chain.png")
    Lightsaber.hiltNormId = loadTexture("res/chain.norm.png")
    Lightsaber.bloomTexId = loadTexture("res/common/bloom.png")

    -- Load shaders
    Lightsaber.phongShaderId = loadTexturedShadersEx("phong_multilight_vertex.spv", "phong_multilight_fragment.spv", 1, 2)
    setShaderParameters(Lightsaber.phongShaderId, 0.3, 0.7, 0.5, 32.0)

    Lightsaber.saberShaderId = loadTexturedShadersAdditive("lightsaber_vertex.spv", "lightsaber_fragment.spv", 2, 1)

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
    if Lightsaber.bladeBody then
        local x, y = b2GetBodyPosition(Lightsaber.bladeBody)
        if x ~= nil and y ~= nil then
            updateLight(Lightsaber.lightId, x, y, config.lightZ, config.colorR, config.colorG, config.colorB, config.lightIntensity)
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

function Lightsaber.reset()
    local startX = config.x
    local startY = config.y
    local bladeOffsetY = config.hiltLength / 2 + config.bladeLength / 2

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
