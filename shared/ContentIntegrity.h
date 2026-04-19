#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace Nganu {
namespace ContentIntegrity {

inline uint64_t Fnv1a64(const std::string& payload) {
    uint64_t hash = 14695981039346656037ull;
    constexpr uint64_t kPrime = 1099511628211ull;
    for (unsigned char ch : payload) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= kPrime;
    }
    return hash;
}

inline std::string Fnv1a64Hex(const std::string& payload) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << Fnv1a64(payload);
    return out.str();
}

} // namespace ContentIntegrity
} // namespace Nganu
