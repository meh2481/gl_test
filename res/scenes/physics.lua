-- Physics demo scene
-- A demonstration scene with various physics objects

-- Scene-level state (non-object things)
objects = {}
debugDrawEnabled = true

-- Mouse drag state
mouseJointId = nil
draggedBodyId = nil

-- Foreground layer
foregroundLayerId = nil
foregroundShaderId = nil
foregroundTexId = nil

-- Water layer
waterLayerId = nil
waterShaderId = nil
waterField = nil
waterTime = 0
waterRotationTime = 0

function init()
    -- Load the nebula background shader (z-index -2 = background)
    loadShaders("res/shaders/vertex.spv", "res/shaders/nebula_fragment.spv", -2)

    -- Load debug drawing shader (z-index 3, drawn on top)
    loadShaders("res/shaders/debug_vertex.spv", "res/shaders/debug_fragment.spv", 3)

    -- Load foreground shader (z-index 2, drawn in front of objects but behind debug)
    -- foregroundShaderId = loadTexturedShaders("res/shaders/sprite_vertex.spv", "res/shaders/sprite_fragment.spv", 2)
    foregroundShaderId = loadAnimTexturedShaders("res/shaders/anim_sprite_vertex.spv",
                                   "res/shaders/anim_sprite_fragment.spv", 1, 1)

    -- Load foreground texture and create layer
    foregroundTexId = loadTexture("res/textures/rock1.png")
    foregroundLayerId = createLayer(foregroundTexId, 5.5, foregroundShaderId)
    setLayerPosition(foregroundLayerId, 0.3, -6.2, 100)
    setLayerScale(foregroundLayerId, 1.5, 0.75)
    setLayerParallaxDepth(foregroundLayerId, -300.0)
    setLayerSpin(foregroundLayerId, 45.0)  -- 45 deg/sec
    -- setLayerBlink(foregroundLayerId, 1.0, 0.5, 0.1, 0.1)  -- 1s on, 0.5s off, 0.1s transitions
    setLayerWave(foregroundLayerId, 0.5, 2.0, 90.0, 0.1)  -- wavelength, speed, angle, amplitude
    -- setLayerColor(foregroundLayerId, 1.0, 0.5, 0.5, 1.0)  -- red tint
    -- setLayerColorCycle(foregroundLayerId, 1,0,0,1, 0,0,1,1, 2.0)  -- red to blue over 2s

    -- Set up the multi-light system
    setAmbientLight(0.75, 0.75, 0.75)

    -- Enable Box2D debug drawing
    b2EnableDebugDraw(true)

    -- Set gravity
    b2SetGravity(0, -10)

    -- Create static boundaries
    createBoundaries()

    -- Create water force field (with visual effect via water=true flag)
    local waterVertices = {
        -0.4, 0.0,  -- top left rim
        0.4, 0.0,   -- top right rim
        0.3, -0.5,  -- middle right
        0.2, -0.9,  -- bottom right
        -0.2, -0.9, -- bottom left
        -0.3, -0.5  -- middle left
    }
    -- Create force field with water=true to automatically set up water visuals
    waterField = createForceField(waterVertices, 0.0, 15.0, true)
    setWaterPercentage(waterField, 0.6)  -- 60% filled

    -- Add "water" type to the water force field body
    local waterBodyId = getForceFieldBodyId(waterField)
    if waterBodyId then
        b2AddBodyType(waterBodyId, "water")
    end

    createRadialForceField(0.9, 0.0, 0.5, -20.0, -15.0)

    -- Create test nodes to demonstrate the node system
    -- Circle node with script callback at left side
    local circleNodeId = createNode("circle_node_script", {x = -1.0, y = 0.5, radius = 0.2})

    -- Polygon node without script at right side (for position query test)
    local polyNodeVertices = {
        0.6, -0.5,
        0.8, -0.5,
        0.8, -0.3,
        0.6, -0.3
    }
    local polyNodeId = createNode("poly_test", {vertices = polyNodeVertices})
    local polyX, polyY = getNodePosition(polyNodeId)
    print("Polygon node created at position: (" .. polyX .. ", " .. polyY .. ")")

    -- Load objects
    table.insert(objects, loadObject("res/objects/lantern/lantern.lua", { x = -0.605, y = 0.7 }))
    table.insert(objects, loadObject("res/objects/lantern/lantern.lua", { x = -1.2, y = 0.7 }))
    table.insert(objects, loadObject("res/objects/lightsaber/lightsaber.lua", { x = 0.3, y = 0.2, colorR = 0.3, colorG = 0.7, colorB = 1.0 }))
    table.insert(objects, loadObject("res/objects/lightsaber/lightsaber.lua", { x = 0.5, y = 0.2, colorR = 1.0, colorG = 0.0, colorB = 0.0 }))
    table.insert(objects, loadObject("res/objects/destructible_box/destructible_box.lua", { x = 0.8, y = 0.5 }))
    table.insert(objects, loadObject("res/objects/destructible_box/destructible_box.lua", { x = 1.2, y = 0.5 }))
    table.insert(objects, loadObject("res/objects/rock/rock.lua", { x = 0.0, y = 0.5 }))
end

function createBoundaries()
    -- Create ground
    local groundId = b2CreateBody(B2_STATIC_BODY, 0, -0.8, 0)
    b2AddSegmentFixture(groundId, -1.5, 0, 1.5, 0, 0.3, 0.0)

    -- Create left wall
    local leftWallId = b2CreateBody(B2_STATIC_BODY, -1.5, 0, 0)
    b2AddSegmentFixture(leftWallId, 0, -1.0, 0, 10.0, 0.3, 0.0)

    -- Create right wall
    local rightWallId = b2CreateBody(B2_STATIC_BODY, 1.5, 0, 0)
    b2AddSegmentFixture(rightWallId, 0, -1.0, 0, 10.0, 0.3, 0.0)

    -- Create ceiling
    local ceilingId = b2CreateBody(B2_STATIC_BODY, 0, 10.0, 0)
    b2AddSegmentFixture(ceilingId, -1.5, 0, 1.5, 0, 0.3, 0.0)
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

    -- Update water percentage in a sinusoidal pattern
    waterTime = waterTime + deltaTime
    local waterPercent = 0.5 + 0.4 * math.sin(waterTime * 0.5)
    setWaterPercentage(waterField, waterPercent)

    -- Update water rotation (slowly rotate the water force field)
    waterRotationTime = waterRotationTime + deltaTime
    local waterRotation = waterRotationTime * 0.3  -- 0.3 radians per second (~17 degrees/sec)
    setWaterRotation(waterField, waterRotation)
end

function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    end
    if action == ACTION_RESET_PHYSICS then
        -- Reset camera
        setCameraOffset(0, 0)
        setCameraZoom(1.0)

        -- Reset abstracted objects
        for _, obj in ipairs(objects) do
            if obj.reset then
                obj.reset()
            end
        end
    end
    if action == ACTION_TOGGLE_DEBUG_DRAW then
        debugDrawEnabled = not debugDrawEnabled
        b2EnableDebugDraw(debugDrawEnabled)
    end
    if action == ACTION_TOGGLE_BLADE then
        -- Forward to all objects that support it
        for _, obj in ipairs(objects) do
            if obj.onAction then
                obj.onAction(action)
            end
        end
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
