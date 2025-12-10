
local Rock = {}

Rock.body = nil
Rock.layer = nil

Rock.texId = nil
Rock.normId = nil
Rock.shaderId = nil
Rock.resourcesLoaded = false

local config = {
    x = 0.0,
    y = 0.8,
    radius = 0.15
}

local function loadResources()
    if Rock.resourcesLoaded then
        return
    end

    -- Load textures
    Rock.texId = loadTexture("res/objects/rock/rock.png")
    Rock.normId = loadTexture("res/objects/rock/rock.norm.png")

    -- Load shaders
    Rock.shaderId = loadTexturedShadersEx("res/shaders/phong_multilight_vertex.spv", "res/shaders/phong_multilight_fragment.spv", 1, 2)
    setShaderParameters(Rock.shaderId, 0.3, 0.7, 0.5, 32.0)

    Rock.resourcesLoaded = true
end

function Rock.create(params)
    params = params or {}

    loadResources()

    for k, v in pairs(params) do
        config[k] = v
    end

    Rock.body = b2CreateBody(B2_DYNAMIC_BODY, config.x, config.y, 0)
    b2AddCircleFixture(Rock.body, config.radius, 1.0, 0.3, 0.5)

    -- Add "heavy" type for type-based interactions
    b2AddBodyType(Rock.body, "heavy")

    Rock.layer = createLayer(Rock.texId, 0.3, Rock.normId, Rock.shaderId)
    attachLayerToBody(Rock.layer, Rock.body)

    return Rock
end

function Rock.reset()
    -- Reset position and velocity
    if Rock.body then
        b2SetBodyPosition(Rock.body, config.x, config.y)
        b2SetBodyAngle(Rock.body, 0.0)
        b2SetBodyLinearVelocity(Rock.body, 0.0, 0.0)
        b2SetBodyAngularVelocity(Rock.body, 0.0)
        b2SetBodyAwake(Rock.body, true)
    end
end

function Rock.cleanup()
    if Rock.layer then
        destroyLayer(Rock.layer)
        Rock.layer = nil
    end

    if Rock.body then
        b2DestroyBody(Rock.body)
        Rock.body = nil
    end
end

return Rock