#include "core/MovementValidation.h"

#include "core/MapData.h"

#include <algorithm>
#include <cmath>

MovementValidationResult ValidateMovement(const Server::PlayerPosition& previousPosition,
                                          const Server::PlayerPosition& nextPosition,
                                          uint64_t currentMs,
                                          uint64_t lastMoveMs,
                                          const MapData& map) {
    MovementValidationResult result;
    result.rawDtSeconds = static_cast<float>(currentMs - lastMoveMs) / 1000.0f;
    result.dtSeconds = std::clamp(result.rawDtSeconds, 0.030f, 0.25f);

    const float dx = nextPosition.x - previousPosition.x;
    const float dy = nextPosition.y - previousPosition.y;
    result.distance = std::sqrt((dx * dx) + (dy * dy));
    constexpr float kMaxMoveSpeed = 260.0f;
    constexpr float kMoveSlack = 42.0f;
    const bool speedValid = result.distance <= (kMaxMoveSpeed * result.dtSeconds) + kMoveSlack;
    result.walkable = map.isWalkable(nextPosition.x, nextPosition.y, 15.0f);
    result.accepted = speedValid && result.walkable;
    return result;
}
