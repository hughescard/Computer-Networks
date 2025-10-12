#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include "sender.hpp"
#include "reassembly.hpp"

namespace linkchat {
    using DeliverMsgFn = std::function<void(std::uint32_t msg_id,
                                            Type type,
                                            const std::vector<std::uint8_t>& data)>;

    class LinkchatApp {
    public:
        explicit LinkchatApp(SenderConfig cfg) noexcept;

        void set_emit_pdu(std::function<void(const std::vector<std::uint8_t>&)> fn) noexcept;

        void set_on_deliver(DeliverMsgFn fn) noexcept;

        void on_rx_pdu(const std::uint8_t* pdu, std::size_t pdu_size) noexcept;

        std::uint32_t send_bytes(const std::vector<std::uint8_t>& data, Type type) noexcept;

        void tick() noexcept;

        bool is_done(std::uint32_t msg_id) const noexcept;
        std::size_t in_flight(std::uint32_t msg_id) const noexcept;

    private:
        SenderConfig cfg_;
        Sender     sender_;
        Reassembly rx_;
        std::function<void(const std::vector<std::uint8_t>&)> emit_pdu_;
        DeliverMsgFn on_deliver_;
    };

} 
