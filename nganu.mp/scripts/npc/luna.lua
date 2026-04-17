local M = {}
local STARTER_REWARD_ITEM_ID = 7
local STARTER_REWARD_AMOUNT = 25

local function object_text(playerid, object_index, key, fallback)
    local value = GetPlayerMapObjectProperty(playerid, object_index, key)
    if value == nil or value == "" then
        return fallback
    end
    return value
end

function M.interact(playerid, object_index, quest)
    local stage = quest.get_stage(playerid)
    local npc_name = GetPlayerMapObjectProperty(playerid, object_index, "name") or "NPC"

    if stage == 0 then
        SendPlayerMessage(playerid, "[" .. npc_name .. "] " .. object_text(playerid, object_index, "intro1", "Welcome."))
        SendPlayerMessage(playerid, "[" .. npc_name .. "] " .. object_text(playerid, object_index, "intro2", "Head out to the marker."))
        quest.set_stage(playerid, 1)
        quest.sync_objective(playerid)
        return
    end

    if stage == 1 then
        SendPlayerMessage(playerid, "[" .. npc_name .. "] " .. object_text(playerid, object_index, "progress", "Keep moving."))
        return
    end

    if stage == 2 then
        SendPlayerMessage(playerid, "[" .. npc_name .. "] " .. object_text(playerid, object_index, "complete", "Good work."))
        if AddInventoryItem and CountInventoryItem then
            if AddInventoryItem(playerid, STARTER_REWARD_ITEM_ID, STARTER_REWARD_AMOUNT) then
                local total = CountInventoryItem(playerid, STARTER_REWARD_ITEM_ID)
                SendPlayerMessage(playerid,
                                  string.format("[%s] Take these %d gold coins. You now carry %d.",
                                                npc_name,
                                                STARTER_REWARD_AMOUNT,
                                                total))
            else
                SendPlayerMessage(playerid, "[" .. npc_name .. "] Your pack is full. Make room and talk to me again.")
                return
            end
        end
        quest.set_stage(playerid, 3)
        quest.sync_objective(playerid)
        return
    end

    SendPlayerMessage(playerid, "[" .. npc_name .. "] " .. object_text(playerid, object_index, "idle", "Stay sharp."))
end

return M
