#include "helpers.hpp"
#include "structs.hpp"
#include <cstddef>
#include <cstdint>

namespace linkchat
{   
    //helpers (implementations)
    uint8_t type_to_uint8(Type t)noexcept
    {
        return static_cast<uint8_t>(t);
    }

    bool uint8_to_type(uint8_t t, Type &out)noexcept
    {
        switch (t) {
            case 1: out = Type::MSG; return true;
            case 2: out = Type::FILE; return true;
            case 3: out = Type::ACK; return true;
            case 4: out = Type::HELLO; return true;
            default: return false;
        }
    }

    void uint16_to_BE(uint16_t val, uint8_t * buf, int index=0)noexcept
    {
        buf[index] = (val >> 8);
        buf[index+1] = (val );
    }

    void uint32_to_BE(uint32_t val, uint8_t * buf,int index=0)noexcept
    {
        buf[index] = (val >> 24);
        buf[index+1] = (val >> 16);
        buf[index+2] = (val >> 8);
        buf[index+3] = (val );
    }

    uint32_t BE_to_uint32(const uint8_t * buf, int index=0)noexcept
    {
        return (buf[index]<<24) |
               (buf[index+1]<<16) |
               (buf[index+2]<<8 ) |
               (buf[index+3] );
    }

    uint16_t BE_to_uint16(const uint8_t * buf, int index=0)noexcept
    {
        return (buf[index]<<8) |
               (buf[index+1] );
    }

}