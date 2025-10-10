#include "reassembly.hpp"
#include "util/helpers.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstring> 
#include <algorithm> 
#include <utility>
using namespace std;

namespace linkchat
{
    Reassembly::Reassembly(EmitAckFn emit_ack): msgs_(), emit_ack_(move(emit_ack))
    {
        if(!emit_ack_) emit_ack_ = [](const AckFields &){};
    }

    RxChunkEvent Reassembly::feed_pdu(const std::uint8_t *pdu, std::size_t pdu_size) noexcept
    {
        RxChunkEvent event;

        if(pdu == nullptr || pdu_size < kHeaderSize + kCrcSize)
        {
            event.accepted = false;
            return event;
        }

        Header h;
        if(!parse_header(pdu, pdu_size, h)) 
        {
            event.accepted = false;
            return event;
        }

        //fill event fields  after parsing header
        event.type = h.type;
        event.msg_id = h.msg_id;
        event.seq = h.seq;
        event.total = h.total;
        event.payload_len = h.payload_len;

        //if the pdu is an ack, reject it
        if(h.type == Type::ACK)
        {
            event.accepted = false;
            return event;
        }

        //parse pdu to get payload and validate crc
        vector<uint8_t> out_payload;
        if(!parse_pdu(pdu, pdu_size, h, out_payload)) 
        {
            event.accepted = false;
            return event;
        }

        //validate header fields and payload length
        if(h.total == 0 || h.seq >= h.total || h.payload_len != out_payload.size())
        {
            event.accepted = false;
            return event;
        }
        
        //check if message state exists, if not create it
        uint32_t msg_id = h.msg_id ;
        if(msgs_.find(msg_id) == msgs_.end())
        {
            MsgState new_msg;
            new_msg.type = h.type;
            new_msg.total = h.total;
            new_msg.chunks.resize(h.total);
            new_msg.received.resize(h.total, 0);
            new_msg.prefix = -1;
            new_msg.bytes_accum = 0;

            msgs_[msg_id] = move(new_msg);
            event.duplicate = false;
        }
        else 
        {
            if(msgs_[msg_id].type != h.type || msgs_[msg_id].total != h.total)
            {
                event.accepted = false;
                return event;
            }

            if(msgs_[msg_id].received[h.seq] == 1)
            {
                event.duplicate = true;
                event.accepted = false;
                event.highest_seq_ok = msgs_[msg_id].prefix;
                AckFields ack = {.msg_id = msg_id, .highest_seq_ok = static_cast<uint32_t>(msgs_[msg_id].prefix)};
                emit_ack_(ack); 
                return event;
            }
        }
        
        //Save Chunk 
        msgs_[msg_id].chunks[h.seq] = move(out_payload);
        msgs_[msg_id].received[h.seq] = 1;
        msgs_[msg_id].bytes_accum += h.payload_len;

        
        int current_prefix = msgs_[msg_id].prefix;
        while(msgs_[msg_id].prefix + 1 < static_cast<int>(h.total) && msgs_[msg_id].received[msgs_[msg_id].prefix + 1] == 1)
        {
            msgs_[msg_id].prefix++;
        }

        if(current_prefix != msgs_[msg_id].prefix)
        {
            event.highest_seq_ok = static_cast<uint32_t>(msgs_[msg_id].prefix);
            AckFields ack = {.msg_id = msg_id, .highest_seq_ok = static_cast<uint32_t>(msgs_[msg_id].prefix)};
            emit_ack_(ack);
        }
        else    
        {
            if(current_prefix == 0)
                event.highest_seq_ok = 0u;
            else
                event.highest_seq_ok = static_cast<uint32_t>(current_prefix);
        }
        
        event.completed = (msgs_[msg_id].prefix == static_cast<int>(h.total) - 1);
        event.accepted = true;
        return event;
    }

    bool Reassembly::is_complete(uint32_t msg_id) const noexcept
    {
        auto ptr = msgs_.find(msg_id);
        if(ptr == msgs_.end()) 
            return false;

        MsgState msg_state = ptr->second; 
        return (static_cast<uint32_t>(msg_state.prefix + 1) == msg_state.total);
    }

    bool Reassembly::extract_message(uint32_t msg_id, vector<uint8_t> &out) noexcept
    {
        if(!is_complete(msg_id))
            return false;
        
        out.clear();
        out.reserve(msgs_[msg_id].bytes_accum);
        for (uint8_t chunk = 0; chunk < msgs_[msg_id].total; chunk++)
        {
            for(uint8_t byte = 0; byte< msgs_[msg_id].chunks[chunk].size(); byte++)
            {
                out.push_back(msgs_[msg_id].chunks[chunk][byte]);
            }
        }

        msgs_.erase(msg_id);
        return true;
    }

    void Reassembly::clear() noexcept
    {
        msgs_.clear();
    }

}