-- Physics demo scene
bodies = {}

function init()
    -- Load the nebula background shader (z-index 0)
    loadShaders("vertex.spv", "nebula_fragment.spv", 0)

    -- Load debug drawing shader (z-index 1, drawn on top)
    loadShaders("debug_vertex.spv", "debug_fragment.spv", 1)

    -- Enable Box2D debug drawing
    b2EnableDebugDraw(true)

    -- Set gravity (x, y)
    b2SetGravity(0, -10)

    -- Create ground (static body)
    local groundId = b2CreateBody(B2_STATIC_BODY, 0, -0.8, 0)
    b2AddBoxFixture(groundId, 1.5, 0.1, 1.0, 0.3, 0.0)
    table.insert(bodies, groundId)

    -- Create some dynamic boxes
    for i = 1, 5 do
        local x = -0.5 + (i - 1) * 0.25
        local y = 0.5
        local bodyId = b2CreateBody(B2_DYNAMIC_BODY, x, y, 0)
        b2AddBoxFixture(bodyId, 0.1, 0.1, 1.0, 0.3, 0.3)
        table.insert(bodies, bodyId)
    end

    -- Create a dynamic circle
    local circleId = b2CreateBody(B2_DYNAMIC_BODY, 0, 0.8, 0)
    b2AddCircleFixture(circleId, 0.15, 1.0, 0.3, 0.5)
    table.insert(bodies, circleId)

    print("Physics demo scene initialized")
end

function update(deltaTime)
    -- Step the physics simulation (Box2D 3.x uses subStepCount instead of velocity/position iterations)
    b2Step(deltaTime, 4)
end

function cleanup()
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
            b2ApplyForce(circleId, 0, 500, x, y)
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
    end
end
