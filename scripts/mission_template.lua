-- Mission authoring template.
--
-- IMPORTANT:
-- This filename is intentionally generic, so Sunrise will not auto-attach it to an activity.
-- Copy it and rename the copy to the exact controller filename shown in Sunrise's Mission
-- Script HUD, then replace the placeholder generated constants.

local missions = require("missions")
local controller = require("lib.mission_controller")
local lib = require("lib.mission_lib")

local mission = require(assert(missions.YOUR_MISSION, "generated mission module is absent"))
local INITIAL_STATE = lib.one(mission.states.YOUR_INITIAL_STATE, "initial state")

local INTRO_TRIGGER = mission.Slot.YOUR_INTRO_TRIGGER
local INTRO_VOLUME = mission.TriggerVolume.YOUR_INTRO_TRIGGER
local EXIT_DOOR = mission.Slot.YOUR_EXIT_DOOR
local OBJECTIVE = mission.Slot.YOUR_OBJECTIVE

local ENEMY_SQUADS = lib.list(
    mission.Squad.YOUR_ENEMY_SQUAD
)

return controller.build{
    tag = "main",
    initial = "arrival",
    initial_state = INITIAL_STATE,

    stages = {
        arrival = {
            phase = 10,

            enter = function(context, scope)
                if not scope:once("built") then
                    return
                end

                context:select_state(INITIAL_STATE)
                context:slot(EXIT_DOOR):transition{
                    transition = context.sdk.device_transitions.close,
                    snap = true,
                }
                context:slot(INTRO_TRIGGER):fire_trigger()
            end,

            player_trigger = function(context, scope, event)
                if lib.is_trigger(context, event, INTRO_TRIGGER, INTRO_VOLUME) then
                    return "combat"
                end
            end,
        },

        combat = {
            phase = 20,

            enter = function(context, scope)
                context:slot(OBJECTIVE):reset_objectives()
                lib.place_all(context, ENEMY_SQUADS, context.sdk.squad_modes.replace)
            end,

            squad_state = function(context, scope, event)
                -- Replace this generic example with exact encounter squad matching.
                if event.alive_count == 0 and event.previous_alive_count > 0 then
                    return "exit"
                end
            end,
        },

        exit = {
            phase = 30,

            enter = function(context, scope)
                context:slot(EXIT_DOOR):transition{
                    transition = context.sdk.device_transitions.open,
                    snap = false,
                }
            end,
        },
    },
}
