#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <functional>
#include "header.hpp"
#include "pdu.hpp"
#include "util/structs.hpp"

namespace linkchat
{

    struct RxChunkEvent
    {
        Type type;                    
        std::uint32_t msg_id;         
        std::uint32_t seq;   
        std::uint32_t total;          
        std::size_t payload_len;      
        bool accepted;                
        bool duplicate;               
        bool completed;               
        std::uint32_t highest_seq_ok; 
    };

    using EmitAckFn = std::function<void(const AckFields &)>;

    class Reassembly
    {
    public:
        explicit Reassembly(EmitAckFn emit_ack);
       
        RxChunkEvent feed_pdu(const std::uint8_t *pdu, std::size_t pdu_size) noexcept;

        bool is_complete(std::uint32_t msg_id) const noexcept;

        bool extract_message(std::uint32_t msg_id, std::vector<std::uint8_t> &out) noexcept;

        void clear() noexcept;

    private:
        struct MsgState
        {
            Type type;
            std::uint32_t total;                           
            std::vector<std::vector<std::uint8_t>> chunks; 
            std::vector<std::uint8_t> received;            
            std::int32_t prefix;                           
            std::size_t bytes_accum;                       
        };

        std::unordered_map<std::uint32_t, MsgState> msgs_; 
        EmitAckFn emit_ack_;
    };
}
