#pragma once
#include <cstdint>

using namespace std;

namespace linkchat
{
    inline uint32_t crc32_stub(const unsigned char *, int) { return 0; }
}
