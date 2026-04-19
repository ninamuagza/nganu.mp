#include "core/ContentRevision.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace {
uint64_t fnv1a64(const std::string& payload, uint64_t seedOffset) {
    uint64_t hash = seedOffset;
    constexpr uint64_t kPrime = 1099511628211ull;
    for (unsigned char ch : payload) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= kPrime;
    }
    return hash;
}
}

std::string BuildDeterministicContentRevision(const std::string& payload) {
    constexpr uint64_t kOffsetA = 14695981039346656037ull;
    constexpr uint64_t kOffsetB = 1099511628211ull ^ 0xCBF29CE484222325ull;
    const uint64_t hashA = fnv1a64(payload, kOffsetA);
    const uint64_t hashB = fnv1a64(payload, kOffsetB);
    std::ostringstream out;
    out << "map-"
        << std::hex << std::setfill('0')
        << std::setw(16) << hashA
        << std::setw(16) << hashB;
    return out.str();
}
