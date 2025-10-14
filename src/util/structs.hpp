#pragma once
#include <cstdint>

namespace linkchat {

enum class Type : std::uint8_t {
    MSG   = 1,
    FILE  = 2,
    ACK   = 3,
    HELLO = 4
};

enum class Off 
    { T=0, MID=1, SEQ=5, TOT=9, LEN=13 };

}
