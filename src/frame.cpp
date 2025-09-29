#include "frame.hpp"

using namespace std;

namespace linkchat
{
    size_t serialize_header(const Header & /*h*/, uint8_t * /*buf*/, size_t /*buf_size*/)
    {
        // TODO: implementar serialización real (BE) en el subpaso siguiente
        return 0; // 0 = fallo/“no escribí el header”
    }

    bool parse_header(const uint8_t * /*buf*/, size_t /*buf_size*/, Header & /*out*/)
    {
        // TODO: implementar parseo real (BE) en el subpaso siguiente
        return false; // false = fallo
    }

} // namespace linkchat