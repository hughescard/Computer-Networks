#pragma once
#include <cstdint>
#include <cstddef>

namespace linkchat
{

    inline constexpr size_t kHeaderSize = 15;
    inline constexpr size_t kCrcSize = 4;
    
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

    uint8_t type_to_uint8(Type t);
    bool uint8_to_type(uint8_t t, Type &out);
    void uint32_to_BE(uint32_t val, uint8_t *buf, int index);
    void uint16_to_BE(uint16_t val, uint8_t *buf, int index);
    uint32_t BE_to_uint32(const uint8_t *buf, int index);
    uint16_t BE_to_uint16(const uint8_t *buf, int index);
}
