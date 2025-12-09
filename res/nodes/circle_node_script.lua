-- Circle Node Script
-- Handles events for the circle node in the physics demo

local circleNodeScript = {}

-- Local state
circleNodeScript.counter = 0

-- Update function called every frame
function circleNodeScript.update(dt)
    -- Update logic here (currently empty)
end

-- Called when a body enters the node
function circleNodeScript.onEnter(bodyId, x, y)
    print("Circle node entered by body " .. bodyId .. " at position (" .. x .. ", " .. y .. ")")
    -- Additional logic can be added here
end

return circleNodeScript