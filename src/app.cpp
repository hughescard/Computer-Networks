#include "app.hpp"
#include "util/time.hpp"
#include "header.hpp"
#include <vector>

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
            auto pdu = create_ack(ack);
            if(!pdu.empty() && emit_pdu_) emit_pdu_(pdu); }),
          emit_pdu_{},
          on_deliver_{}
    {
        if (!emit_pdu_)
            emit_pdu_ = [](const vector<uint8_t> &) {};
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
            sender_.on_ack(ack);
            return;
        }

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

    void LinkchatApp::tick() noexcept
    {
        sender_.on_tick();
    }

    bool LinkchatApp::is_done(uint32_t msg_id) const noexcept
    {
        return sender_.is_done(msg_id);
    }

    size_t LinkchatApp::in_flight(uint32_t msg_id) const noexcept
    {
        return sender_.in_flight(msg_id);
    }

    auto LinkchatApp::get_emit_pdu() const noexcept -> function<void(const vector<uint8_t> &)>
    {
        return emit_pdu_;
    }

}
