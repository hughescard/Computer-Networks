#include "time.hpp"
#include <chrono>
using namespace std;

namespace linkchat {
    uint64_t steady_millis() noexcept
    {
        auto now = chrono::steady_clock::now();
        auto now_ms = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch());
        return static_cast<uint64_t>(now_ms.count());
    }
}
