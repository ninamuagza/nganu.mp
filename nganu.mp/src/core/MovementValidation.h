#pragma once

#include "core/Server.h"
#include <cstdint>

class MapData;

struct MovementValidationResult {
    bool accepted = false;
    float distance = 0.0f;
    float dtSeconds = 0.0f;
    float rawDtSeconds = 0.0f;
    bool walkable = false;
};

MovementValidationResult ValidateMovement(const Server::PlayerPosition& previousPosition,
                                          const Server::PlayerPosition& nextPosition,
                                          uint64_t currentMs,
                                          uint64_t lastMoveMs,
                                          const MapData& map);
