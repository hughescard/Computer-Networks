#include "pdu.hpp"
#include "util/crc32.hpp"
#include "util/helpers.hpp"
#include "util/structs.hpp"
#include <cstring>     // memcpy
#include <vector>
using namespace std;

namespace linkchat 
{

    size_t build_pdu(const Header& h,
                 const uint8_t* payload, size_t payload_len,
                 uint8_t* out, size_t out_cap) noexcept
    {
        if(out == nullptr || out_cap <15 + payload_len + 4)
            return 0;
        
        if(payload_len != h.payload_len)
            return 0;
        
        if(serialize_header(h, out, out_cap)!=15)
            return 0;
        
        if(payload_len > 0 && payload != nullptr)
            memcpy(out+15, payload, payload_len);
        
        uint32_t crc = crc32(out + 15, payload_len);
        uint32_to_BE(crc, out, 15 + payload_len);

        return 15 + payload_len + 4;

    }

    bool parse_pdu(const uint8_t * buf, size_t buf_size,
                   Header & out_h,
                   vector<uint8_t> & out_payload) noexcept
    {
        if(buf == nullptr || buf_size < kHeaderSize+kCrcSize)
            return false;

        if(!parse_header(buf, buf_size, out_h)) return false;

        if(buf_size < kHeaderSize + kCrcSize) return false;

        const size_t real_paylen = buf_size - (kHeaderSize + kCrcSize);
        
        const uint8_t * payload_ptr = buf + kHeaderSize;
        const uint8_t * crc_ptr = payload_ptr + real_paylen;
        uint32_t received_crc = BE_to_uint32(crc_ptr, 0);
        uint32_t computed_crc = crc32(payload_ptr, real_paylen);

        if(received_crc != computed_crc)
            return false;

        //copy payload to out_payload vector                                                       
        for(size_t i=kHeaderSize ;i<kHeaderSize+real_paylen;i++)
            out_payload.push_back(buf[i]) ;
            
        return true;
    }

    bool is_ack_header(const Header& h)noexcept
    {
        if((h.type == Type::ACK) && (h.seq == 0) && (h.total == 0) && (h.payload_len == kAckPayloadSize))
            return true;

        return false;
    }

    vector<uint8_t> create_ack(const AckFields& ack)noexcept
    {
        int total_pdu_size = kHeaderSize + kAckPayloadSize + kCrcSize;//total_size = 27
        vector<uint8_t> pdu_out(total_pdu_size);

        Header h;
        h.type = Type::ACK;
        h.msg_id = ack.msg_id;
        h.seq = 0;
        h.total = 0;
        h.payload_len = kAckPayloadSize;

        //Fill Header 
        uint8_t * header_ptr = pdu_out.data();
        if(serialize_header(h,header_ptr,kHeaderSize)!=kHeaderSize)return {};//return empty vector on failure
        
        //Fill Payload
        uint8_t * payload_ptr = pdu_out.data() + kHeaderSize;
        uint32_to_BE(ack.msg_id,payload_ptr,0);
        uint32_to_BE(ack.highest_seq_ok,payload_ptr,4);
        
        //Fill CRC
        uint8_t * crc_ptr = pdu_out.data() + kHeaderSize + kAckPayloadSize;
        uint32_t crc = crc32(payload_ptr,kAckPayloadSize);
        uint32_to_BE(crc,crc_ptr,0);

        return pdu_out;
    }

    bool try_parse_ack(const uint8_t * pdu,size_t pdu_size,AckFields& out)noexcept
    {
        if( pdu == nullptr || pdu_size < kHeaderSize + kAckPayloadSize + kCrcSize)
            return false;
        
        Header h;
        
        if(!parse_header(pdu,pdu_size,h))
            return false;
        
        if(!is_ack_header(h))
            return false;

        //check crc
        int crc_index = kHeaderSize+kAckPayloadSize;
        uint32_t crc_rcv = BE_to_uint32(pdu,crc_index);
        const uint8_t * payload_ptr = pdu + kHeaderSize;
        uint32_t crc_calc = crc32(payload_ptr,kAckPayloadSize);
        if(crc_rcv != crc_calc)
            return false;

        //fill out with msg_id and highest_seq_ok info from parsed pdu
        out.msg_id = BE_to_uint32(pdu,kHeaderSize);
        out.highest_seq_ok = BE_to_uint32(pdu,kHeaderSize + 4);

        //check that msg_id from ack payload structure matches the msg_id field from header
        if(out.msg_id != h.msg_id)return false;
        
        return true;
    }

    [[nodiscard]] vector<vector<uint8_t>> chunkify_from_buffer(const uint8_t *data,
                                                               size_t data_size, 
                                                               uint32_t msg_id,
                                                               Type msg_type, 
                                                               uint16_t mtu)
    {
        if(data_size == 0 || data==nullptr)
            return {};

        size_t cap = mtu_payload(mtu);

        if(cap==0)
            return {};

        vector<vector<uint8_t>> out;

        uint32_t total = (data_size)/cap;
        if(data_size%cap != 0)
            total++;

        out.reserve(total);

        for(uint32_t seq=0;seq<total;seq++)
        {
            size_t offset = static_cast<size_t>(seq)*cap;
            size_t chunk_len = min(cap,data_size-offset);
            Header h;
            h.type = msg_type;
            h.msg_id = msg_id;
            h.seq = seq;
            h.total = total;
            h.payload_len = static_cast<uint16_t>(chunk_len);
        
            vector<uint8_t> pdu(kHeaderSize+chunk_len+kCrcSize);
            const uint8_t * payload = data + offset;
            size_t built_pdu = build_pdu(h,payload,chunk_len,pdu.data(),pdu.size());
            
            if(built_pdu != kHeaderSize + chunk_len + kCrcSize)
                return {};

            out.push_back(move(pdu));
        }

        return out;
    }

    [[nodiscard]] vector<vector<uint8_t>> chunkify_from_vector( const vector<uint8_t> &msg,
                                                                uint32_t msg_id,
                                                                Type msg_type,
                                                                uint16_t mtu)
    {
        if(msg.empty())
            return {};
        else 
            return chunkify_from_buffer(msg.data(),msg.size(),msg_id,msg_type,mtu);
    }

}