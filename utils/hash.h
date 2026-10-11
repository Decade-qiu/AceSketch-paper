#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// MurmurHash3 x86_32 is in the public domain. Only its 32-bit variant is needed.
inline uint32_t murmur_hash32(const void* key, std::size_t len, uint32_t seed) {
    const auto* bytes = static_cast<const uint8_t*>(key);
    auto rotate = [](uint32_t value, int bits) {
        return (value << bits) | (value >> (32 - bits));
    };
    auto mix = [&](uint32_t value) {
        return rotate(value * 0xcc9e2d51u, 15) * 0x1b873593u;
    };

    uint32_t hash = seed;
    const std::size_t blocks = len / 4;
    for (std::size_t i = 0; i < blocks; ++i) {
        const auto* p = bytes + i * 4;
        uint32_t value = uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
        hash = rotate(hash ^ mix(value), 13) * 5 + 0xe6546b64u;
    }

    uint32_t tail = 0;
    for (std::size_t i = 0; i < len % 4; ++i) {
        tail |= uint32_t(bytes[blocks * 4 + i]) << (8 * i);
    }
    if (len % 4) hash ^= mix(tail);
    hash ^= static_cast<uint32_t>(len);
    hash ^= hash >> 16;
    hash *= 0x85ebca6bu;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35u;
    return hash ^ (hash >> 16);
}

inline uint32_t H(const std::string& value, uint32_t seed = 31) {
    return murmur_hash32(value.data(), value.size(), seed);
}

inline uint32_t H(uint32_t value, uint32_t seed = 31) {
    const uint8_t bytes[] = {uint8_t(value), uint8_t(value >> 8),
                             uint8_t(value >> 16), uint8_t(value >> 24)};
    return murmur_hash32(bytes, sizeof(bytes), seed);
}
