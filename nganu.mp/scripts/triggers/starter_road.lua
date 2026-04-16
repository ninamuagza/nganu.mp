local M = {}

function M.enter(playerid, object_index, quest)
    if quest.get_stage(playerid) ~= 1 then
        return
    end

    SendPlayerMessage(playerid, "[Quest] " .. (GetPlayerMapObjectProperty(playerid, object_index, "complete_text") or "Objective complete."))
    quest.set_stage(playerid, 2)
    quest.sync_objective(playerid)
end

return M
