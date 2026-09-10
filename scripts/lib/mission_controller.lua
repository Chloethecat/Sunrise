-- Reusable durable stage controller for Sunrise Mission Lua.
--
-- A stage can define:
--   phase = integer
--   enter(context, scope, event, previous_stage)
--   resume(context, scope)
--   leave(context, scope, event, next_stage)
--   <event_name>(context, scope, event)
--
-- Returning a stage name from an event callback transitions immediately.

local lib = require("lib.mission_lib")
local controller = {}

local STAGE_KEY = "stage"

local function checked_stage(spec, name)
    assert(type(name) == "string" and #name > 0, "mission stage name must be a non-empty string")
    local stage = spec.stages[name]
    assert(stage ~= nil, "unknown mission stage: " .. name)
    return stage
end

local function publish_phase(context, stage)
    if stage.phase ~= nil then
        assert(type(stage.phase) == "number", "stage.phase must be an integer")
        context:set_phase(stage.phase)
    end
end

local function transition(spec, context, state, event, next_name)
    local scope = lib.scope(context, state, spec.tag)
    local previous_name = scope:variable(STAGE_KEY)

    if previous_name == next_name then
        return false
    end

    local next_stage = checked_stage(spec, next_name)

    if previous_name ~= nil then
        local previous_stage = checked_stage(spec, previous_name)
        if previous_stage.leave ~= nil then
            previous_stage.leave(context, scope, event, next_name)
        end
    end

    scope:set_variable(STAGE_KEY, next_name)
    publish_phase(context, next_stage)

    if spec.on_transition ~= nil then
        spec.on_transition(context, scope, previous_name, next_name, event)
    end

    if next_stage.enter ~= nil then
        next_stage.enter(context, scope, event, previous_name)
    end
    return true
end

local function ensure_started(spec, context, state, event)
    local scope = lib.scope(context, state, spec.tag)
    local name = scope:variable(STAGE_KEY)

    if name == nil then
        transition(spec, context, state, event, spec.initial)
        name = scope:variable(STAGE_KEY)
    end

    return name, checked_stage(spec, name), scope
end

local function dispatch(spec, event_name, context, state, event)
    local name, stage, scope = ensure_started(spec, context, state, event)

    if spec.events ~= nil then
        local global = spec.events[event_name]
        if global ~= nil then
            local next_name = global(context, scope, event, name)
            if type(next_name) == "string" then
                transition(spec, context, state, event, next_name)
                return
            end
        end
    end

    local handler = stage[event_name]
    if handler ~= nil then
        local next_name = handler(context, scope, event)
        if type(next_name) == "string" then
            transition(spec, context, state, event, next_name)
        end
    end
end

function controller.build(spec)
    assert(type(spec) == "table", "mission controller requires a specification table")
    assert(type(spec.tag) == "string" and #spec.tag > 0, "mission controller requires tag")
    assert(type(spec.initial) == "string" and #spec.initial > 0, "mission controller requires initial")
    assert(type(spec.stages) == "table", "mission controller requires stages")
    checked_stage(spec, spec.initial)

    local program = {
        initial_state = spec.initial_state,
    }

    program.on_start = function(context, state)
        ensure_started(spec, context, state, nil)
        if spec.on_start ~= nil then
            spec.on_start(context, lib.scope(context, state, spec.tag))
        end
    end

    program.on_load = function(context, state)
        local name, stage, scope = ensure_started(spec, context, state, nil)
        publish_phase(context, stage)

        if stage.resume ~= nil then
            stage.resume(context, scope)
        end
        if spec.on_load ~= nil then
            spec.on_load(context, scope, name)
        end
    end

    local function hook(name)
        return function(context, state, event)
            dispatch(spec, name, context, state, event)
        end
    end

    program.on_event_sensor_sense_updated = hook("sensor_sense_updated")
    program.on_event_client_state_changed = hook("client_state_changed")
    program.on_event_client_message_received = hook("client_message_received")
    program.on_event_auth_state_committed = hook("auth_state_committed")
    program.on_event_auth_state_transport_staged = hook("auth_state_transport_staged")
    program.on_event_auth_state_canceled = hook("auth_state_canceled")
    program.on_event_incident_received = hook("incident_received")
    program.on_event_incident_queued = hook("incident_queued")
    program.on_event_incident_transport_staged = hook("incident_transport_staged")
    program.on_event_incident_canceled = hook("incident_canceled")
    program.on_event_incident_refused = hook("incident_refused")
    program.on_event_scriptable_override_committed = hook("scriptable_override_committed")
    program.on_event_scriptable_override_transport_staged = hook("scriptable_override_transport_staged")
    program.on_event_scriptable_override_canceled = hook("scriptable_override_canceled")
    program.on_event_operator_refused = hook("operator_refused")
    program.on_event_timer_elapsed = hook("timer_elapsed")
    program.on_event_effect_result = hook("effect_result")
    program.on_event_phase_entered = hook("phase_entered")
    program.on_event_trigger_entered = hook("trigger_entered")
    program.on_event_trigger_exited = hook("trigger_exited")
    program.on_event_squad_state = hook("squad_state")
    program.on_event_entity_spawned = hook("entity_spawned")
    program.on_event_entity_died = hook("entity_died")
    program.on_event_scene_finished = hook("scene_finished")
    program.on_event_objective_progress = hook("objective_progress")
    program.on_event_entity_slots_requested = hook("entity_slots_requested")
    program.on_event_session_joined = hook("session_joined")
    program.on_event_session_left = hook("session_left")
    program.on_event_player_trigger = hook("player_trigger")
    program.on_event_cinematic_started = hook("cinematic_started")
    program.on_event_cinematic_terminated = hook("cinematic_terminated")
    program.on_event_ghost_link_state = hook("ghost_link_state")
    program.on_event_actor_path_state = hook("actor_path_state")
    program.on_event_object_interacted = hook("object_interacted")
    program.on_event_cinematic_skip_requested = hook("cinematic_skip_requested")
    program.on_event_fireteam_state = hook("fireteam_state")
    program.on_event_object_state = hook("object_state")
    program.on_event_damage_state = hook("damage_state")

    return program
end

return controller
