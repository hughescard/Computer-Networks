#pragma once
#define FRAME_HPP

#include <cstdint>
#include <cstddef>
#include "util/helpers.hpp"
#include "util/structs.hpp"

namespace linkchat
{
    #pragma pack(push, 1)
        struct Header
        {
            Type type;                  // message type
            std::uint32_t msg_id;      // message identifier
            std::uint32_t seq;         // sequence number
            std::uint32_t total;       // total frames in message
            std::uint16_t payload_len; // payload length in bytes
        };
    #pragma pack(pop)
    static_assert(sizeof(Header) == 15, "Header must be exactly 15 bytes"); // Ensure no padding
    
    size_t serialize_header(const Header & h, std::uint8_t * buf, std::size_t buf_size)noexcept;

    bool parse_header(const std::uint8_t * buf, std::size_t buf_size, Header & out)noexcept;
 
}
