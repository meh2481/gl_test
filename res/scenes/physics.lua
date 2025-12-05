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
    local waterMinX = -0.3
    local waterMaxX = 0.3
    local waterMinY = -0.9
    local waterMaxY = 0.0
    local waveBuffer = 0.1  -- Extra height above surface for wave peaks

    local waterVertices = {
        waterMinX, waterMinY,   -- bottom-left
        waterMaxX, waterMinY,   -- bottom-right
        waterMaxX, waterMaxY,   -- top-right (surface)
        waterMinX, waterMaxY    -- top-left (surface)
    }
    -- Create water with: vertices, forceX, forceY, alpha, rippleAmplitude, rippleSpeed
    -- Amplitude 0.025 for medium-height waves
    waterField = createWaterForceField(waterVertices, 0.0, 15.0, 0.75, 0.025, 2.0)

    -- Enable render-to-texture reflection at the water surface
    enableReflection(waterMaxY)

    -- Create a visual layer for the water using water shaders (supports splash ripples)
    waterShaderId = loadWaterShaders("res/shaders/water_vertex.spv", "res/shaders/water_fragment.spv", 2)
    local waterTexId = loadTexture("res/textures/rock1.png")

    -- Calculate layer dimensions - ensure full width coverage
    local waterWidth = (waterMaxX - waterMinX) * 2
    local waterHeight = waterMaxY - waterMinY
    local totalHeight = waterHeight + waveBuffer
    local centerX = (waterMinX + waterMaxX) / 2
    local centerY = (waterMinY + waterMaxY + waveBuffer) / 2

    -- Get the reflection texture ID
    local reflectionTexId = getReflectionTextureId()

    -- Create layer with dual textures: water texture + reflection texture
    if reflectionTexId then
        waterLayerId = createLayer(waterTexId, waterWidth, reflectionTexId, waterShaderId)
    else
        -- Fallback if reflection not available
        waterLayerId = createLayer(waterTexId, waterWidth, waterShaderId)
    end
    setLayerPosition(waterLayerId, centerX, centerY, 0)
    -- Scale height to cover the water area plus wave buffer
    setLayerScale(waterLayerId, 1.0, totalHeight / waterWidth)
    setLayerParallaxDepth(waterLayerId, -0.1)
    -- Enable local UV mode for shader coordinates
    setLayerUseLocalUV(waterLayerId, true)
    -- Set water shader parameters: alpha, rippleAmplitude, rippleSpeed, maxY(surface), minX, minY, maxX
    setShaderParameters(waterShaderId, 0.75, 0.025, 2.0, waterMaxY, waterMinX, waterMinY, waterMaxX)

    -- Associate the water shader with the water force field for splash ripples
    setWaterFieldShader(waterField, waterShaderId)

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
