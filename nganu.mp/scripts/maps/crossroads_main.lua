local map_objects = dofile("scripts/runtime/map_objects.lua")

local M = {}

local object_scripts = {
    portal_travel = function(playerid, object_index)
        map_objects.handle_portal_travel(playerid, object_index)
    end,
}

function M.OnGameModeInit()
    print("Map script loaded: crossroads_main")
end

function M.OnPlayerConnect(playerid)
    return nil
end

function M.OnPlayerEnterMap(playerid)
    local name = GetPlayerName(playerid)
    SendPlayerMessage(playerid, string.format("Welcome to the Crossroads, %s.", name))
    SendPlayerMessage(playerid, "Travelers pass through here. Use E near the gate marker to return.")
    SendPlayerMessage(playerid, map_objects.map_population_line(playerid))
    SetPlayerObjective(playerid, "Explore the Crossroads or return through the gate.")
    BroadcastMapMessage(GetPlayerMapId(playerid), string.format("%s arrived at the Crossroads.", name))
end

function M.OnPlayerLeaveMap(playerid)
    BroadcastMapMessage(GetPlayerMapId(playerid), string.format("%s left the Crossroads.", GetPlayerName(playerid)))
end

function M.OnPlayerDisconnect(playerid, reason)
    return nil
end

function M.OnPlayerNameChange(playerid)
    BroadcastMapMessage(GetPlayerMapId(playerid), string.format("%s updated their travel papers.", GetPlayerName(playerid)))
end

function M.OnPlayerText(playerid)
    print(string.format("[crossroads] %s: %s", GetPlayerName(playerid), GetLastPlayerText()))
end

function M.OnPlayerCommand(playerid)
    local text = GetLastPlayerText()

    if text == "/help" then
        SendPlayerMessage(playerid, "Commands: /help, /where")
        return
    end

    if text == "/where" then
        SendPlayerMessage(playerid, map_objects.where_line(playerid))
        return
    end

    SendPlayerMessage(playerid, "No quest board here. Try /where or head back through the gate.")
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
        map_objects.describe_prop(playerid, object_index, "The road splits in three directions.")
    end
end

function M.OnMapTriggerEnter(playerid, object_index)
    return nil
end

return M
