#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include "header.hpp"
#include "util/structs.hpp"

namespace linkchat
{

    inline constexpr std::size_t kHeaderSize = 15;
    inline constexpr std::size_t kCrcSize = 4;
    inline constexpr std::size_t kAckPayloadSize = 8;

    // [Header(15)] [Payload(P)] [CRC32(4, BE)]
    size_t build_pdu(const Header &h,
                     const std::uint8_t *payload, std::size_t payload_len,
                     std::uint8_t *out, size_t out_cap) noexcept;

    bool parse_pdu(const std::uint8_t *buf, std::size_t n,
                   Header &out_h,
                   std::vector<uint8_t> &out_payload) noexcept;

    struct AckFields
    {
        // Ack structure: type=ACK, seq=0, total=0, payload_len=8 (BE), CRC32(payload)
        // Payload body
        std::uint32_t msg_id;         // id from message that is being acknowledged
        std::uint32_t highest_seq_ok; // biggest seq of consecutive PDU's received
    };

    bool is_ack_header(const Header &h) noexcept;

    std::vector<uint8_t> create_ack(const AckFields &ack) noexcept;

    bool try_parse_ack(const std::uint8_t *pdu, std::size_t pdu_size, AckFields &out) noexcept;

    [[nodiscard]] inline constexpr std::size_t mtu_payload(std::uint16_t mtu) noexcept
    {
        if(mtu>kHeaderSize + kCrcSize)
            return mtu-kHeaderSize-kCrcSize;
        else 
            return 0;
    }

    //create pdu from message stored in buffer
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> chunkify_from_buffer(const std::uint8_t *data,
                                                                                std::size_t n,
                                                                                std::uint32_t msg_id,
                                                                                Type msg_type,
                                                                                std::uint16_t mtu = 1500);

    //create pdu from message stored in vector 
    [[nodiscard]] std::vector<std::vector<std::uint8_t>>
    chunkify_from_vector(const std::vector<std::uint8_t> &msg,
                            std::uint32_t msg_id,
                            Type msg_type,
                            std::uint16_t mtu = 1500);
}
