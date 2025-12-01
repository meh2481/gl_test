-- Physics demo scene
-- A demonstration scene with various physics objects

-- Scene-level state (non-object things)
bodies = {}
joints = {}
layers = {}
debugDrawEnabled = true

-- Mouse drag state
mouseJointId = nil
draggedBodyId = nil

-- Reference to circle body for force application
circleBody = nil

-- Shader IDs for scene-specific rendering (boxes, rocks, etc.)
phongShaderId = nil
toonShaderId = nil
simpleTexShaderId = nil

-- Texture IDs for scene-specific objects
rockTexId = nil
rockNormId = nil
metalwallTexId = nil
metalwallNormId = nil
chainTexId = nil
chainNormId = nil
lanternTexId = nil
lanternNormId = nil

function init()
    -- Load the nebula background shader (z-index -2 = background)
    loadShaders("res/shaders/vertex.spv", "res/shaders/nebula_fragment.spv", -2)

    -- Load scene-level shaders for demo boxes and shapes
    phongShaderId = loadTexturedShadersEx("res/shaders/phong_multilight_vertex.spv", "res/shaders/phong_multilight_fragment.spv", 1, 2)
    toonShaderId = loadTexturedShadersEx("res/shaders/toon_vertex.spv", "res/shaders/toon_fragment.spv", 1, 1)
    simpleTexShaderId = loadTexturedShadersEx("res/shaders/sprite_vertex.spv", "res/shaders/sprite_fragment.spv", 1, 1)

    -- Load debug drawing shader (z-index 3, drawn on top)
    loadShaders("res/shaders/debug_vertex.spv", "res/shaders/debug_fragment.spv", 3)

    -- Set shader parameters
    setShaderParameters(phongShaderId, 0.3, 0.7, 0.5, 32.0)
    setShaderParameters(toonShaderId, 0.5, 0.5, 0.25, 3.0)

    -- Load textures for scene-level objects (use full relative paths)
    rockTexId = loadTexture("res/rock.png")
    rockNormId = loadTexture("res/rock.norm.png")
    metalwallTexId = loadTexture("res/metalwall.png")
    metalwallNormId = loadTexture("res/metalwall.norm.png")
    chainTexId = loadTexture("res/chain.png")
    chainNormId = loadTexture("res/chain.norm.png")
    lanternTexId = loadTexture("res/objects/lantern/lantern.png")
    lanternNormId = loadTexture("res/objects/lantern/lantern.norm.png")

    -- Set up the multi-light system
    clearLights()
    setAmbientLight(0.15, 0.15, 0.15)

    -- Enable Box2D debug drawing
    b2EnableDebugDraw(true)

    -- Set gravity
    b2SetGravity(0, -10)

    -- Create static boundaries
    createBoundaries()

    -- Create demo boxes with various shaders
    createDemoBoxes()

    -- Create scene-level dynamic objects (rock and lantern polygon)
    createSceneObjects()

    -- Load abstracted objects - just position, objects handle everything else
    loadObject("res/objects/lantern/lantern.lua", { x = -0.605, y = 0.7 })
    loadObject("res/objects/lightsaber/lightsaber.lua", { x = 0.0, y = 0.2, colorR = 0.3, colorG = 0.7, colorB = 1.0 })
    loadObject("res/objects/lightsaber/lightsaber.lua", { x = 0.5, y = 0.2, colorR = 1.0, colorG = 0.0, colorB = 0.0 })
    loadObject("res/objects/destructible_box/destructible_box.lua", { x = 0.9, y = -0.3 })

    print("Physics demo scene initialized")
    print("Drag objects with the mouse, press R to reset")
end

function createBoundaries()
    -- Create ground
    local groundId = b2CreateBody(B2_STATIC_BODY, 0, -0.8, 0)
    b2AddSegmentFixture(groundId, -1.5, 0, 1.5, 0, 0.3, 0.0)
    table.insert(bodies, groundId)

    -- Create left wall
    local leftWallId = b2CreateBody(B2_STATIC_BODY, -1.5, 0, 0)
    b2AddSegmentFixture(leftWallId, 0, -1.0, 0, 10.0, 0.3, 0.0)
    table.insert(bodies, leftWallId)

    -- Create right wall
    local rightWallId = b2CreateBody(B2_STATIC_BODY, 1.5, 0, 0)
    b2AddSegmentFixture(rightWallId, 0, -1.0, 0, 10.0, 0.3, 0.0)
    table.insert(bodies, rightWallId)

    -- Create ceiling
    local ceilingId = b2CreateBody(B2_STATIC_BODY, 0, 10.0, 0)
    b2AddSegmentFixture(ceilingId, -1.5, 0, 1.5, 0, 0.3, 0.0)
    table.insert(bodies, ceilingId)
end

function createDemoBoxes()
    -- Create 5 dynamic boxes with different rendering
    for i = 1, 5 do
        local x = -0.5 + (i - 1) * 0.25
        local y = 0.5
        local bodyId = b2CreateBody(B2_DYNAMIC_BODY, x, y, 0)
        b2AddBoxFixture(bodyId, 0.1, 0.1, 1.0, 0.3, 0.3)
        table.insert(bodies, bodyId)

        local layerId
        if i <= 2 then
            layerId = createLayer(metalwallTexId, 0.2, metalwallNormId, phongShaderId)
        elseif i == 3 then
            layerId = createLayer(metalwallTexId, 0.2, toonShaderId)
        elseif i == 4 then
            layerId = createLayer(metalwallTexId, 0.2, simpleTexShaderId)
        else
            layerId = createLayer(metalwallNormId, 0.2, simpleTexShaderId)
        end
        attachLayerToBody(layerId, bodyId)
        table.insert(layers, layerId)
    end

    -- Create a rectangle box using chain graphic
    local chainWidth, chainHeight = getTextureDimensions(chainTexId)
    local chainAspectRatio = chainWidth / chainHeight
    local boxHeight = 0.2
    local boxWidth = boxHeight * chainAspectRatio
    local chainBoxBody = b2CreateBody(B2_DYNAMIC_BODY, -0.7, 0.3, 0)
    b2AddBoxFixture(chainBoxBody, boxWidth / 2, boxHeight / 2, 1.0, 0.3, 0.3)
    table.insert(bodies, chainBoxBody)

    local chainBoxLayerId = createLayer(chainTexId, boxHeight, chainNormId, phongShaderId)
    attachLayerToBody(chainBoxLayerId, chainBoxBody)
    table.insert(layers, chainBoxLayerId)
end

function createSceneObjects()
    -- Create a dynamic circle with rock sprite
    circleBody = b2CreateBody(B2_DYNAMIC_BODY, 0, 0.8, 0)
    b2AddCircleFixture(circleBody, 0.15, 1.0, 0.3, 0.5)
    table.insert(bodies, circleBody)

    local layerId = createLayer(rockTexId, 0.3, rockNormId, phongShaderId)
    attachLayerToBody(layerId, circleBody)
    table.insert(layers, layerId)

    -- Create a dynamic polygon with lantern sprite
    local lanternBody = b2CreateBody(B2_DYNAMIC_BODY, 0.5, 0.8, 0)
    local lanternVerts = {
        -0.06, -0.146,
         0.06, -0.146,
         0.085, -0.1,
         0.067, 0.045,
         0.0, 0.15,
        -0.067, 0.045,
        -0.085, -0.1
    }
    b2AddPolygonFixture(lanternBody, lanternVerts, 1.0, 0.3, 0.5)
    table.insert(bodies, lanternBody)

    local lanternLayerId = createLayer(lanternTexId, 0.3, lanternNormId, phongShaderId)
    attachLayerToBody(lanternLayerId, lanternBody)
    table.insert(layers, lanternLayerId)
end

function update(deltaTime)
    -- Step the physics simulation
    b2Step(deltaTime, 4)

    -- Update mouse joint target if dragging
    if mouseJointId then
        local cursorX, cursorY = getCursorPosition()
        b2UpdateMouseJointTarget(mouseJointId, cursorX, cursorY)
        if draggedBodyId then
            b2SetBodyAwake(draggedBodyId, true)
        end
    end
end

function cleanup()
    -- Destroy mouse joint if active
    if mouseJointId then
        b2DestroyMouseJoint(mouseJointId)
        mouseJointId = nil
        draggedBodyId = nil
    end

    -- Clean up fragments
    b2CleanupAllFragments()

    -- Destroy all scene layers
    for _, layerId in ipairs(layers) do
        destroyLayer(layerId)
    end
    layers = {}

    -- Destroy all joints
    for _, jointId in ipairs(joints) do
        b2DestroyJoint(jointId)
    end
    joints = {}

    -- Destroy all physics bodies
    for _, bodyId in ipairs(bodies) do
        b2ClearBodyDestructible(bodyId)
        b2DestroyBody(bodyId)
    end
    bodies = {}
    circleBody = nil

    -- Clear all lights
    clearLights()

    -- Disable debug drawing
    b2EnableDebugDraw(false)
    print("Physics demo scene cleaned up")
end

function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    end
    if action == ACTION_APPLY_FORCE then
        if circleBody then
            local x, y = b2GetBodyPosition(circleBody)
            b2ApplyForce(circleBody, 0, 50, x, y)
            vibrate(0.5, 0.5, 200)
        end
    end
    if action == ACTION_RESET_PHYSICS then
        -- Reset camera
        setCameraOffset(0, 0)
        setCameraZoom(1.0)

        -- Clean up fragments
        b2CleanupAllFragments()

        -- Reset scene bodies
        for i, bodyId in ipairs(bodies) do
            if i <= 4 then
                -- Static bodies, no reset needed
            elseif i <= 9 then
                -- Boxes
                local x = -0.5 + (i - 5) * 0.25
                b2SetBodyPosition(bodyId, x, 0.5)
                b2SetBodyAngle(bodyId, 0)
                b2SetBodyLinearVelocity(bodyId, 0, 0)
                b2SetBodyAngularVelocity(bodyId, 0)
                b2SetBodyAwake(bodyId, true)
            elseif i == 10 then
                -- Chain box
                b2SetBodyPosition(bodyId, -0.7, 0.3)
                b2SetBodyAngle(bodyId, 0)
                b2SetBodyLinearVelocity(bodyId, 0, 0)
                b2SetBodyAngularVelocity(bodyId, 0)
                b2SetBodyAwake(bodyId, true)
            elseif i == 11 then
                -- Circle (rock)
                b2SetBodyPosition(bodyId, 0, 0.8)
                b2SetBodyAngle(bodyId, 0)
                b2SetBodyLinearVelocity(bodyId, 0, 0)
                b2SetBodyAngularVelocity(bodyId, 0)
                b2SetBodyAwake(bodyId, true)
            elseif i == 12 then
                -- Lantern polygon
                b2SetBodyPosition(bodyId, 0.5, 0.8)
                b2SetBodyAngle(bodyId, 0)
                b2SetBodyLinearVelocity(bodyId, 0, 0)
                b2SetBodyAngularVelocity(bodyId, 0)
                b2SetBodyAwake(bodyId, true)
            end
        end

        vibrate(0.3, 0.3, 150)
    end
    if action == ACTION_TOGGLE_DEBUG_DRAW then
        debugDrawEnabled = not debugDrawEnabled
        b2EnableDebugDraw(debugDrawEnabled)
    end
    if action == ACTION_DRAG_START then
        local cursorX, cursorY = getCursorPosition()
        local bodyId = b2QueryBodyAtPoint(cursorX, cursorY)
        if bodyId >= 0 then
            draggedBodyId = bodyId
            mouseJointId = b2CreateMouseJoint(bodyId, cursorX, cursorY, 1000.0)
        end
    end
    if action == ACTION_DRAG_END then
        if mouseJointId then
            b2DestroyMouseJoint(mouseJointId)
            mouseJointId = nil
            draggedBodyId = nil
        end
    end
end
