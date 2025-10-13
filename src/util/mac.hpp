#pragma once
#include <cstdint>
#include <string>
#include <cstddef>

namespace linkchat {

    inline constexpr std::size_t kMacSize = 6;

    struct Mac final {
        std::uint8_t bytes[kMacSize];
    };

    bool operator==(const Mac& a, const Mac& b) noexcept;
    bool operator!=(const Mac& a, const Mac& b) noexcept;

    bool is_broadcast(const Mac& mac) noexcept;

    bool is_zero(const Mac& mac) noexcept;

    bool parse_mac(const std::string& text, Mac& out) noexcept;

    std::string mac_to_string(const Mac& mac) noexcept;

    std::string bytes_to_hex(const uint8_t byte) noexcept;
    
    bool is_hexVal(char c) noexcept;
}
