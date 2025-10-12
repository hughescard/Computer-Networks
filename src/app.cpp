#include "app.hpp"
#include "util/time.hpp"  
#include "header.hpp"
#include <vector>

using namespace std;

namespace linkchat {

    SenderConfig correctness_check(SenderConfig cfg) 
    { 
        if(cfg.now == nullptr)
        cfg.now =  steady_millis;
        if(cfg.window == 0)
        cfg.window = 1;
        if(cfg.mtu < kHeaderSize + kCrcSize + 1)
        cfg.mtu = kHeaderSize + kCrcSize + 1;
        if(cfg.rto_ms == 0)
        cfg.rto_ms = 1;
        return cfg;
    }

    LinkchatApp::LinkchatApp(SenderConfig cfg)noexcept :  
    cfg_(move(correctness_check(cfg))),
    sender_([this](const vector<uint8_t>& pdu ){if(emit_pdu_)emit_pdu_(pdu);}, cfg_),
    rx_([this](const AckFields& ack){ sender_.on_ack(ack);}),
    emit_pdu_{},
    on_deliver_{}
    {
        if (!emit_pdu_)  
            emit_pdu_   = [](const vector<uint8_t>& ){};
        if (!on_deliver_) 
            on_deliver_ = [](uint32_t, Type, const vector<uint8_t>& ){};
    }

    void LinkchatApp::set_emit_pdu(function<void(const vector<uint8_t>&)> fn) noexcept
    {
        if(fn == nullptr) emit_pdu_ = [](const vector<uint8_t>&){};
        else emit_pdu_ = move(fn); 
    }

    void LinkchatApp::set_on_deliver(DeliverMsgFn fn) noexcept
    {
        if(fn == nullptr) on_deliver_ = [](uint32_t, Type, const vector<uint8_t>&){};
        else on_deliver_ = move(fn);
    }

    void LinkchatApp::on_rx_pdu(const uint8_t* pdu, size_t pdu_size) noexcept
    {
        if(pdu == nullptr || pdu_size < kHeaderSize + kCrcSize)
            return;
        RxChunkEvent event = rx_.feed_pdu(pdu, pdu_size);
        if(!event.accepted)
            return;
        if(event.completed || rx_.is_complete(event.msg_id))
        {
            vector<uint8_t> out_msg;
            if(rx_.extract_message(event.msg_id, out_msg))
            {
                on_deliver_(event.msg_id, event.type, out_msg);
            }
        }
        else 
            return;
    }

    uint32_t LinkchatApp::send_bytes(const vector<uint8_t>& data, Type type) noexcept
    {
        if(data.empty())
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

}
