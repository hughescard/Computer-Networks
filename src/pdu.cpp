#include "pdu.hpp"
#include "util/crc32.hpp"
#include "util/helpers.hpp"
#include <cstring>     // memcpy
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
}