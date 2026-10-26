#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>

namespace titanrefs {

namespace detail {
inline constexpr uint32_t CRC32C_POLYNOMIAL = 0x82F63B78u;

constexpr std::array<uint32_t, 256> MakeCrc32cTable() noexcept {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int b = 0; b < 8; ++b) {
            c = (c >> 1) ^ (CRC32C_POLYNOMIAL & -(c & 1));
        }
        table[i] = c;
    }
    return table;
}

inline constexpr auto CRC32C_TABLE = MakeCrc32cTable();
} // namespace detail

/**
 * @brief Clean-room Castagnoli CRC32C (Polynomial 0x82F63B78) Engine.
 * Conforms to RFC 3720 and ReFS v3 integrity stream specification.
 */
class Crc32c {
public:
    [[nodiscard]] static uint32_t Compute(std::span<const uint8_t> data, uint32_t seed = 0) noexcept {
        return Compute(data.data(), data.size(), seed);
    }

    [[nodiscard]] static uint32_t Compute(const void* data, size_t length, uint32_t seed = 0) noexcept {
        const auto* bytes = static_cast<const uint8_t*>(data);
        uint32_t crc = seed ^ 0xFFFFFFFFu;

        for (size_t i = 0; i < length; ++i) {
            uint8_t index = static_cast<uint8_t>(crc ^ bytes[i]);
            crc = (crc >> 8) ^ detail::CRC32C_TABLE[index];
        }

        return crc ^ 0xFFFFFFFFu;
    }
};

} // namespace titanrefs
