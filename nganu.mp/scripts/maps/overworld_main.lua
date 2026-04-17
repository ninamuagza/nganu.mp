local starter_quest = dofile("scripts/quests/starter_quest.lua")
local npc_luna = dofile("scripts/npc/luna.lua")
local trigger_starter_road = dofile("scripts/triggers/starter_road.lua")
local map_objects = dofile("scripts/runtime/map_objects.lua")

local M = {}

local object_scripts = {
    npc_luna = function(playerid, object_index)
        npc_luna.interact(playerid, object_index, starter_quest)
    end,
    portal_travel = function(playerid, object_index)
        map_objects.handle_portal_travel(playerid, object_index)
    end,
}

local trigger_scripts = {
    trigger_starter_road = function(playerid, object_index)
        trigger_starter_road.enter(playerid, object_index, starter_quest)
    end,
}

function M.OnGameModeInit()
    print("Map script loaded: overworld_main")
end

function M.OnPlayerConnect(playerid)
    starter_quest.set_stage(playerid, 0)
    starter_quest.sync_objective(playerid)

    if GivePlayerMoney then
        GivePlayerMoney(playerid, 1000)
    end
end

function M.OnPlayerEnterMap(playerid)
    local name = GetPlayerName(playerid)
    SendPlayerMessage(playerid, string.format("Welcome to the frontier, %s.", name))
    SendPlayerMessage(playerid, "Greenfields and Crossroads now live on one overworld.")
    SendPlayerMessage(playerid, map_objects.map_population_line(playerid))
    BroadcastMapMessage(GetPlayerMapId(playerid), string.format("%s entered the overworld.", name))
    starter_quest.sync_objective(playerid)
end

function M.OnPlayerLeaveMap(playerid)
    BroadcastMapMessage(GetPlayerMapId(playerid), string.format("%s left the overworld.", GetPlayerName(playerid)))
end

function M.OnPlayerDisconnect(playerid, reason)
    starter_quest.clear(playerid)
end

function M.OnPlayerNameChange(playerid)
    BroadcastMapMessage(GetPlayerMapId(playerid), string.format("Player %d is now known as %s.", playerid, GetPlayerName(playerid)))
end

function M.OnPlayerText(playerid)
    print(string.format("[overworld] %s: %s", GetPlayerName(playerid), GetLastPlayerText()))
end

function M.OnPlayerCommand(playerid)
    local text = GetLastPlayerText()

    if text == "/help" then
        SendPlayerMessage(playerid, "Commands: /help, /where, /quest")
        return
    end

    if text == "/where" then
        SendPlayerMessage(playerid, map_objects.where_line(playerid))
        return
    end

    if text == "/quest" then
        SendPlayerMessage(playerid, "Quest status: " .. starter_quest.objective_for_stage(playerid))
        return
    end

    SendPlayerMessage(playerid, "Unknown command: " .. text)
end

function M.OnMapObjectInteract(playerid, object_index)
    local script_name = GetPlayerMapObjectProperty(playerid, object_index, "script")
    if script_name ~= nil and script_name ~= "" then
        local handler = object_scripts[script_name]
        if handler ~= nil then
            handler(playerid, object_index)
            return
        end
    end

    local kind = GetPlayerMapObjectKind(playerid, object_index)
    if kind == "prop" then
        map_objects.describe_prop(playerid, object_index, "A weathered sign stands here.")
    end
end

function M.OnMapTriggerEnter(playerid, object_index)
    local script_name = GetPlayerMapObjectProperty(playerid, object_index, "script")
    if script_name == nil or script_name == "" then
        return
    end

    local handler = trigger_scripts[script_name]
    if handler ~= nil then
        handler(playerid, object_index)
    end
end

return M
