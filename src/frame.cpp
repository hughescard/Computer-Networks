#include "frame.hpp"

using namespace std;

namespace linkchat
{
    //helpers (implementations)
    uint8_t type_to_uint8(Type t)
    {
        return static_cast<uint8_t>(t);
    }

    bool uint8_to_type(uint8_t t, Type &out)
    {
        switch (t) {
            case 1: out = Type::MSG; return true;
            case 2: out = Type::FILE; return true;
            case 3: out = Type::ACK; return true;
            case 4: out = Type::HELLO; return true;
            default: return false;
        }
    }

    void uint16_to_BE(uint16_t val, uint8_t * buf, int index=0)
    {
        buf[index] = (val >> 8);
        buf[index+1] = (val );
    }

    void uint32_to_BE(uint32_t val, uint8_t * buf,int index=0)
    {
        buf[index] = (val >> 24);
        buf[index+1] = (val >> 16);
        buf[index+2] = (val >> 8);
        buf[index+3] = (val );
    }

    uint32_t BE_to_uint32(const uint8_t * buf, int index=0)
    {
        return (buf[index]<<24) |
               (buf[index+1]<<16) |
               (buf[index+2]<<8 ) |
               (buf[index+3] );
    }

    uint16_t BE_to_uint16(const uint8_t * buf, int index=0)
    {
        return (buf[index]<<8) |
               (buf[index+1] );
    }

    //main functions
    size_t serialize_header(Header & h, uint8_t * buf, size_t buf_size)
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

} // namespace linkchat