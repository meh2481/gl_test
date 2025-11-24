-- Aspect ratio test scene
bodies = {}
layers = {}

function init()
    -- Load simple textured shader (z-index 1, 1 texture)
    simpleTexShaderId = loadTexturedShadersEx("sprite_vertex.spv", "sprite_fragment.spv", 1, 1)

    -- Load textures
    testRectTexId = loadTexture("test_rect.png")

    -- Set gravity
    b2SetGravity(0, -10)

    -- Create ground (static body)
    local groundId = b2CreateBody(B2_STATIC_BODY, 0, -0.8, 0)
    b2AddBoxFixture(groundId, 1.5, 0.1, 1.0, 0.3, 0.0)
    table.insert(bodies, groundId)

    -- Get texture dimensions to show they're available
    local texWidth, texHeight = getTextureDimensions(testRectTexId)
    if texWidth and texHeight then
        print(string.format("Test rect texture dimensions: %dx%d (aspect ratio %.2f:1)", texWidth, texHeight, texWidth/texHeight))
    end

    -- Create three bodies to demonstrate the difference:
    -- Left: Old API with explicit width/height that stretches to square (WRONG)
    -- Middle: Old API with correct aspect ratio (manual calculation)
    -- Right: New API with automatic aspect ratio (size parameter only)

    -- Left body - intentionally stretched to square (WRONG - should be 2:1 ratio)
    local leftBody = b2CreateBody(B2_DYNAMIC_BODY, -0.5, 0.5, 0)
    b2AddBoxFixture(leftBody, 0.15, 0.15, 1.0, 0.3, 0.3)
    table.insert(bodies, leftBody)
    local leftLayer = createLayer(testRectTexId, 0.3, 0.3, simpleTexShaderId)  -- Stretched to square (WRONG)
    attachLayerToBody(leftLayer, leftBody)
    table.insert(layers, leftLayer)

    -- Middle body - old API with manual correct aspect ratio (2:1)
    local midBody = b2CreateBody(B2_DYNAMIC_BODY, 0, 0.5, 0)
    b2AddBoxFixture(midBody, 0.15, 0.075, 1.0, 0.3, 0.3)
    table.insert(bodies, midBody)
    local midLayer = createLayer(testRectTexId, 0.3, 0.15, simpleTexShaderId)  -- Correct 2:1 aspect ratio (manual)
    attachLayerToBody(midLayer, midBody)
    table.insert(layers, midLayer)

    -- Right body - new API with automatic aspect ratio
    local rightBody = b2CreateBody(B2_DYNAMIC_BODY, 0.5, 0.5, 0)
    b2AddBoxFixture(rightBody, 0.15, 0.075, 1.0, 0.3, 0.3)
    table.insert(bodies, rightBody)
    local rightLayer = createLayer(testRectTexId, 0.3, simpleTexShaderId)  -- Auto aspect ratio (NEW API)
    attachLayerToBody(rightLayer, rightBody)
    table.insert(layers, rightLayer)

    print("Aspect ratio test scene initialized")
    print("Left: Stretched to square (WRONG), Middle: Manual 2:1 (CORRECT), Right: Auto 2:1 (CORRECT)")
    print("Press ESC to return to default scene")
end

function update(deltaTime)
    -- Step the physics simulation
    b2Step(deltaTime, 4)
end

function cleanup()
    -- Destroy all scene layers
    for i, layerId in ipairs(layers) do
        destroyLayer(layerId)
    end
    layers = {}

    -- Destroy all physics bodies
    for i, bodyId in ipairs(bodies) do
        b2DestroyBody(bodyId)
    end
    bodies = {}
    print("Aspect ratio test scene cleaned up")
end

-- Handle actions
function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    end
end
