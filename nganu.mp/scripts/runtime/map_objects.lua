local M = {}

local function object_text(playerid, object_index, key, fallback)
    local value = GetPlayerMapObjectProperty(playerid, object_index, key)
    if value == nil or value == "" then
        return fallback
    end
    return value
end

function M.describe_prop(playerid, object_index, fallback_note)
    local title = object_text(playerid, object_index, "title", "Marker")
    local note = object_text(playerid, object_index, "description", fallback_note or "A weathered sign stands here.")
    SendPlayerMessage(playerid, "[" .. title .. "] " .. note)
end

function M.handle_portal_travel(playerid, object_index)
    local current_map_id = GetPlayerMapId(playerid)
    local destination_map_id = object_text(playerid, object_index, "target_map", current_map_id)
    local target_title = object_text(playerid, object_index, "target_title", object_text(playerid, object_index, "title", "portal"))
    local target_x = tonumber(GetPlayerMapObjectProperty(playerid, object_index, "target_x") or "")
    local target_y = tonumber(GetPlayerMapObjectProperty(playerid, object_index, "target_y") or "")

    if destination_map_id == current_map_id then
        if target_x == nil or target_y == nil then
            SendPlayerMessage(playerid, "That portal has no valid destination.")
            return false
        end
        return TeleportPlayer(playerid, target_x, target_y, "Travelled to " .. target_title .. ".")
    end

    if target_x ~= nil and target_y ~= nil then
        return TransferPlayerMap(playerid, destination_map_id, target_x, target_y)
    end
    return TransferPlayerMap(playerid, destination_map_id)
end

function M.map_population_line(playerid)
    local map_id = GetPlayerMapId(playerid)
    local map_count = GetMapPlayerCount(map_id)
    local total_count = GetPlayerCount()
    return string.format("Population: %d here, %d online.", map_count, total_count)
end

function M.where_line(playerid)
    local x, y = GetPlayerPosition(playerid)
    local map_id = GetPlayerMapId(playerid)
    local map_count = GetMapPlayerCount(map_id)
    local total_count = GetPlayerCount()
    return string.format("You are at %.0f, %.0f on %s. Population: %d here, %d online.", x, y, map_id, map_count, total_count)
end

return M
