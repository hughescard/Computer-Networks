// src/sender.hpp
#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <functional>
#include "pdu.hpp"      // Type, chunkify_from_vector, etc.
#include "header.hpp"   // kHeaderSize, etc. (si lo necesitas)

namespace linkchat {

    // Emisor enviará bytes listos (PDU completas) al medio
    using EmitTxFn = std::function<void(const std::vector<std::uint8_t>&)>;

    // Reloj inyectable para tests: devuelve ms monotónicos (steady)
    using NowFn = std::function<std::uint64_t(void)>;

    struct SenderConfig {
        std::uint16_t mtu = 1500;       // MTU de enlace
        std::uint32_t window = 4;       // tamaño de ventana (PDUs en vuelo)
        std::uint32_t rto_ms = 300;     // timeout de retransmisión (ms)
        NowFn now;                      // reloj (obligatorio: steady ms)
    };

    // Estado interno de un mensaje en transmisión
    struct TxMsg {
        std::uint32_t                 msg_id{};
        Type                           type{};
        std::vector<std::vector<std::uint8_t>> pdus;   // PDUs completas
        std::uint32_t                 base{0};         // primera PDU no ACKed
        std::uint32_t                 next{0};         // primera PDU no enviada
        std::vector<std::uint64_t>    sent_at_ms;      // timestamp por PDU
        bool                           done{false};
    };

    class Sender {
    public:
        // cfg.now debe estar definido; emit_tx no nulo
        explicit Sender(EmitTxFn emit_tx, SenderConfig cfg);

        // Encola y lanza un mensaje: lo fragmenta y envía la primera ventana.
        // Devuelve el msg_id asignado.
        std::uint32_t send(const std::vector<std::uint8_t>& data, Type type);

        // Procesa un ACK acumulativo (highest_seq_ok).
        void on_ack(const AckFields& ack) noexcept;

        // Avanza temporizador: retransmite si hay timeout (Go-Back-N).
        void on_tick() noexcept;

        // Utilidades (opcionales pero útiles para tests/UI)
        bool is_done(std::uint32_t msg_id) const noexcept;
        std::size_t in_flight(std::uint32_t msg_id) const noexcept;


    private:
        EmitTxFn emit_tx_;
        SenderConfig cfg_;
        std::unordered_map<std::uint32_t, TxMsg> msgs_;  // por msg_id
        std::uint32_t next_msg_id_{1};                   // genera IDs
    };

} 
