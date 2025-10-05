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
        if(buf == nullptr || buf_size < 19 /*header_size+crc_size*/)
            return false;

        if(!parse_header(buf, 15, out_h))return false;

        uint8_t header_paylen = out_h.payload_len;
        uint8_t real_paylen = buf_size - 19;//total_size - header_size - crc_size (header_size - crc_size = 19)
        
        if(header_paylen != real_paylen)
            return false;

        uint32_t received_crc = BE_to_uint32(buf, buf_size - 4);
        uint32_t computed_crc = crc32(buf + 15, real_paylen);

        if(received_crc != computed_crc)
            return false;

        //copy payload to out_payload vector                                                       
        for(int i=15 ;i<15+real_paylen;i++) //using real_paylen is equivalent to header_paylen
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
        if(serialize_header(h,header_ptr,kHeaderSize)!=15)return {};//return empty vector on failure
        
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

    bool try_parse_ack(uint8_t * pdu,size_t pdu_size,AckFields& out)noexcept
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
        uint8_t * payload_ptr = pdu + kHeaderSize;
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
}