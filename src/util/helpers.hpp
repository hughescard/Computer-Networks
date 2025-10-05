#pragma once
#include <cstdint>
#include <cstddef>
#include "../header.hpp"
#include "structs.hpp"

namespace linkchat
{

    std::uint8_t type_to_uint8(Type t)noexcept;
    bool uint8_to_type(std::uint8_t t, Type &out)noexcept;
    void uint32_to_BE(std::uint32_t val, std::uint8_t *buf, int index)noexcept;
    void uint16_to_BE(std::uint16_t val, std::uint8_t *buf, int index)noexcept;
    std::uint32_t BE_to_uint32(const std::uint8_t *buf, int index)noexcept;
    std::uint16_t BE_to_uint16(const std::uint8_t *buf, int index)noexcept;
}
