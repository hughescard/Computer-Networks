#include "sender.hpp"
#include <algorithm>
#include <utility>

using namespace std;

namespace linkchat {

    Sender::Sender(EmitTxFn emit_tx, SenderConfig cfg)
    {
        emit_tx_ = emit_tx;
        if(emit_tx_ == nullptr)
            emit_tx_ = [](const vector<uint8_t>&){};
        
        cfg_ = cfg;
        if(cfg_.now == nullptr)
            cfg_.now = [](){ return 0u; };
    
        if(cfg_.window == 0)
            cfg_.window = 1;
    
        if(cfg_.mtu < kHeaderSize + kCrcSize + 1)
            cfg_.mtu = kHeaderSize + kCrcSize + 1;

        if(cfg_.rto_ms == 0)
            cfg_.rto_ms = 1;
        
        next_msg_id_ = 1;
    }

    uint32_t Sender::send(const vector<uint8_t>& data, Type type)
    {
        uint32_t msg_id = next_msg_id_++;

        vector<vector<uint8_t>> pdus = chunkify_from_vector(data,msg_id,type,cfg_.mtu);

        if(pdus.empty())
            return 0;

        TxMsg txmsg;
        txmsg.msg_id = msg_id;
        txmsg.type = type;
        txmsg.pdus = move(pdus);
        txmsg.base = 0;
        txmsg.next = 0;
        txmsg.sent_at_ms.resize(txmsg.pdus.size(),0);
        txmsg.done = false;

        msgs_[msg_id] = move(txmsg);
        auto msg = msgs_.find(msg_id);
        TxMsg& msg_st = msg->second;

        uint32_t window_limit = min(cfg_.window, static_cast<uint32_t>(msg_st.pdus.size()));
        vector<vector<uint8_t>> to_send;
        to_send.reserve(window_limit);
        for(uint32_t i = 0; i < window_limit; i++)
        {
            to_send.push_back(msg_st.pdus[i]);
            msg_st.sent_at_ms[i] = cfg_.now();
        }
        msg_st.next = window_limit;

        for(uint32_t i = 0; i < to_send.size(); i++)
        {
            emit_tx_(to_send[i]);
        }

        return msg_id;
    }

    void Sender::on_ack(const AckFields& ack)noexcept
    {
        auto msg_id = ack.msg_id;
        if(msgs_.find(msg_id) == msgs_.end())
            return;
    
        TxMsg& msg_st = msgs_[msg_id];
        
        if(msg_st.done)
            return;

        if(msg_st.pdus.empty())
            return;
        
        uint32_t max_index = static_cast<uint32_t>(msg_st.pdus.size()) - 1;

        uint32_t index = min(ack.highest_seq_ok, max_index);

        if(index + 1 <= msg_st.base)
            return;

        msg_st.base = index + 1;

        if(msg_st.base>=msg_st.pdus.size())
        {
            msg_st.done = true; 
            msgs_.erase(msg_id);
            return;
        }

        while(msg_st.next < msg_st.pdus.size() && msg_st.next < msg_st.base + cfg_.window)
        {
            emit_tx_(msg_st.pdus[msg_st.next]);
            msg_st.sent_at_ms[msg_st.next] = cfg_.now();
            msg_st.next++;
        }
    
    }

    void Sender::on_tick()noexcept
    {
        auto now = cfg_.now();
        vector<uint32_t> to_erase;
        for(auto& [msg_id,msg_st] : msgs_)
        {
            if(msg_st.done)
            {
                to_erase.push_back(msg_id);
                continue;
            }
            if(msg_st.base>= msg_st.next)
                continue;
            
            uint32_t curr_ind = msg_st.base;

            if(msg_st.sent_at_ms[curr_ind] == 0)
                continue;
            
            if(now - msg_st.sent_at_ms[curr_ind] < cfg_.rto_ms)
                continue;
            
            for(uint32_t j = msg_st.base;j<msg_st.next;j++)
            {
                emit_tx_(msg_st.pdus[j]);
                msg_st.sent_at_ms[j] = now;
            }
        }
        for(size_t i = 0;i<to_erase.size();i++)
        {
            msgs_.erase(to_erase[i]);
        }
    }

    bool Sender::is_done(uint32_t msg_id)const noexcept
    {
        return (msgs_.find(msg_id) == msgs_.end());
    }

    size_t Sender::in_flight(uint32_t msg_id)const noexcept
    {
        if(msgs_.find(msg_id) == msgs_.end())
            return 0;
        else 
        {
            auto msg_st = msgs_.find(msg_id);

            if(msg_st->second.next >= msg_st->second.base)
                return msg_st->second.next - msg_st->second.base;
            else 
                return 0;
        }
    }


} 
