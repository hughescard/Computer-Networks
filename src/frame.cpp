#include "frame.hpp"
#include "util/helpers.hpp"

using namespace std;

namespace linkchat
{
    size_t serialize_header(const Header & h, uint8_t * buf, size_t buf_size)
    {
        if(buf_size < 15 || buf == nullptr)
            return 0;
        if(type_to_uint8(h.type) != 1 && type_to_uint8(h.type) != 2 && type_to_uint8(h.type) != 3 && type_to_uint8(h.type) != 4)
            return 0;
        
        //type
        buf[uint8_t(Off::T)] = type_to_uint8(h.type);
        //message_id
        uint32_to_BE(h.msg_id,buf,uint8_t(Off::MID));
        //seq
        uint32_to_BE(h.seq,buf,uint8_t(Off::SEQ));
        //total
        uint32_to_BE(h.total,buf,uint8_t(Off::TOT));
        //payload_len
        uint16_to_BE(h.payload_len,buf,uint8_t(Off::LEN));

        return 15 ;
    }

    bool parse_header(const uint8_t * buf, size_t buf_size, Header & out)
    {
        if(buf == nullptr || buf_size < 15)
            return false;
         
        if(buf[0]!=1 && buf[0]!=2 && buf[0]!=3 && buf[0]!=4) 
            return false;

        //type
        uint8_to_type(buf[uint8_t(Off::T)],out.type);
        //msg
        out.msg_id = BE_to_uint32(buf,uint8_t(Off::MID));
        //seq
        out.seq = BE_to_uint32(buf,uint8_t(Off::SEQ));
        //total
        out.total = BE_to_uint32(buf,uint8_t(Off::TOT));
        //payload_len
        out.payload_len = BE_to_uint16(buf,uint8_t(Off::LEN));

        return true; // false = fallo
    }

}