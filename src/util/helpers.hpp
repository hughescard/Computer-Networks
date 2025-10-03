#pragma once
#include <cstdint>
#include <cstddef>
#include "../header.hpp"

namespace linkchat
{

    uint8_t type_to_uint8(Type t);
    bool uint8_to_type(uint8_t t, Type &out);
    void uint32_to_BE(uint32_t val, uint8_t *buf, int index);
    void uint16_to_BE(uint16_t val, uint8_t *buf, int index);
    uint32_t BE_to_uint32(const uint8_t *buf, int index);
    uint16_t BE_to_uint16(const uint8_t *buf, int index);
}
