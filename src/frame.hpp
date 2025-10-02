#pragma once
#define FRAME_HPP

#include <cstdint>
#include <cstddef>
#include "util/helpers.hpp"

namespace linkchat
{
    
    size_t serialize_header(const Header & h, uint8_t * buf, size_t buf_size);

    bool parse_header(const uint8_t * buf, size_t buf_size, Header & out);
 
}
