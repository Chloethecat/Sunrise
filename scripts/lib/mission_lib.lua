-- Shared Sunrise mission-authoring helpers.
-- This module stays inside the restricted Mission Lua sandbox.

local lib = {}

lib.VARIABLE_CAPACITY = 512
lib.TIMER_CAPACITY = 32
lib.KEY_BYTE_CAPACITY = 64
lib.STRING_BYTE_CAPACITY = 128

function lib.one(value, name)
    assert(value ~= nil, "missing mission constant: " .. name)
    return value
end

function lib.list(...)
    local values = {...}
    for index = 1, select("#", ...) do
        assert(values[index] ~= nil, "mission list entry " .. index .. " is missing")
    end
    return values
end

local Scope = {}
Scope.__index = Scope

function Scope:key(name)
    return self.tag .. "." .. name
end

function Scope:variable(name)
    return self.state:variable(self:key(name))
end

function Scope:set_variable(name, value)
    return self.context:set_variable(self:key(name), value)
end

function Scope:clear_variable(name)
    return self.context:clear_variable(self:key(name))
end

function Scope:start_timer(name, milliseconds)
    return self.context:start_timer(self:key(name), milliseconds)
end

function Scope:cancel_timer(name)
    return self.context:cancel_timer(self:key(name))
end

function Scope:once(name)
    if self:variable(name) then
        return false
    end
    self:set_variable(name, true)
    return true
end

-- RequestKey.value is a decimal string, which is safe to keep in durable mission state.
function Scope:track_request(name, request)
    if request == nil or request.value == nil then
        self:clear_variable(name)
        return nil
    end
    self:set_variable(name, request.value)
    return request
end

function Scope:request_matches(name, request)
    local expected = self:variable(name)
    return expected ~= nil
        and request ~= nil
        and request.value ~= nil
        and request.value == expected
end

function Scope:clear_request(name)
    return self:clear_variable(name)
end

function lib.scope(context, state, tag)
    assert(type(tag) == "string" and #tag > 0, "a scope needs a tag")
    return setmetatable({context = context, state = state, tag = tag}, Scope)
end

function lib.timer_name(tag, elapsed)
    if type(elapsed) ~= "string" then
        return nil
    end
    local head = tag .. "."
    if string.sub(elapsed, 1, #head) ~= head then
        return nil
    end
    return string.sub(elapsed, #head + 1)
end

function lib.is_slot(context, event, slot)
    local target = context:slot(slot)
    return event.registry_key == target.registry_key
        and event.slot_type == target.slot_type
        and event.slot_index == target.slot_index
end

function lib.is_trigger(context, event, slot, volume)
    return lib.is_slot(context, event, slot)
        and event.volume_registry_key == volume.registry_key
        and event.volume_slot_type == volume.slot_type
        and event.volume_slot_index == volume.slot_index
end

function lib.place_all(context, squads, mode)
    for _, squad in ipairs(squads) do
        context:squad(squad):place{mode = mode}
    end
end

function lib.activate_scenes(context, scenes)
    for _, scene in ipairs(scenes) do
        context:scene(scene):activate{}
    end
end

function lib.play_idles(context, idles)
    for _, idle in ipairs(idles) do
        context:slot(idle.sensor):play_performance{state = idle.state}
    end
end

function lib.transition_all(context, slots, transition, snap)
    for _, slot in ipairs(slots) do
        context:slot(slot):transition{
            transition = transition,
            snap = snap == true,
        }
    end
end

function lib.set_objects_active(context, slots, active)
    for _, slot in ipairs(slots) do
        context:slot(slot):set_object_active{active = active == true}
    end
end

-- The Host takes one head row plus 63 more on one object run.
local OBJECT_RUN = 64
local SLOT_PREFIX = "slot/"
local TAG_FIRST = #SLOT_PREFIX + 1
local TAG_LAST = TAG_FIRST + 7

local function object_tag(slot)
    if type(slot) ~= "string" or string.sub(slot, 1, #SLOT_PREFIX) ~= SLOT_PREFIX then
        return slot
    end
    return string.sub(slot, TAG_FIRST, TAG_LAST)
end

local function group_by_tag(slots)
    local order, by_tag = {}, {}
    for _, slot in ipairs(slots) do
        local tag = object_tag(slot)
        local group = by_tag[tag]
        if group == nil then
            group = {}
            by_tag[tag] = group
            order[#order + 1] = group
        end
        group[#group + 1] = slot
    end
    return order
end

function lib.activate_objects(context, slots)
    for _, group in ipairs(group_by_tag(slots)) do
        local index = 1
        while index <= #group do
            local last = math.min(index + OBJECT_RUN - 1, #group)
            local with = {}
            for member = index + 1, last do
                with[#with + 1] = group[member]
            end
            context:slot(group[index]):set_object_active{active = true, with = with}
            index = last + 1
        end
    end
end

return lib
