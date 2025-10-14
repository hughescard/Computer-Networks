#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <functional>
#include "pdu.hpp"      
#include "header.hpp"   

namespace linkchat {

    using EmitTxFn = std::function<void(const std::vector<std::uint8_t>&)>;

    using NowFn = std::function<std::uint64_t(void)>;

    struct SenderConfig {
        std::uint16_t mtu = 1500;       
        std::uint32_t window = 4;       
        std::uint32_t rto_ms = 300;     
        NowFn now;                      
    };

    struct TxMsg {
        std::uint32_t                 msg_id{};
        Type                           type{};
        std::vector<std::vector<std::uint8_t>> pdus;   
        std::uint32_t                 base{0};         
        std::uint32_t                 next{0};         
        std::vector<std::uint64_t>    sent_at_ms;      
        bool                           done{false};
    };

    class Sender {
    public:
        explicit Sender(EmitTxFn emit_tx, SenderConfig cfg);

        std::uint32_t send(const std::vector<std::uint8_t>& data, Type type);

        void on_ack(const AckFields& ack) noexcept;

        void on_tick() noexcept;

        bool is_done(std::uint32_t msg_id) const noexcept;
        std::size_t in_flight(std::uint32_t msg_id) const noexcept;


    private:
        EmitTxFn emit_tx_;
        SenderConfig cfg_;
        std::unordered_map<std::uint32_t, TxMsg> msgs_;  
        std::uint32_t next_msg_id_{1};                   
    };

} 
