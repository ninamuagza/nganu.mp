local loaded_modules = {}

local function load_map_module(script_name)
    local key = script_name
    if key == nil or key == "" then
        key = "overworld_main"
    end

    if loaded_modules[key] == nil then
        loaded_modules[key] = dofile("scripts/maps/" .. key .. ".lua")
    end
    return loaded_modules[key]
end

local function module_for_player(playerid)
    return load_map_module(GetPlayerMapProperty(playerid, "map_script"))
end

local function call_player(name, playerid, ...)
    local module = module_for_player(playerid)
    local fn = module[name]
    if fn ~= nil then
        return fn(playerid, ...)
    end
    return nil
end

function OnGameModeInit()
    load_map_module(GetMapProperty("map_script"))
    return nil
end

function OnGameModeExit()
    for _, module in pairs(loaded_modules) do
        local fn = module["OnGameModeExit"]
        if fn ~= nil then
            fn()
        end
    end
    return nil
end

function OnGameModeUpdate(tick)
    for _, module in pairs(loaded_modules) do
        local fn = module["OnGameModeUpdate"]
        if fn ~= nil then
            fn(tick)
        end
    end
    return nil
end

function OnPlayerConnect(playerid)
    return call_player("OnPlayerConnect", playerid)
end

function OnPlayerEnterMap(playerid)
    return call_player("OnPlayerEnterMap", playerid)
end

function OnPlayerLeaveMap(playerid)
    return call_player("OnPlayerLeaveMap", playerid)
end

function OnPlayerDisconnect(playerid, reason)
    return call_player("OnPlayerDisconnect", playerid, reason)
end

function OnPlayerNameChange(playerid)
    return call_player("OnPlayerNameChange", playerid)
end

function OnPlayerText(playerid)
    return call_player("OnPlayerText", playerid)
end

function OnPlayerCommand(playerid)
    return call_player("OnPlayerCommand", playerid)
end

function OnMapObjectInteract(playerid, object_index)
    return call_player("OnMapObjectInteract", playerid, object_index)
end

function OnMapTriggerEnter(playerid, object_index)
    return call_player("OnMapTriggerEnter", playerid, object_index)
end
