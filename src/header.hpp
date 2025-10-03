#pragma once
#define FRAME_HPP

#include <cstdint>
#include <cstddef>
#include "util/helpers.hpp"

namespace linkchat
{
    enum class Type : uint8_t
    {
        MSG = 1,
        FILE = 2,
        ACK = 3,
        HELLO = 4
    };

    enum class Off 
    { T=0, MID=1, SEQ=5, TOT=9, LEN=13 };


    #pragma pack(push, 1)
        struct Header
        {
            Type type;            // message type
            uint32_t msg_id;      // message identifier
            uint32_t seq;         // sequence number
            uint32_t total;       // total frames in message
            uint16_t payload_len; // payload length in bytes
        };
    #pragma pack(pop)
    static_assert(sizeof(Header) == 15, "Header must be exactly 15 bytes"); // Ensure no padding
    
    size_t serialize_header(const Header & h, uint8_t * buf, size_t buf_size);

    bool parse_header(const uint8_t * buf, size_t buf_size, Header & out);
 
}
