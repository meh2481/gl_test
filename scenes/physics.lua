-- Physics demo scene
bodies = {}
joints = {}
layers = {}
debugDrawEnabled = true
chainLinks = {}
lightBody = nil
circleBody = nil
chainAnchor = nil
chainStartX = -0.605
chainStartY = 0.7
chainLightZ = 0.25
chainLinkHeight = 0.04
chainLength = 10
CHAIN_OFFSET = 0.0035

-- Mouse drag state
mouseJointId = nil
draggedBodyId = nil

function init()
    -- Load the nebula background shader (z-index 0)
    loadShaders("vertex.spv", "nebula_fragment.spv", 0)

    -- Load Phong shaders (z-index 1, 2 textures)
    phongShaderId = loadTexturedShadersEx("phong_vertex.spv", "phong_fragment.spv", 1, 2)

    -- Load toon shader (z-index 1, 1 texture - no normal map)
    toonShaderId = loadTexturedShadersEx("toon_vertex.spv", "toon_fragment.spv", 1, 1)

    -- Load regular textured shaders (no normal mapping) (z-index 1, 1 texture)
    simpleTexShaderId = loadTexturedShadersEx("sprite_vertex.spv", "sprite_fragment.spv", 1, 1)

    -- Load additive blending shader for bloom effect (z-index 2, below debug, 1 texture)
    bloomShaderId = loadTexturedShadersAdditive("sprite_vertex.spv", "sprite_fragment.spv", 2, 1)

    -- Set shader parameters for Phong shader
    -- Position (0.5, 0.5, chainLightZ) - light position
    -- ambient: 0.3, diffuse: 0.7, specular: 0.8, shininess: 32.0
    setShaderParameters(phongShaderId, 0.5, 0.5, chainLightZ, 0.3, 0.7, 0.8, 32.0)

    -- Set shader parameters for Toon shader (only 4 parameters needed)
    -- Position (0.5, 0.5, chainLightZ) - light position
    -- levels: 3.0 (3 cel-shading levels)
    setShaderParameters(toonShaderId, 0.5, 0.5, chainLightZ, 3.0)

    -- Load debug drawing shader (z-index 3, drawn on top)
    loadShaders("debug_vertex.spv", "debug_fragment.spv", 3)

    rockTexId = loadTexture("rock.png")
    rockNormId = loadTexture("rock.norm.png")
    metalwallTexId = loadTexture("metalwall.png")
    metalwallNormId = loadTexture("metalwall.norm.png")
    lanternTexId = loadTexture("lantern.png")
    lanternNormId = loadTexture("lantern.norm.png")
    chainTexId = loadTexture("chain.png")
    chainNormId = loadTexture("chain.norm.png")
    bloomTexId = loadTexture("bloom.png")

    -- Enable Box2D debug drawing
    b2EnableDebugDraw(true)

    -- Set gravity (x, y)
    b2SetGravity(0, -10)

    -- Create ground (static body)
    local groundId = b2CreateBody(B2_STATIC_BODY, 0, -0.8, 0)
    b2AddBoxFixture(groundId, 1.5, 0.1, 1.0, 0.3, 0.0)
    table.insert(bodies, groundId)

    -- Create 5 dynamic boxes with different rendering:
    -- Box 1 & 2: Phong with normal maps (metalwall)
    -- Box 3: Toon shader (metalwall)
    -- Box 4: Simple texture, no normal (metalwall)
    -- Box 5: Display normal map as color (metalwall.norm)
    for i = 1, 5 do
        local x = -0.5 + (i - 1) * 0.25
        local y = 0.5
        local bodyId = b2CreateBody(B2_DYNAMIC_BODY, x, y, 0)
        b2AddBoxFixture(bodyId, 0.1, 0.1, 1.0, 0.3, 0.3)
        table.insert(bodies, bodyId)

        local layerId
        if i <= 1 then
            layerId = createLayer(metalwallTexId, 0.2, metalwallNormId, phongShaderId)
        elseif i <= 2 then
            -- Phong shading with normal maps
            layerId = createLayer(metalwallTexId, 0.2, metalwallNormId, phongShaderId)
        elseif i == 3 then
            -- Toon shader (single texture, no normal map)
            layerId = createLayer(metalwallTexId, 0.2, toonShaderId)
        elseif i == 4 then
            -- Simple texture, no normal map
            layerId = createLayer(metalwallTexId, 0.2, simpleTexShaderId)
        else
            -- Show normal map as color
            layerId = createLayer(metalwallNormId, 0.2, simpleTexShaderId)
        end
        attachLayerToBody(layerId, bodyId)
        table.insert(layers, layerId)
    end

    -- Create a rectangle box using chain graphic with proper aspect ratio
    local chainWidth, chainHeight = getTextureDimensions(chainTexId)
    local chainAspectRatio = chainWidth / chainHeight
    local boxHeight = 0.2  -- Fixed height
    local boxWidth = boxHeight * chainAspectRatio  -- Width based on aspect ratio
    local chainBoxBody = b2CreateBody(B2_DYNAMIC_BODY, -0.7, 0.3, 0)
    b2AddBoxFixture(chainBoxBody, boxWidth / 2, boxHeight / 2, 1.0, 0.3, 0.3)
    table.insert(bodies, chainBoxBody)

    -- Attach chain sprite layer to the box
    local chainBoxLayerId = createLayer(chainTexId, boxHeight, chainNormId, phongShaderId)
    attachLayerToBody(chainBoxLayerId, chainBoxBody)
    table.insert(layers, chainBoxLayerId)

    -- Create a dynamic circle with rock.png sprite using Phong with normal maps
    circleBody = b2CreateBody(B2_DYNAMIC_BODY, 0, 0.8, 0)
    b2AddCircleFixture(circleBody, 0.15, 1.0, 0.3, 0.5)
    table.insert(bodies, circleBody)

    -- Attach a sprite layer to the circle
    local layerId = createLayer(rockTexId, 0.3, rockNormId, phongShaderId)
    attachLayerToBody(layerId, circleBody)
    table.insert(layers, layerId)

    -- Create a dynamic polygon with lantern.png sprite using Phong with normal maps
    circleBody2 = b2CreateBody(B2_DYNAMIC_BODY, 0.5, 0.8, 0)
    -- Lantern-shaped polygon: narrower at top, wider at bottom
    local lanternVerts = {
        -0.06, -0.146,  -- bottom left
         0.06, -0.146,  -- bottom right
         0.085,  -0.1,   -- middle right
         0.067,  0.045,  -- top right
         0.0,  0.15,    -- top center
        -0.067,  0.045,  -- top left
        -0.085,  -0.1    -- middle left
    }
    b2AddPolygonFixture(circleBody2, lanternVerts, 1.0, 0.3, 0.5)
    table.insert(bodies, circleBody2)

    -- Attach a sprite layer to the polygon body
    local layerId = createLayer(lanternTexId, 0.3, lanternNormId, phongShaderId)
    attachLayerToBody(layerId, circleBody2)
    table.insert(layers, layerId)

    -- Create a chain with multiple links
    local chainStartX = chainStartX
    local chainStartY = chainStartY
    local linkHeight = chainLinkHeight

    -- Create anchor point (static body at the top)
    chainAnchor = b2CreateBody(B2_STATIC_BODY, chainStartX, chainStartY, 0)
    b2AddCircleFixture(chainAnchor, 0.02, 1.0, 0.3, 0.0)
    table.insert(bodies, chainAnchor)

    local prevBodyId = chainAnchor

    -- Create chain links
    for i = 1, chainLength do
        local linkY = chainStartY - i * linkHeight
        local linkId = b2CreateBody(B2_DYNAMIC_BODY, chainStartX, linkY, 0)
        b2AddBoxFixture(linkId, 0.01, linkHeight / 2, 0.5, 0.3, 0.0)
        table.insert(bodies, linkId)
        table.insert(chainLinks, linkId)

        -- Attach sprite layer to chain link
        local layerId = createLayer(chainTexId, linkHeight, chainNormId, phongShaderId)
        attachLayerToBody(layerId, linkId)
        table.insert(layers, layerId)

        -- Create revolute joint to connect to previous link/anchor
        local jointId = b2CreateRevoluteJoint(
            prevBodyId,
            linkId,
            0.0, -linkHeight / 2 + CHAIN_OFFSET,  -- anchor on previous body (bottom)
            0.0, linkHeight / 2 - CHAIN_OFFSET,    -- anchor on current body (top)
            false, 0.0, 0.0         -- no angle limits
        )
        table.insert(joints, jointId)

        prevBodyId = linkId
    end

    -- Create light body at the end of the chain (a small dynamic polygon)
    lightBody = b2CreateBody(B2_DYNAMIC_BODY, chainStartX, chainStartY - (chainLength + 0.5) * linkHeight, 0)
    -- Small lantern-shaped polygon for the chain light
    local smallLanternVerts = {
        0, -0.045,  -- bottom center
        0.03,  -0.03,   -- middle right
        0.025,  0.02,  -- top right
        0.0,  0.045,    -- top center
        -0.025,  0.02,  -- top left
        -0.03,  -0.03    -- middle left
    }
    b2AddPolygonFixture(lightBody, smallLanternVerts, 0.2, 0.3, 0.3)
    table.insert(bodies, lightBody)

    -- Attach lantern sprite to the light body
    local layerId = createLayer(lanternTexId, 0.1, lanternNormId, phongShaderId)
    attachLayerToBody(layerId, lightBody)
    setLayerOffset(layerId, 0, 0.002)  -- Offset the lantern graphic slightly below the light body center
    table.insert(layers, layerId)

    -- Add bloom effect to swinging chain light
    local bloomLayerId = createLayer(bloomTexId, 1.5, bloomShaderId)
    attachLayerToBody(bloomLayerId, lightBody)
    table.insert(layers, bloomLayerId)

    -- Connect light to the last chain link
    local lightJointId = b2CreateRevoluteJoint(
        prevBodyId,
        lightBody,
        0.0, -linkHeight / 2 + CHAIN_OFFSET,  -- anchor on last chain link (bottom)
        0.0, 0.05 - CHAIN_OFFSET,               -- anchor on light body (top)
        false, 0.0, 0.0
    )
    table.insert(joints, lightJointId)

    print("Physics demo scene initialized with multiple shader types")
end

function update(deltaTime)
    -- Step the physics simulation (Box2D 3.x uses subStepCount instead of velocity/position iterations)
    b2Step(deltaTime, 4)

    -- Update mouse joint target if dragging
    if mouseJointId then
        local cursorX, cursorY = getCursorPosition()
        b2UpdateMouseJointTarget(mouseJointId, cursorX, cursorY)
        b2SetBodyAwake(draggedBodyId, true)
    end

    -- Update light position based on the light body at the end of the chain
    if lightBody then
        local lightX, lightY = b2GetBodyPosition(lightBody)
        lightX = lightX - 0.1 -- idk why this is slightly off
        -- Update shader parameters with new light position
        setShaderParameters(phongShaderId, lightX, lightY, chainLightZ, 0.3, 0.7, 0.8, 32.0)
        setShaderParameters(toonShaderId, lightX, lightY, chainLightZ, 3.0)
    end
end

function cleanup()
    -- Destroy mouse joint if active
    if mouseJointId then
        b2DestroyMouseJoint(mouseJointId)
        mouseJointId = nil
        draggedBodyId = nil
    end

    -- Destroy all scene layers
    for i, layerId in ipairs(layers) do
        destroyLayer(layerId)
    end
    layers = {}

    -- Destroy all joints
    for i, jointId in ipairs(joints) do
        b2DestroyJoint(jointId)
    end
    joints = {}

    -- Destroy all physics bodies
    for i, bodyId in ipairs(bodies) do
        b2DestroyBody(bodyId)
    end
    bodies = {}
    chainLinks = {}
    lightBody = nil
    circleBody = nil
    chainAnchor = nil
    -- Disable debug drawing
    b2EnableDebugDraw(false)
    print("Physics demo scene cleaned up")
end

-- Handle actions
function onAction(action)
    if action == ACTION_EXIT then
        popScene()
    end
    if action == ACTION_APPLY_FORCE then
        -- Apply impulse to the circle (rock)
        if circleBody then
            local x, y = b2GetBodyPosition(circleBody)
            b2ApplyForce(circleBody, 0, 50, x, y)
            -- Trigger controller vibration: medium intensity for 200ms
            vibrate(0.5, 0.5, 200)
        end
    end
    if action == ACTION_RESET_PHYSICS then
        -- Reset all bodies
        for i, bodyId in ipairs(bodies) do
            if i == 1 then
                -- Ground - keep it in place
                b2SetBodyPosition(bodyId, 0, -0.8)
                b2SetBodyAngle(bodyId, 0)
            elseif i <= 6 then
                -- Boxes
                local x = -0.5 + (i - 2) * 0.25
                b2SetBodyPosition(bodyId, x, 0.5)
                b2SetBodyAngle(bodyId, 0)
                b2SetBodyLinearVelocity(bodyId, 0, 0)
                b2SetBodyAngularVelocity(bodyId, 0)
                b2SetBodyAwake(bodyId, true)
            elseif i == 7 then
                -- Chain box
                b2SetBodyPosition(bodyId, -0.7, 0.3)
                b2SetBodyAngle(bodyId, 0)
                b2SetBodyLinearVelocity(bodyId, 0, 0)
                b2SetBodyAngularVelocity(bodyId, 0)
                b2SetBodyAwake(bodyId, true)
            elseif i == 8 then
                -- Circle (rock)
                b2SetBodyPosition(bodyId, 0, 0.8)
                b2SetBodyAngle(bodyId, 0)
                b2SetBodyLinearVelocity(bodyId, 0, 0)
                b2SetBodyAngularVelocity(bodyId, 0)
                b2SetBodyAwake(bodyId, true)
            elseif i == 9 then
                -- Lantern circle
                b2SetBodyPosition(bodyId, 0.5, 0.8)
                b2SetBodyAngle(bodyId, 0)
                b2SetBodyLinearVelocity(bodyId, 0, 0)
                b2SetBodyAngularVelocity(bodyId, 0)
                b2SetBodyAwake(bodyId, true)
            elseif i == 10 then
                -- Chain anchor - keep it in place (static body)
                b2SetBodyPosition(bodyId, chainStartX, chainStartY)
                b2SetBodyAngle(bodyId, 0)
            elseif i <= 10 + chainLength then
                -- Chain links
                local linkIndex = i - 10
                local linkY = chainStartY - linkIndex * chainLinkHeight
                b2SetBodyPosition(bodyId, chainStartX, linkY)
                b2SetBodyAngle(bodyId, 0)
                b2SetBodyLinearVelocity(bodyId, 0, 0)
                b2SetBodyAngularVelocity(bodyId, 0)
                b2SetBodyAwake(bodyId, true)
            else
                -- Light body at the end of chain
                local lightY = chainStartY - (chainLength + 0.5) * chainLinkHeight
                b2SetBodyPosition(bodyId, chainStartX, lightY)
                b2SetBodyAngle(bodyId, 0)
                b2SetBodyLinearVelocity(bodyId, 0, 0)
                b2SetBodyAngularVelocity(bodyId, 0)
                b2SetBodyAwake(bodyId, true)
            end
        end
        -- Trigger controller vibration: light intensity for 150ms
        vibrate(0.3, 0.3, 150)
    end
    if action == ACTION_TOGGLE_DEBUG_DRAW then
        print("Toggling debug draw")
        debugDrawEnabled = not debugDrawEnabled
        b2EnableDebugDraw(debugDrawEnabled)
    end
    if action == ACTION_DRAG_START then
        -- Start dragging: find body at cursor position and create mouse joint
        local cursorX, cursorY = getCursorPosition()
        local bodyId = b2QueryBodyAtPoint(cursorX, cursorY)
        if bodyId >= 0 then
            draggedBodyId = bodyId
            mouseJointId = b2CreateMouseJoint(bodyId, cursorX, cursorY, 1000.0)
        end
    end
    if action == ACTION_DRAG_END then
        -- End dragging: destroy mouse joint
        if mouseJointId then
            b2DestroyMouseJoint(mouseJointId)
            mouseJointId = nil
            draggedBodyId = nil
        end
    end
end
