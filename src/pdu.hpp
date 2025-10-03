#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include "header.hpp"

namespace linkchat {

inline constexpr size_t kHeaderSize = 15;
inline constexpr size_t kCrcSize = 4;

// [Header(15)] [Payload(P)] [CRC32(4, BE)]
size_t build_pdu(const Header& h,
                 const uint8_t* payload, size_t payload_len,
                 uint8_t* out, size_t out_cap) noexcept;

bool parse_pdu(const uint8_t* buf, size_t n,
               Header& out_h,
               std::vector<uint8_t>& out_payload) noexcept;

}
