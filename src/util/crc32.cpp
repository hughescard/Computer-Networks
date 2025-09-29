#include "crc32.hpp"
using namespace std;

namespace linkchat
{
    uint32_t crc32(const uint8_t *data, size_t len) 
    { 
        const uint32_t polynomial = 0xEDB88320u;
        uint32_t crc = 0xFFFFFFFFu;
        if(len == 0) return 0x00000000u;
        
        for(size_t i=0 ; i < len; i++) 
        { 
            uint32_t b = static_cast<uint32_t>(data[i]);
            crc^=b;
            for(int j=0; j < 8; j++) 
            { 
                if(crc & 1u) 
                    crc = (crc >> 1) ^ polynomial; 
                else
                    crc >>= 1; 
            }
        }

        crc = ~crc;
        return crc;
    }
}
