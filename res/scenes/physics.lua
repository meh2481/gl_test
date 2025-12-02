-- Physics demo scene
-- A demonstration scene with various physics objects

-- Scene-level state (non-object things)
objects = {}
debugDrawEnabled = true
forceFieldId = nil
radialForceFieldId = nil

-- Mouse drag state
mouseJointId = nil
draggedBodyId = nil

function init()
    -- Load the nebula background shader (z-index -2 = background)
    loadShaders("res/shaders/vertex.spv", "res/shaders/nebula_fragment.spv", -2)

    -- Load debug drawing shader (z-index 3, drawn on top)
    loadShaders("res/shaders/debug_vertex.spv", "res/shaders/debug_fragment.spv", 3)

    -- Set up the multi-light system
    clearLights()
    setAmbientLight(0.75, 0.75, 0.75)

    -- Enable Box2D debug drawing
    b2EnableDebugDraw(true)

    -- Set gravity
    b2SetGravity(0, -10)

    -- Create static boundaries
    createBoundaries()

    -- Create a force field that pushes objects upward
    local fieldVertices = {
        -0.3, -0.9,   -- bottom-left
         0.3, -0.9,   -- bottom-right
         0.3,  0.0,   -- top-right
        -0.3,  0.0    -- top-left
    }
    forceFieldId = createForceField(fieldVertices, 0.0, 15.0)

    -- Create a radial force field that repels objects from the center
    radialForceFieldId = createRadialForceField(0.9, 0.0, 0.5, -20.0, -15.0)

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
