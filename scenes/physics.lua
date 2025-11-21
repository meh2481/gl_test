-- Physics demo scene
bodies = {}
layers = {}
debugDrawEnabled = true

function init()
    -- Load the nebula background shader (z-index 0)
    loadShaders("vertex.spv", "nebula_fragment.spv", 0)

    -- Load Phong shaders with normal maps (z-index 1)
    loadTexturedShadersEx("phong_vertex.spv", "phong_fragment.spv", 1, "metalwall.png", "metalwall.norm.png")
    loadTexturedShadersEx("phong_vertex.spv", "phong_fragment.spv", 1, "rock.png", "rock.norm.png")
    
    -- Load toon shader (z-index 1) - uses same texture twice to get extended push constants
    loadTexturedShadersEx("toon_vertex.spv", "toon_fragment.spv", 1, "metalwall.png", "metalwall.png")
    
    -- Load regular textured shaders (no normal mapping) (z-index 1)
    loadTexturedShaders("sprite_vertex.spv", "sprite_fragment.spv", 1)
    
    -- Set shader parameters (shared by all extended shaders)
    -- Position (0.5, 0.5, 1.0) - light position for both Phong and Toon
    -- param3: Phong uses as ambient (0.3), Toon uses as levels (3.0) - compromise value 3.0
    -- param4: Phong uses as diffuse (0.7), Toon ignores
    -- param5: Phong uses as specular (0.8), Toon ignores
    -- param6: Phong uses as shininess (32.0), Toon ignores
    setShaderParameters(0.5, 0.5, 1.0, 3.0, 0.7, 0.8, 32.0)

    -- Load debug drawing shader (z-index 2, drawn on top)
    loadShaders("debug_vertex.spv", "debug_fragment.spv", 2)

    -- Load textures
    loadTexture("rock.png")
    loadTexture("rock.norm.png")
    loadTexture("metalwall.png")
    loadTexture("metalwall.norm.png")

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
        if i <= 2 then
            -- Phong shading with normal maps
            layerId = createLayer("metalwall.png", 0.2, 0.2, "metalwall.norm.png")
        elseif i == 3 then
            -- Toon shader (uses metalwall texture twice as a workaround for dual-texture pipeline)
            layerId = createLayer("metalwall.png", 0.2, 0.2, "metalwall.png")
        elseif i == 4 then
            -- Simple texture, no normal map
            layerId = createLayer("metalwall.png", 0.2, 0.2)
        else
            -- Show normal map as color
            layerId = createLayer("metalwall.norm.png", 0.2, 0.2)
        end
        attachLayerToBody(layerId, bodyId)
        table.insert(layers, layerId)
    end

    -- Create a dynamic circle with rock.png sprite using Phong with normal maps
    local circleId = b2CreateBody(B2_DYNAMIC_BODY, 0, 0.8, 0)
    b2AddCircleFixture(circleId, 0.15, 1.0, 0.3, 0.5)
    table.insert(bodies, circleId)

    -- Attach a sprite layer to the circle
    local layerId = createLayer("rock.png", 0.3, 0.3, "rock.norm.png")
    attachLayerToBody(layerId, circleId)
    table.insert(layers, layerId)

    print("Physics demo scene initialized with multiple shader types")
end

function update(deltaTime)
    -- Step the physics simulation (Box2D 3.x uses subStepCount instead of velocity/position iterations)
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
        -- Apply impulse to the circle
        if #bodies > 0 then
            local circleId = bodies[#bodies]
            local x, y = b2GetBodyPosition(circleId)
            b2ApplyForce(circleId, 0, 50, x, y)
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
            else
                -- Circle
                b2SetBodyPosition(bodyId, 0, 0.8)
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
end
