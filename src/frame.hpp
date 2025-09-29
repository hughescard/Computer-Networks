#ifndef FRAME_HPP
#define FRAME_HPP

#include <cstdint>
#include <cstddef>

enum class Type : uint8_t
{
    MSG = 1,
    FILE = 2,
    ACK = 3,
    HELLO = 4
};

#pragma pack(push, 1)
struct Header
{
    Type type;             // message type
    uint32_t msg_id;       // message identifier
    uint32_t seq;          // sequence number
    uint32_t total;        // total frames in message
    uint16_t payload_len;  // payload length in bytes
};
#pragma pack(pop)
static_assert(sizeof(Header) == 15, "Header must be exactly 15 bytes");// Ensure no padding

size_t serialize_header()
{
    // Implement serialization logic here
}

bool parse_header()
{

}

#endif // FRAME_HPP
