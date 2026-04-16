local M = {}

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
        local x, y = GetPlayerPosition(playerid)
        SendPlayerMessage(playerid, string.format("You are at %.0f, %.0f in %s", x, y, GetPlayerMapId(playerid)))
        return
    end

    SendPlayerMessage(playerid, "No quest board here. Try /where or head back through the gate.")
end

function M.OnMapObjectInteract(playerid, object_index)
    local kind = GetPlayerMapObjectKind(playerid, object_index)
    if kind == "prop" then
        local title = GetPlayerMapObjectProperty(playerid, object_index, "title") or "Marker"
        local note = GetPlayerMapObjectProperty(playerid, object_index, "description") or "The road splits in three directions."
        SendPlayerMessage(playerid, "[" .. title .. "] " .. note)
    end
end

function M.OnMapTriggerEnter(playerid, object_index)
    return nil
end

return M
