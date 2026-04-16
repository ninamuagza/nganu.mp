#pragma once

#include "shared/Protocol.h"

namespace Packet {

inline PacketOpcode readOpcode(const void* data, size_t len) {
    return Protocol::readOpcode(data, len);
}

inline const uint8_t* payload(const void* data) {
    return Protocol::payload(data);
}

inline size_t payloadLen(size_t totalLen) {
    return Protocol::payloadLen(totalLen);
}

} /* namespace Packet */
