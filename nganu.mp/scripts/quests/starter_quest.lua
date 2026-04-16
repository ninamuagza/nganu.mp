local M = {}

local quest_state = {}

local QUEST_NOT_STARTED = 0
local QUEST_SCOUT_ROAD = 1
local QUEST_RETURN_TO_NPC = 2
local QUEST_DONE = 3

function M.get_stage(playerid)
    return quest_state[playerid] or QUEST_NOT_STARTED
end

function M.set_stage(playerid, stage)
    quest_state[playerid] = stage
end

function M.clear(playerid)
    quest_state[playerid] = nil
end

function M.objective_for_stage(playerid)
    local stage = M.get_stage(playerid)
    if stage == QUEST_NOT_STARTED then
        return "Talk to Luna."
    end
    if stage == QUEST_SCOUT_ROAD then
        return "Reach the road marker."
    end
    if stage == QUEST_RETURN_TO_NPC then
        return "Return to Luna."
    end
    return "No active objective."
end

function M.sync_objective(playerid)
    SetPlayerObjective(playerid, M.objective_for_stage(playerid))
end

return M
