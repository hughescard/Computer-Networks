#pragma once
#include <thread>
#include <functional>
#include <vector>
#include <atomic>
#include "../app.hpp"
#include "eth_adapter.hpp"

namespace linkchat
{

    struct AppEthHandle
    {
        std::thread rx_thread;
        std::atomic<bool> running{false};
    };

    bool bind_app_to_eth(LinkchatApp &app, const EthConfig &cfg, AppEthHandle &out) noexcept;

    void unbind_app_from_eth(AppEthHandle &h) noexcept;

}
