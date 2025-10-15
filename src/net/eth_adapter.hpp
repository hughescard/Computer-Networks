#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include <string>
#include "../util/mac.hpp"  

namespace linkchat{

    struct EthConfig {
        std::string   ifname;       // "eth0", "wlan0"
        Mac           src_mac;      // local MAC 
        Mac           dst_mac;      // destiny MAC
        std::uint16_t ether_type;   // own EtherType 0x88B5 
        std::size_t   frame_mtu;    // interface MTU 
    };

    inline constexpr std::size_t kEthHdr = 14;

    bool eth_init(const EthConfig& cfg) noexcept;

    bool eth_send_pdu(const std::vector<std::uint8_t>& pdu) noexcept;

    void eth_rx_loop(std::function<void(const Mac& ,const std::uint8_t*, std::size_t)> on_pdu) noexcept;

    void eth_shutdown() noexcept;

    bool eth_send_pdu_to(const Mac& dst, const std::vector<std::uint8_t>& pdu) noexcept;


} 
