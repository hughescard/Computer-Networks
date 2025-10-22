#include "app.hpp"
#include "util/time.hpp"
#include "header.hpp"
#include <vector>
#include "util/mac.hpp"

using namespace std;

namespace linkchat
{

    SenderConfig correctness_check(SenderConfig cfg)
    {
        if (cfg.now == nullptr)
            cfg.now = steady_millis;
        if (cfg.window == 0)
            cfg.window = 1;
        if (cfg.mtu < kHeaderSize + kCrcSize + 1)
            cfg.mtu = kHeaderSize + kCrcSize + 1;
        if (cfg.rto_ms == 0)
            cfg.rto_ms = 1;
        return cfg;
    }

    LinkchatApp::LinkchatApp(SenderConfig cfg) noexcept
        : cfg_(move(correctness_check(cfg))),
          sender_([this](const vector<uint8_t> &pdu)
                  { if(emit_pdu_) emit_pdu_(pdu); }, cfg_),
          rx_([this](const AckFields &ack)
              {
                // route ACKs back to the origin of the current RX frame
                auto pdu = create_ack(ack);
                if(!pdu.empty()) {
                    if (emit_pdu_to_) emit_pdu_to_(current_rx_src_, pdu);
                    else if (emit_pdu_) emit_pdu_(pdu);
                }
              }),
          emit_pdu_{},
          emit_pdu_to_{},
          on_deliver_{}
    {
        if (!emit_pdu_)
            emit_pdu_ = [](const vector<uint8_t> &) {};
        if (!emit_pdu_to_)
            emit_pdu_to_ = [](const Mac&, const vector<uint8_t>&) {};
        if (!on_deliver_)
            on_deliver_ = [](uint32_t, Type, const vector<uint8_t> &, const Mac &) {};
    }

    void LinkchatApp::set_emit_pdu(function<void(const vector<uint8_t> &)> fn) noexcept
    {
        if (fn == nullptr)
            emit_pdu_ = [](const vector<uint8_t> &) {};
        else
            emit_pdu_ = move(fn);
    }

    void LinkchatApp::set_emit_pdu_to(function<void(const Mac&, const vector<uint8_t>&)> fn) noexcept
    {
        if (fn == nullptr)
            emit_pdu_to_ = [](const Mac&, const vector<uint8_t>&) {};
        else
            emit_pdu_to_ = move(fn);
    }

    void LinkchatApp::set_on_deliver(DeliverMsgFn fn) noexcept
    {
        if (fn == nullptr)
            on_deliver_ = [](uint32_t, Type, const vector<uint8_t> &, const Mac &) {};
        else
            on_deliver_ = move(fn);
    }

    void LinkchatApp::on_rx_pdu(const Mac &src_mac, const uint8_t *pdu, size_t pdu_size) noexcept
    {
        if (pdu == nullptr || pdu_size < kHeaderSize + kCrcSize)
            return;

        Header h{};
        if (!parse_header(pdu, pdu_size, h))
            return;

        const size_t want = kHeaderSize + static_cast<size_t>(h.payload_len) + kCrcSize;
        if (pdu_size < want)
            return;

        AckFields ack{};
        if (try_parse_ack(const_cast<uint8_t *>(pdu), want, ack))
        {
            // route ack to sender for this src_mac
            auto key = mac_to_string(src_mac);
            auto it = peer_senders_.find(key);
            if (it != peer_senders_.end())
                it->second.on_ack(ack);
            else
                sender_.on_ack(ack); // legacy path
            return;
        }

        // set current source for ACK routing
        current_rx_src_ = src_mac;
        RxChunkEvent event = rx_.feed_pdu(pdu, want);
        if (!event.accepted)
            return;
        if (event.completed || rx_.is_complete(event.msg_id))
        {
            vector<uint8_t> out_msg;
            if (rx_.extract_message(event.msg_id, out_msg))
                on_deliver_(event.msg_id, event.type, out_msg, src_mac);
        }
        else
            return;
    }

    uint32_t LinkchatApp::send_hello(const string &nick)
    {
        string nn = nick;
        if (nn.size() > HELLO_NICK_MAX)
            nn.resize(HELLO_NICK_MAX);
        vector<uint8_t> payload;
        payload.reserve(1 + nn.size());
        payload.push_back(static_cast<uint8_t>(nn.size()));
        payload.insert(payload.end(), nn.begin(), nn.end());
        return send_bytes(payload, Type::HELLO);
    }

    uint32_t LinkchatApp::send_bytes(const vector<uint8_t> &data, Type type) noexcept
    {
        if (data.empty())
            return 0;
        return sender_.send(data, type);
    }

    uint32_t LinkchatApp::send_bytes_to(const Mac& dst_mac, const vector<uint8_t>& data, Type type) noexcept
    {
        if (data.empty()) return 0;
        const string key = mac_to_string(dst_mac);
        auto it = peer_senders_.find(key);
        if (it == peer_senders_.end())
        {
            // create a Sender bound to this destination
            Sender s([this, dst_mac](const vector<uint8_t>& pdu){ if (emit_pdu_to_) emit_pdu_to_(dst_mac, pdu); }, cfg_);
            auto [ins_it, ok] = peer_senders_.emplace(key, move(s));
            it = ins_it;
        }
        return it->second.send(data, type);
    }

    void LinkchatApp::tick() noexcept
    {
        sender_.on_tick();
        for (auto &kv : peer_senders_)
            kv.second.on_tick();
    }

    bool LinkchatApp::is_done(uint32_t msg_id) const noexcept
    {
        if (sender_.is_done(msg_id)) return true;
        for (const auto &kv : peer_senders_)
            if (kv.second.is_done(msg_id)) return true;
        return false;
    }

    size_t LinkchatApp::in_flight(uint32_t msg_id) const noexcept
    {
        size_t n = sender_.in_flight(msg_id);
        for (const auto &kv : peer_senders_)
            n += kv.second.in_flight(msg_id);
        return n;
    }

    auto LinkchatApp::get_emit_pdu() const noexcept -> function<void(const vector<uint8_t> &)>
    {
        return emit_pdu_;
    }

}
