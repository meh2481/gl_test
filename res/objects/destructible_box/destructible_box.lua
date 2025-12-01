-- Destructible box object
-- A fully self-contained destructible box with physics and fracture mechanics

local DestructibleBox = {}

-- Object state
DestructibleBox.body = nil
DestructibleBox.layer = nil

-- Loaded resources (loaded once on first create)
DestructibleBox.texId = nil
DestructibleBox.normId = nil
DestructibleBox.shaderId = nil
DestructibleBox.resourcesLoaded = false

-- Default configuration
local config = {
    x = 0.9,
    y = -0.3,
    size = 0.12,
    strength = 2.5,
    brittleness = 0.6
}

-- Box vertices for physics and fracture
local boxVerts = nil

-- Load all required resources internally
local function loadResources()
    if DestructibleBox.resourcesLoaded then
        return
    end

    -- Load textures
    DestructibleBox.texId = loadTexture("metalwall.png")
    DestructibleBox.normId = loadTexture("metalwall.norm.png")

    -- Load shaders
    DestructibleBox.shaderId = loadTexturedShadersEx("phong_multilight_vertex.spv", "phong_multilight_fragment.spv", 1, 2)
    setShaderParameters(DestructibleBox.shaderId, 0.3, 0.7, 0.5, 32.0)

    DestructibleBox.resourcesLoaded = true
end

function DestructibleBox.create(params)
    params = params or {}

    -- Load resources if not already loaded
    loadResources()

    -- Merge params with defaults
    for k, v in pairs(params) do
        config[k] = v
    end

    -- Create box vertices
    local s = config.size
    boxVerts = {
        -s, -s,
         s, -s,
         s,  s,
        -s,  s
    }

    -- Create body
    DestructibleBox.body = b2CreateBody(B2_DYNAMIC_BODY, config.x, config.y, 0)
    b2AddPolygonFixture(DestructibleBox.body, boxVerts, 1.0, 0.3, 0.3)

    -- Create layer
    DestructibleBox.layer = createLayer(DestructibleBox.texId, config.size * 2, DestructibleBox.normId, DestructibleBox.shaderId)
    attachLayerToBody(DestructibleBox.layer, DestructibleBox.body)

    -- Set body as destructible with properties
    -- The C++ side handles everything: layer destruction, fragment creation, cleanup
    b2SetBodyDestructible(DestructibleBox.body, config.strength, config.brittleness, boxVerts, DestructibleBox.texId, DestructibleBox.normId, DestructibleBox.shaderId)
    b2SetBodyDestructibleLayer(DestructibleBox.body, DestructibleBox.layer)

    return DestructibleBox
end

function DestructibleBox.update(deltaTime)
    -- Check if the box was destroyed and needs to be recreated
    if DestructibleBox.body then
        local x, y = b2GetBodyPosition(DestructibleBox.body)
        if x == nil then
            -- Body was destroyed, recreate it
            DestructibleBox.body = b2CreateBody(B2_DYNAMIC_BODY, config.x, config.y, 0)
            b2AddPolygonFixture(DestructibleBox.body, boxVerts, 1.0, 0.3, 0.3)

            DestructibleBox.layer = createLayer(DestructibleBox.texId, config.size * 2, DestructibleBox.normId, DestructibleBox.shaderId)
            attachLayerToBody(DestructibleBox.layer, DestructibleBox.body)

            b2SetBodyDestructible(DestructibleBox.body, config.strength, config.brittleness, boxVerts, DestructibleBox.texId, DestructibleBox.normId, DestructibleBox.shaderId)
            b2SetBodyDestructibleLayer(DestructibleBox.body, DestructibleBox.layer)
        end
    end
end

function DestructibleBox.getPosition()
    if DestructibleBox.body then
        return b2GetBodyPosition(DestructibleBox.body)
    end
    return nil, nil
end

function DestructibleBox.getBody()
    return DestructibleBox.body
end

function DestructibleBox.reset()
    -- Clean up any fragments
    b2CleanupAllFragments()

    -- Check if body still exists
    if DestructibleBox.body then
        local x, y = b2GetBodyPosition(DestructibleBox.body)
        if x ~= nil then
            -- Body exists, just reset position
            b2SetBodyPosition(DestructibleBox.body, config.x, config.y)
            b2SetBodyAngle(DestructibleBox.body, 0)
            b2SetBodyLinearVelocity(DestructibleBox.body, 0, 0)
            b2SetBodyAngularVelocity(DestructibleBox.body, 0)
            b2SetBodyAwake(DestructibleBox.body, true)
            return
        end
    end

    -- Body was destroyed, recreate it
    DestructibleBox.body = b2CreateBody(B2_DYNAMIC_BODY, config.x, config.y, 0)
    b2AddPolygonFixture(DestructibleBox.body, boxVerts, 1.0, 0.3, 0.3)

    DestructibleBox.layer = createLayer(DestructibleBox.texId, config.size * 2, DestructibleBox.normId, DestructibleBox.shaderId)
    attachLayerToBody(DestructibleBox.layer, DestructibleBox.body)

    b2SetBodyDestructible(DestructibleBox.body, config.strength, config.brittleness, boxVerts, DestructibleBox.texId, DestructibleBox.normId, DestructibleBox.shaderId)
    b2SetBodyDestructibleLayer(DestructibleBox.body, DestructibleBox.layer)
end

function DestructibleBox.cleanup()
    -- Clean up fragments
    b2CleanupAllFragments()

    -- Destroy layer
    if DestructibleBox.layer then
        destroyLayer(DestructibleBox.layer)
        DestructibleBox.layer = nil
    end

    -- Destroy body
    if DestructibleBox.body then
        b2ClearBodyDestructible(DestructibleBox.body)
        b2DestroyBody(DestructibleBox.body)
        DestructibleBox.body = nil
    end
end

return DestructibleBox
