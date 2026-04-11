-- Reusable button / checkbox widget.
-- Load via loadObject("res/nodes/button.lua", params) — each call creates an independent instance.
--
-- params:
--   texId        (integer) texture resource ID from loadTexture()
--   shaderId     (integer) pipeline ID from loadTexturedShaders() etc.
--   normId       (integer, optional) normal-map resource ID (0 if none)
--   x, y         (number) world-space centre position
--   size         (number) button diameter in world units (default 0.22)
--   action       (integer) Action constant to fire via fireAction() on click
--   isCheckbox   (bool)    if true, clicking toggles greyscale (off) / colour (on)
--   r, g, b      (number)  base colour tint 0..1 (default 1, 1, 1)

local Btn = {}

-- Per-instance state (fresh locals for every loadObject call)
local layer    = nil
local bx, by, bhalf
local bAction
local isCheckbox = false
local checked    = false  -- checkboxes start unchecked (greyscale)
local baseR, baseG, baseB = 1.0, 1.0, 1.0

local SCALE_NORMAL = 1.0
local SCALE_HOVER  = 1.12
local SCALE_PRESS  = 0.88
local curScale     = SCALE_NORMAL
local isHov        = false
local isPress      = false

local function inBounds(cx, cy)
    return math.abs(cx - bx) <= bhalf and math.abs(cy - by) <= bhalf
end

local function animScale(target, dur)
    animateLayerScale(layer, curScale, curScale, target, target, dur, INTERPOLATION_EASE_OUT)
    curScale = target
end

local function applyCheckboxColor()
    if checked then
        setLayerColor(layer, baseR, baseG, baseB, 1.0)
    else
        setLayerColor(layer, 0.35, 0.35, 0.35, 1.0)
    end
end

function Btn.create(params)
    bx          = params.x    or 0
    by          = params.y    or 0
    bhalf       = (params.size or 0.22) / 2.0
    bAction     = params.action
    isCheckbox  = params.isCheckbox or false
    checked     = false  -- checkboxes default to unchecked
    baseR       = params.r or 1.0
    baseG       = params.g or 1.0
    baseB       = params.b or 1.0
    isHov       = false
    isPress     = false
    curScale    = SCALE_NORMAL

    local texId    = params.texId
    local normId   = params.normId or 0
    local shaderId = params.shaderId
    local size     = params.size or 0.22

    if normId ~= 0 then
        layer = createLayer(texId, size, normId, shaderId)
    else
        layer = createLayer(texId, size, shaderId)
    end

    setLayerPosition(layer, bx, by)
    setLayerScale(layer, SCALE_NORMAL, SCALE_NORMAL)

    if isCheckbox then
        applyCheckboxColor()
    else
        setLayerColor(layer, baseR, baseG, baseB, 1.0)
    end
end

function Btn.update(dt)
    if layer == nil then return end
    local cx, cy = getCursorPosition()
    local hov = inBounds(cx, cy)
    if hov ~= isHov then
        isHov = hov
        if not isPress then
            if hov then
                animScale(SCALE_HOVER, 0.1)
            else
                animScale(SCALE_NORMAL, 0.1)
            end
        end
    end
end

function Btn.onAction(action)
    if layer == nil then return end

    if action == ACTION_DRAG_START then
        local cx, cy = getCursorPosition()
        if inBounds(cx, cy) then
            isPress = true
            stopLayerAnimations(layer, PROPERTY_LAYER_SCALE)
            animScale(SCALE_PRESS, 0.06)
        end
    elseif action == ACTION_DRAG_END then
        if isPress then
            isPress = false
            local cx, cy = getCursorPosition()
            local inB = inBounds(cx, cy)
            -- Sync isHov so that update() detects a change if the cursor moves away
            -- (e.g. on touch where the cursor is parked off-screen after release).
            -- Without this, a fast tap leaves isHov=false and update() never fires
            -- the scale-to-normal animation, leaving the button stuck at SCALE_HOVER.
            isHov = inB
            animScale(inB and SCALE_HOVER or SCALE_NORMAL, 0.1)
            if inB then
                if isCheckbox then
                    checked = not checked
                    applyCheckboxColor()
                end
                fireAction(bAction)
            end
        end
    end
end

function Btn.cleanup()
    if layer then
        destroyLayer(layer)
        layer = nil
    end
end

return Btn
