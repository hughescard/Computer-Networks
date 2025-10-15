#include "app_eth_bind.hpp"
#include "../app.hpp"
#include <thread>
#include <functional>
#include <vector>
#include <atomic>

using namespace std;

namespace linkchat
{
    bool bind_app_to_eth(LinkchatApp &app, const EthConfig &cfg, AppEthHandle &out) noexcept
    {
        if (!eth_init(cfg))
            return false;

        app.set_emit_pdu([](const vector<uint8_t> &pdu)
                         { eth_send_pdu(pdu); });

        out.running = true;
        out.rx_thread = thread([&app, &out]
                               { eth_rx_loop([&](const Mac &src_mac, const uint8_t *pdu, size_t pdu_size)
                                             {if(out.running) app.on_rx_pdu(src_mac, pdu, pdu_size); }); });

        return true;
    }

    void unbind_app_from_eth(AppEthHandle &h) noexcept
    {
        h.running = false;
        eth_shutdown();
        if (h.rx_thread.joinable())
            h.rx_thread.join();
    }

    bool app_eth_send_to(const Mac &dst, const vector<uint8_t> &pdu) noexcept
    {
        return eth_send_pdu_to(dst, pdu);
    }

}
