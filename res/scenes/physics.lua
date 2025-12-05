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
    setLayerPosition(foregroundLayerId, 0.3, -3.2, 100)
    setLayerScale(foregroundLayerId, 1.5, 0.75)
    setLayerParallaxDepth(foregroundLayerId, -300.0)
    setLayerSpin(foregroundLayerId, 45.0)  -- 45 deg/sec
    -- setLayerBlink(foregroundLayerId, 1.0, 0.5, 0.1, 0.1)  -- 1s on, 0.5s off, 0.1s transitions
    setLayerWave(foregroundLayerId, 0.5, 2.0, 90.0, 0.1)  -- wavelength, speed, angle, amplitude
    -- setLayerColor(foregroundLayerId, 1.0, 0.5, 0.5, 1.0)  -- red tint
    -- setLayerColorCycle(foregroundLayerId, 1,0,0,1, 0,0,1,1, 2.0)  -- red to blue over 2s

    -- Set up the multi-light system
    clearLights()
    setAmbientLight(0.75, 0.75, 0.75)

    -- Enable Box2D debug drawing
    b2EnableDebugDraw(true)

    -- Set gravity
    b2SetGravity(0, -10)

    -- Create static boundaries
    createBoundaries()

    -- Create water force field (with visual effect)
    local waterVertices = {
        -0.3, -0.9,   -- bottom-left
         0.3, -0.9,   -- bottom-right
         0.3,  0.0,   -- top-right (surface)
        -0.3,  0.0    -- top-left (surface)
    }
    -- Create water with: vertices, forceX, forceY, alpha, rippleAmplitude, rippleSpeed
    -- Reduced amplitude to 0.015 for gentler waves
    waterField = createWaterForceField(waterVertices, 0.0, 15.0, 0.8, 0.015, 2.0)

    -- Create a visual layer for the water
    waterShaderId = loadTexturedShadersEx("res/shaders/water_vertex.spv", "res/shaders/water_fragment.spv", 1, 1)
    local waterTexId = loadTexture("res/textures/rock1.png")
    -- Layer width matches force field width (0.6), with extra height for wave peaks
    waterLayerId = createLayer(waterTexId, 0.6, waterShaderId)
    -- Position: center X at 0, center Y slightly higher to allow waves to extend above surface
    -- Water area: X from -0.3 to 0.3 (width 0.6), Y from -0.9 to 0.0 (height 0.9)
    -- Adding 0.05 above surface for wave peaks, so center Y = (-0.9 + 0.05) / 2 = -0.425
    setLayerPosition(waterLayerId, 0.0, -0.425, 0)
    -- Scale: width = 1.0 (layer is 0.6 wide), height = 1.583 (0.95 / 0.6 to cover -0.9 to +0.05)
    setLayerScale(waterLayerId, 1.0, 1.583)
    setLayerParallaxDepth(waterLayerId, -0.1)
    -- Enable local UV mode so the shader gets proper 0-1 coordinates
    setLayerUseLocalUV(waterLayerId, true)
    -- Set water shader parameters: alpha, rippleAmplitude, rippleSpeed, maxY(surface), minX, minY, maxX
    -- Surface Y is 0.0, reduced amplitude to 0.015
    setShaderParameters(waterShaderId, 0.8, 0.015, 2.0, 0.0, -0.3, -0.9, 0.3)

    createRadialForceField(0.9, 0.0, 0.5, -20.0, -15.0)

    -- Load objects
    table.insert(objects, loadObject("res/objects/lantern/lantern.lua", { x = -0.605, y = 0.7 }))
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
