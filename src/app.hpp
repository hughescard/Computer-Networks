#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include "sender.hpp"
#include "reassembly.hpp"
#include "util/structs.hpp" // Type
#include "util/mac.hpp"     // Mac

namespace linkchat {
    
    constexpr size_t HELLO_NICK_MAX = 255;
    using DeliverMsgFn = std::function<void(std::uint32_t msg_id,
                                            Type type,
                                            const std::vector<std::uint8_t>& data,
                                            const Mac& src_mac)>;

    class LinkchatApp {
    public:
        explicit LinkchatApp(SenderConfig cfg) noexcept;

        void set_emit_pdu(std::function<void(const std::vector<std::uint8_t>&)> fn) noexcept;

        // Set a function to emit a PDU to a specific destination MAC
        void set_emit_pdu_to(std::function<void(const Mac&, const std::vector<std::uint8_t>&)> fn) noexcept;

        void set_on_deliver(DeliverMsgFn fn) noexcept;

        void on_rx_pdu(const Mac& src_mac, const std::uint8_t* pdu, std::size_t pdu_size) noexcept;

        std::uint32_t send_bytes(const std::vector<std::uint8_t>& data, Type type) noexcept;
        
        // Send to a specific destination (multi-peer support)
        std::uint32_t send_bytes_to(const Mac& dst_mac, const std::vector<std::uint8_t>& data, Type type) noexcept;
        
        std::uint32_t send_hello(const std::string& nick);
        
        void tick() noexcept;
        
        bool is_done(std::uint32_t msg_id) const noexcept;
        
        std::size_t in_flight(std::uint32_t msg_id) const noexcept;

        std::function<void(const std::vector<std::uint8_t>&)> get_emit_pdu() const noexcept;

        
    private:
        SenderConfig cfg_;
        // single-sender legacy kept for compatibility with send_bytes();
        Sender     sender_;
        // per-peer senders (keyed by ASCII MAC)
        std::unordered_map<std::string, Sender> peer_senders_;
        
        Reassembly rx_;
        std::function<void(const std::vector<std::uint8_t>&)> emit_pdu_;
        std::function<void(const Mac&, const std::vector<std::uint8_t>&)> emit_pdu_to_;
        DeliverMsgFn on_deliver_;
        Mac current_rx_src_{}; // used to route ACKs to origin
    };

} 
