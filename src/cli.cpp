#include "cli.hpp"

#include "app.hpp"          // LinkchatApp, SenderConfig
#include "app_eth_bind.hpp" // AppEthHandle, bind_app_to_eth, unbind_app_from_eth
#include "eth_adapter.hpp"  // EthConfig
#include "mac.hpp"          // parse_mac(Mac)

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <optional>
#include <algorithm>
#include <iomanip>
#include <ctime>
#include <unordered_set>

using namespace std;
using namespace chrono_literals;
using namespace linkchat;
namespace fs = filesystem;

namespace
{
    atomic<bool> g_running{true};
    void on_sigint(int) { g_running.store(false); }

    static bool read_file(const string &path, vector<uint8_t> &out)
    {
        error_code ec;
        fs::path p = fs::absolute(fs::path(path), ec);
        if (ec || !fs::exists(p))
            return false;

        ifstream f(p, ios::binary);
        if (!f)
            return false;
        f.seekg(0, ios::end);
        auto sz = f.tellg();
        if (sz <= 0)
            return false;
        out.resize(static_cast<size_t>(sz));
        f.seekg(0, ios::beg);
        f.read(reinterpret_cast<char *>(out.data()), out.size());
        return f.good();
    }

    static bool write_file(const string &path, const vector<uint8_t> &data)
    {
        ofstream f(path, ios::binary);
        if (!f)
            return false;
        f.write(reinterpret_cast<const char *>(data.data()), data.size());
        return f.good();
    }

    static void print_help()
    {
        cout <<
            R"(Commands:
            config                Configure interface, destination MAC and protocol params
            chat                  Start interactive chat (text + /sendfile <path>)
            groupchat             Start interactive group chat (send to all online)
            send <path>           Send a file directly and return to prompt
            discover              Send HELLO packet to discover peers
            info                  Show current configuration
            exit                  Quit

            While in chat:
            Type messages and press Enter to send
            Use /sendfile <path> to send files
            Use /online to list online peers (alias + mac)
            Use /contacts to list all seen contacts (with status)
            Use /connect <alias> to switch chat target
            In groupchat, manage recipients with:
              /members list | add <alias> | del <alias|mac> | clear
            Use /quit to leave chat
            )";
    }
    static bool ensure_dir(const string &d)
    {
        error_code ec;
        if (d.empty())
            return false;
        fs::create_directories(d, ec);
        return !ec;
    }

    static vector<uint8_t> wrap_file_with_name(const string &filepath,
                                               const vector<uint8_t> &file_bytes)
    {
        string name = fs::path(filepath).filename().string();
        if (name.size() > 65535)
            name = name.substr(name.size() - 65535);

        uint16_t nlen = static_cast<uint16_t>(name.size());
        uint8_t hi = static_cast<uint8_t>(nlen >> 8);
        uint8_t lo = static_cast<uint8_t>(nlen & 0xFF);

        vector<uint8_t> out;
        out.reserve(2 + name.size() + file_bytes.size());
        out.push_back(hi);
        out.push_back(lo);
        out.insert(out.end(), name.begin(), name.end());
        out.insert(out.end(), file_bytes.begin(), file_bytes.end());
        return out;
    }

    static bool unwrap_file_with_name(const vector<uint8_t> &data,
                                      string &out_name,
                                      vector<uint8_t> &out_bytes)
    {
        if (data.size() < 2)
            return false;
        uint16_t nlen = (static_cast<uint16_t>(data[0]) << 8) | data[1];
        if (static_cast<uint16_t>(data.size()) < 2 + nlen)
            return false;

        out_name.assign(reinterpret_cast<const char *>(&data[2]), nlen);
        out_bytes.assign(data.begin() + 2 + nlen, data.end());

        out_name = fs::path(out_name).filename().string();
        return !out_name.empty();
    }

    static string get_local_mac_ascii(const string &ifname)
    {
        string path = "/sys/class/net/" + ifname + "/address";
        ifstream f(path);
        if (!f)
            return {};
        string mac;
        getline(f, mac);
        // ("aa:bb:cc:dd:ee:ff")
        if (mac.size() >= 17)
            mac = mac.substr(0, 17);
        for (auto &c : mac)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        return mac;
    }

    //  HELLO payload: [1B nick_len][nick][mac_ascii(17B "aa:bb:cc:dd:ee:ff")]
    static vector<uint8_t> build_hello_payload(const string &nick, const string &mac_ascii)
    {
        string nn = nick;
        if (nn.size() > 255)
            nn.resize(255);
        vector<uint8_t> out;
        out.reserve(1 + nn.size() + 17);
        out.push_back(static_cast<uint8_t>(nn.size()));
        out.insert(out.end(), nn.begin(), nn.end());
        string mac = mac_ascii;
        if (mac.size() != 17)
            mac = "??:??:??:??:??:??";
        out.insert(out.end(), mac.begin(), mac.end());
        return out;
    }

    static bool parse_hello_payload(const vector<uint8_t> &data, string &out_nick, string &out_mac)
    {
        if (data.size() < 1 + 17)
            return false;
        uint8_t nlen = data[0];
        if (static_cast<uint8_t>(data.size()) < 1 + nlen + 17)
            return false;
        out_nick.assign(reinterpret_cast<const char *>(&data[1]), nlen);
        out_mac.assign(reinterpret_cast<const char *>(&data[1 + nlen]), 17);
        return true;
    }

    static string now_hms()
    {
        using namespace std::chrono;
        auto t = system_clock::to_time_t(system_clock::now());
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%H:%M:%S");
        return oss.str();
    }

    struct PeerInfo {
        string alias;
        Mac mac;
        string mac_ascii;
        chrono::steady_clock::time_point last_seen;
    };

    class PeerRegistry {
    public:
        explicit PeerRegistry(chrono::seconds ttl): ttl_(ttl) {}

        void upsert(const string& alias, const Mac& mac, const string& mac_ascii)
        {
            lock_guard<mutex> lk(mu_);
            string key = mac_to_string(mac);
            auto &p = by_mac_[key];
            p.alias = alias;
            p.mac = mac;
            p.mac_ascii = mac_ascii.empty() ? key : mac_ascii;
            p.last_seen = chrono::steady_clock::now();
        }

        vector<PeerInfo> list_online() const
        {
            lock_guard<mutex> lk(mu_);
            vector<PeerInfo> out;
            auto now = chrono::steady_clock::now();
            for (auto &kv : by_mac_)
            {
                if (now - kv.second.last_seen <= ttl_)
                    out.push_back(kv.second);
            }
            return out;
        }

        vector<PeerInfo> list_all() const
        {
            lock_guard<mutex> lk(mu_);
            vector<PeerInfo> out;
            for (auto &kv : by_mac_)
                out.push_back(kv.second);
            return out;
        }

        vector<PeerInfo> find_by_alias(const string& alias) const
        {
            lock_guard<mutex> lk(mu_);
            vector<PeerInfo> out;
            auto now = chrono::steady_clock::now();
            for (auto &kv : by_mac_)
            {
                if (now - kv.second.last_seen <= ttl_ && kv.second.alias == alias)
                    out.push_back(kv.second);
            }
            return out;
        }

        optional<PeerInfo> get_by_mac(const Mac& mac) const
        {
            lock_guard<mutex> lk(mu_);
            string key = mac_to_string(mac);
            auto it = by_mac_.find(key);
            if (it == by_mac_.end()) return nullopt;
            auto now = chrono::steady_clock::now();
            if (now - it->second.last_seen > ttl_) return nullopt;
            return it->second;
        }

        optional<PeerInfo> latest_by_alias(const string& alias) const
        {
            lock_guard<mutex> lk(mu_);
            optional<PeerInfo> best;
            auto now = chrono::steady_clock::now();
            for (auto &kv : by_mac_)
            {
                const auto &p = kv.second;
                if (now - p.last_seen > ttl_)
                    continue;
                if (p.alias != alias)
                    continue;
                if (!best.has_value() || p.last_seen > best->last_seen)
                    best = p;
            }
            return best;
        }

        void prune() { /* no-op: keep contacts history during session */ }
    private:
        chrono::seconds ttl_;
        mutable mutex mu_;
        unordered_map<string, PeerInfo> by_mac_;
    };

    static bool make_ethcfg_for(const RuntimeConfig &rcfg,
                                const string &dst_mac_ascii,
                                EthConfig &out)
    {
        out.ifname = rcfg.ifname;
        out.ether_type = rcfg.ethertype;
        out.frame_mtu = static_cast<size_t>(rcfg.mtu);

        if (!parse_mac(dst_mac_ascii, out.dst_mac))
            return false;

        return true;
    }

    
}

int run_cli()
{
    RuntimeConfig cfg;
    signal(SIGINT, on_sigint);

    cout << "LinkChat — Ethernet P2P Messenger (Layer 2)\n";
    cout << "Type 'help' for commands.\n";

    string line;
    while (g_running.load())
    {
        cout << "\n> ";
        if (!getline(cin, line))
            break;
        if (line.empty())
            continue;

        stringstream ss(line);
        string cmd;
        ss >> cmd;

        if (cmd == "help")
        {
            print_help();
            continue;
        }

        if (cmd == "exit" || cmd == "quit")
        {
            break;
        }

        if (cmd == "info")
        {
            cout << "Interface : " << (cfg.ifname.empty() ? "(unset)" : cfg.ifname) << "\n"
                 << "Dest MAC  : " << (cfg.dst_mac.empty() ? "(unset)" : cfg.dst_mac) << "\n"
                 << "MTU       : " << cfg.mtu << "\n"
                 << "Window    : " << cfg.window << "\n"
                 << "RTO (ms)  : " << cfg.rto_ms << "\n"
                 << "Ethertype : 0x" << hex << cfg.ethertype << dec << "\n"
                 << "Outdir    : " << cfg.outdir << "\n"
                 << "Alias     : " << cfg.alias << "\n";
            continue;
        }

        if (cmd == "config")
        {
            string s;

            cout << "Interface name: ";
            getline(cin, cfg.ifname);

            cout << "MTU (default 1500): ";
            getline(cin, s);
            if (!s.empty())
                cfg.mtu = max(60, atoi(s.c_str()));

            cout << "Window (default 1): ";
            getline(cin, s);
            if (!s.empty())
                cfg.window = max(1, atoi(s.c_str()));

            cout << "RTO (ms, default 300): ";
            getline(cin, s);
            if (!s.empty())
                cfg.rto_ms = max(1, atoi(s.c_str()));

            cout << "Downloads dir (default 'inbox'): ";
            string s2;
            getline(cin, s2);
            if (!s2.empty())
                cfg.outdir = s2;
            if (!ensure_dir(cfg.outdir))
            {
                cerr << "[WARN] cannot create/access '" << cfg.outdir << "', using current dir\n";
            }

            cout << "User alias (default 'LinkChat User'): ";
            getline(cin, s2);
            if (!s2.empty())
                cfg.alias = s2;

            cout << "[OK] Configuration saved.\n";
            continue;
        }

        if (cmd == "chat")
        {
            if (cfg.ifname.empty())
            {
                cerr << "[ERR] please set interface with 'config' first.\n";
                continue;
            }

            SenderConfig scfg{};
            scfg.mtu = cfg.mtu;
            scfg.window = cfg.window;
            scfg.rto_ms = cfg.rto_ms;

            EthConfig ecfg{};
            ecfg.ifname = cfg.ifname;
            ecfg.ether_type = cfg.ethertype;
            ecfg.frame_mtu = static_cast<size_t>(cfg.mtu);

            LinkchatApp app(scfg);

            // Dynamic destination for chat messages; starts with configured MAC
            Mac active_dst_mac{};
            if (!cfg.dst_mac.empty())
                parse_mac(cfg.dst_mac, active_dst_mac);
            mutex dst_mu;

            // Peer registry with TTL
            PeerRegistry peers(chrono::seconds(30));
            // Serialize temporary emit overrides vs normal sends
            mutex emit_swap_mu;

            app.set_on_deliver([&](uint32_t msg_id, Type type, const vector<uint8_t> &data, const Mac &src_mac)
                               {
                                    if (type == Type::HELLO)
                                    {
                                        string alias, peer_mac_ascii;
                                        if (parse_hello_payload(data, alias, peer_mac_ascii))
                                        {
                                            peers.upsert(alias, src_mac, peer_mac_ascii);
                                        }
                                        else
                                        {
                                            // fallback to hardware MAC
                                            string alias2;
                                            if(!data.empty())
                                            {
                                                uint8_t alias_len = data[0];
                                                if(data.size() >= 1 + alias_len)
                                                    alias2.assign(reinterpret_cast<const char*>(&data[1]), alias_len);
                                            }
                                            peers.upsert(alias2, src_mac, mac_to_string(src_mac));
                                        }
                                        return;
                                    }
                                    if (type == Type::FILE) 
                                    {
                                        string sender = mac_to_string(src_mac);
                                        if (auto pi = peers.get_by_mac(src_mac))
                                        {
                                            if (!pi->alias.empty()) sender = pi->alias;
                                        }
                                        string fname;
                                        vector<uint8_t> file_bytes;
                                        if (unwrap_file_with_name(data, fname, file_bytes)) 
                                        {
                                            if (!ensure_dir(cfg.outdir)) 
                                            {
                                                cerr << "\n[WARN] cannot access outdir '" << cfg.outdir << "', using current dir\n> ";
                                            }
                                            auto outpath = (fs::path(cfg.outdir) / fs::path(fname)).string();
                                            if (write_file(outpath, file_bytes)) 
                                            {
                                                cout<< "\n[" << now_hms() << "] [" << sender << "] file recv: saved " << outpath
                                                    << " (" << file_bytes.size() << " bytes)\n> ";
                                            } 
                                            else 
                                            {
                                                cerr << "\n[ERR] failed to save file msg_id=" << msg_id << "\n> ";
                                            }
                                        } 
                                        else 
                                        {
                                            // Compat: si el emisor no empaquetó nombre, guardar genérico
                                            auto outpath = (fs::path(cfg.outdir) / fs::path("file-" + to_string(msg_id) + ".bin")).string();
                                            if (write_file(outpath, data)) 
                                            {
                                                cout << "\n[" << now_hms() << "] [" << sender << "] file recv: saved " << outpath
                                                    << " (" << data.size() << " bytes)\n> ";
                                            } 
                                            else 
                                            {
                                                cerr << "\n[ERR] failed to save file msg_id=" << msg_id << "\n> ";
                                            }
                                        }
                                        return;
                                    }

                                    {
                                        string sender = mac_to_string(src_mac);
                                        if (auto pi = peers.get_by_mac(src_mac))
                                        {
                                            if (!pi->alias.empty()) sender = pi->alias;
                                        }
                                        cout << "\n[" << now_hms() << "] [" << sender << "] "
                                             << string(data.begin(), data.end()) << "\n> ";
                                    } });

            AppEthHandle handle{};
            if (!bind_app_to_eth(app, ecfg, handle))
            {
                cerr << "[ERR] bind failed (eth init / RX thread)\n";
                continue;
            }

            // Use app.send_bytes_to for per-peer delivery; no override needed

            cout << "[chat] connected. Type messages, /sendfile <path> to send file, /quit to exit.\n";

            thread tick_thr([&]()
                            {
                while (g_running.load()) {
                    app.tick();
                    this_thread::sleep_for(10ms);
                } });

            // Helper to build a single-PDU (seq=0,total=1) without reliable sender
            auto build_single_pdu = [&](Type t, const vector<uint8_t>& payload){
                Header h{}; h.type=t; h.msg_id=0; h.seq=0; h.total=1; h.payload_len=static_cast<uint16_t>(payload.size());
                vector<uint8_t> pdu(kHeaderSize + payload.size() + kCrcSize);
                size_t n = build_pdu(h, payload.data(), payload.size(), pdu.data(), pdu.size());
                if (n != pdu.size()) return vector<uint8_t>{};
                return pdu;
            };

            // Background discovery thread: periodic HELLO broadcast
            atomic<bool> disc_run{true};
            thread disc_thr([&]()
                            {
                                const auto period = chrono::seconds(5);
                                const string my_mac_ascii = get_local_mac_ascii(cfg.ifname);
                                while (disc_run.load())
                                {
                                    auto payload = build_hello_payload(cfg.alias, my_mac_ascii);
                                    auto pdu = build_single_pdu(Type::HELLO, payload);
                                    if (!pdu.empty()) {
                                        Mac bcast{}; fill(std::begin(bcast.bytes), std::end(bcast.bytes), 0xFF);
                                        (void)eth_send_pdu_to(bcast, pdu);
                                    }
                                    // sleep until next period
                                    for (int i = 0; i < 50 && disc_run.load(); ++i)
                                        this_thread::sleep_for(chrono::milliseconds(period.count()*20)); // ~5s total
                                }
                            });

            string msg;
            while (getline(cin, msg))
            {
                if (msg == "/quit")
                    break;

                if (msg == "/online")
                {
                    auto list = peers.list_online();
                    cout << "[online] peers: " << list.size() << "\n";
                    auto now = chrono::steady_clock::now();
                    int idx = 1;
                    for (const auto &p : list)
                    {
                        auto age = chrono::duration_cast<chrono::seconds>(now - p.last_seen).count();
                        cout << "  " << idx++ << ") alias=" << (p.alias.empty()?"(anon)":p.alias)
                             << " mac=" << p.mac_ascii
                             << " last=" << age << "s ago\n";
                    }
                    cout << "> ";
                    continue;
                }

                if (msg == "/contacts")
                {
                    auto list = peers.list_all();
                    cout << "[contacts] total: " << list.size() << "\n";
                    auto now = chrono::steady_clock::now();
                    int idx = 1;
                    for (const auto &p : list)
                    {
                        bool online = (now - p.last_seen <= chrono::seconds(30));
                        cout << "  " << idx++ << ") alias=" << (p.alias.empty()?"(anon)":p.alias)
                             << " mac=" << p.mac_ascii
                             << " status=" << (online?"online":"offline") << "\n";
                    }
                    cout << "> ";
                    continue;
                }

                // removed chat subcommand /discover

                if (msg.rfind("/connect ", 0) == 0)
                {
                    string alias = msg.substr(string("/connect ").size());
                    if (alias.empty()) { cerr << "Usage: /connect <alias>\n> "; continue; }
                    auto best = peers.latest_by_alias(alias);
                    if (!best)
                    {
                        cerr << "[ERR] alias not found or offline: " << alias << "\n> ";
                        continue;
                    }
                    {
                        lock_guard<mutex> lk(dst_mu);
                        active_dst_mac = best->mac;
                        cfg.dst_mac = best->mac_ascii; // reflect selection in runtime config
                    }
                    cout << "[connect] now chatting with alias='" << (best->alias.empty()?"(anon)":best->alias)
                         << "' mac=" << best->mac_ascii << "\n> ";
                    continue;
                }

                if (msg.rfind("/sendfile ", 0) == 0)
                {
                    string path = msg.substr(string("/sendfile ").size());
                    {
                        lock_guard<mutex> lk(dst_mu);
                        if (is_zero(active_dst_mac))
                        {
                            cerr << "[ERR] no active peer. Use /connect <alias> (see /online)\n> ";
                            continue;
                        }
                    }
                    vector<uint8_t> bytes;
                    if (!read_file(path, bytes))
                    {
                        cerr << "[ERR] cannot read file: " << path << "\n> ";
                        continue;
                    }
                    auto wrapped = wrap_file_with_name(path, bytes);
                    {
                        lock_guard<mutex> lk(emit_swap_mu);
                        app.send_bytes_to(active_dst_mac, wrapped, Type::FILE);
                    }
                    cout << "[file sent] " << fs::path(path).filename().string()
                         << " (" << bytes.size() << " bytes)\n> ";
                    continue;
                }

                

                {
                    lock_guard<mutex> lk(dst_mu);
                    if (is_zero(active_dst_mac))
                    {
                        cerr << "[ERR] no active peer. Use /connect <alias> (see /online)\n> ";
                        continue;
                    }
                }
                vector<uint8_t> bytes(msg.begin(), msg.end());
                {
                    lock_guard<mutex> lk(emit_swap_mu);
                    app.send_bytes_to(active_dst_mac, bytes, Type::MSG);
                }
                cout << "> ";
            }

            g_running.store(false);
            if (tick_thr.joinable())
                tick_thr.join();
            disc_run.store(false);
            if (disc_thr.joinable())
                disc_thr.join();
            unbind_app_from_eth(handle);
            g_running.store(true);
            continue;
        }

        if (cmd == "groupchat")
        {
            if (cfg.ifname.empty())
            {
                cerr << "[ERR] please set interface with 'config' first.\n";
                continue;
            }

            SenderConfig scfg{};
            scfg.mtu = cfg.mtu;
            scfg.window = cfg.window;
            scfg.rto_ms = cfg.rto_ms;

            EthConfig ecfg{};
            ecfg.ifname = cfg.ifname;
            ecfg.ether_type = cfg.ethertype;
            ecfg.frame_mtu = static_cast<size_t>(cfg.mtu);

            LinkchatApp app(scfg);

            // Peer registry with TTL and helpers
            PeerRegistry peers(chrono::seconds(30));

            app.set_on_deliver([&](uint32_t msg_id, Type type, const vector<uint8_t> &data, const Mac &src_mac)
                               {
                                    if (type == Type::HELLO)
                                    {
                                        string alias, peer_mac_ascii;
                                        if (parse_hello_payload(data, alias, peer_mac_ascii))
                                            peers.upsert(alias, src_mac, peer_mac_ascii);
                                        else
                                        {
                                            string alias2;
                                            if(!data.empty())
                                            {
                                                uint8_t alias_len = data[0];
                                                if(data.size() >= 1 + alias_len)
                                                    alias2.assign(reinterpret_cast<const char*>(&data[1]), alias_len);
                                            }
                                            peers.upsert(alias2, src_mac, mac_to_string(src_mac));
                                        }
                                        return;
                                    }
                                    if (type == Type::FILE)
                                    {
                                        string sender = mac_to_string(src_mac);
                                        if (auto pi = peers.get_by_mac(src_mac))
                                            if (!pi->alias.empty()) sender = pi->alias;
                                        string fname; vector<uint8_t> file_bytes;
                                        if (unwrap_file_with_name(data, fname, file_bytes))
                                        {
                                            if (!ensure_dir(cfg.outdir))
                                                cerr << "\n[WARN] cannot access outdir '" << cfg.outdir << "', using current dir\n> ";
                                            auto outpath = (fs::path(cfg.outdir) / fs::path(fname)).string();
                                            if (write_file(outpath, file_bytes))
                                                cout<< "\n[" << now_hms() << "] [" << sender << "] file recv: saved " << outpath
                                                    << " (" << file_bytes.size() << " bytes)\n> ";
                                            else
                                                cerr << "\n[ERR] failed to save file msg_id=" << msg_id << "\n> ";
                                        }
                                        else
                                        {
                                            auto outpath = (fs::path(cfg.outdir) / fs::path("file-" + to_string(msg_id) + ".bin")).string();
                                            if (write_file(outpath, data))
                                                cout << "\n[" << now_hms() << "] [" << sender << "] file recv: saved " << outpath
                                                     << " (" << data.size() << " bytes)\n> ";
                                            else
                                                cerr << "\n[ERR] failed to save file msg_id=" << msg_id << "\n> ";
                                        }
                                        return;
                                    }
                                    {
                                        string sender = mac_to_string(src_mac);
                                        if (auto pi = peers.get_by_mac(src_mac))
                                            if (!pi->alias.empty()) sender = pi->alias;
                                        cout << "\n[" << now_hms() << "] [" << sender << "] "
                                             << string(data.begin(), data.end()) << "\n> ";
                                    } });

            AppEthHandle handle{};
            if (!bind_app_to_eth(app, ecfg, handle))
            {
                cerr << "[ERR] bind failed (eth init / RX thread)\n";
                continue;
            }

            cout << "[groupchat] connected. Type messages, /sendfile <path> to send file, /quit to exit.\n";

            thread tick_thr([&]()
                            {
                while (g_running.load()) { app.tick(); this_thread::sleep_for(10ms); } });

            // Periodic HELLO broadcast (fire-and-forget)
            atomic<bool> disc_run{true};
            auto build_single_pdu = [&](Type t, const vector<uint8_t>& payload){
                Header h{}; h.type=t; h.msg_id=0; h.seq=0; h.total=1; h.payload_len=static_cast<uint16_t>(payload.size());
                vector<uint8_t> pdu(kHeaderSize + payload.size() + kCrcSize);
                size_t n = build_pdu(h, payload.data(), payload.size(), pdu.data(), pdu.size());
                if (n != pdu.size()) return vector<uint8_t>{};
                return pdu;
            };
            thread disc_thr([&]()
                            {
                const auto period = chrono::seconds(5);
                const string my_mac_ascii = get_local_mac_ascii(cfg.ifname);
                while (disc_run.load())
                {
                    auto payload = build_hello_payload(cfg.alias, my_mac_ascii);
                    auto pdu = build_single_pdu(Type::HELLO, payload);
                    if (!pdu.empty()) { Mac bcast{}; fill(std::begin(bcast.bytes), std::end(bcast.bytes), 0xFF); (void)eth_send_pdu_to(bcast, pdu); }
                    for (int i = 0; i < 50 && disc_run.load(); ++i)
                        this_thread::sleep_for(chrono::milliseconds(period.count()*20)); // ~5s total
                }
            });

            // Group recipients (MAC ascii). If empty => send to all online by default
            unordered_set<string> group_macs;

            string msg;
            while (getline(cin, msg))
            {
                if (msg == "/quit") break;

                if (msg.rfind("/members", 0) == 0)
                {
                    string rest = msg.substr(string("/members").size());
                    while (!rest.empty() && isspace(static_cast<unsigned char>(rest.front()))) rest.erase(rest.begin());
                    stringstream ss2(rest);
                    string sub; ss2 >> sub;
                    if (sub.empty() || sub == "list")
                    {
                        cout << "[members]" << (group_macs.empty()?" (using all online by default)":"") << "\n";
                        auto list = peers.list_all();
                        auto now = chrono::steady_clock::now();
                        int idx = 1;
                        for (const auto &p : list)
                        {
                            bool in_group = (group_macs.find(p.mac_ascii) != group_macs.end());
                            bool online = (now - p.last_seen <= chrono::seconds(30));
                            cout << "  " << idx++ << ") alias=" << (p.alias.empty()?"(anon)":p.alias)
                                 << " mac=" << p.mac_ascii
                                 << " status=" << (online?"online":"offline")
                                 << (in_group?" [member]":"") << "\n";
                        }
                        cout << "> ";
                        continue;
                    }
                    if (sub == "add")
                    {
                        string alias; ss2 >> std::ws; getline(ss2, alias);
                        if (alias.empty()) { cerr << "Usage: /members add <alias>\n> "; continue; }
                        auto best = peers.latest_by_alias(alias);
                        if (!best) { cerr << "[ERR] alias not found: " << alias << "\n> "; continue; }
                        group_macs.insert(best->mac_ascii);
                        cout << "[members] added alias='" << (best->alias.empty()?"(anon)":best->alias) << "' mac=" << best->mac_ascii << "\n> ";
                        continue;
                    }
                    if (sub == "del")
                    {
                        string token; ss2 >> std::ws; getline(ss2, token);
                        if (token.empty()) { cerr << "Usage: /members del <alias|mac>\n> "; continue; }
                        bool removed = false;
                        // try mac exact
                        if (group_macs.erase(token) > 0) removed = true;
                        // try by alias (remove any matches)
                        auto all = peers.list_all();
                        for (const auto &p : all)
                        {
                            if (p.alias == token)
                                removed = (group_macs.erase(p.mac_ascii) > 0) || removed;
                        }
                        if (removed) cout << "[members] removed '" << token << "'\n> "; else cerr << "[ERR] not in members: '" << token << "'\n> ";
                        continue;
                    }
                    if (sub == "clear")
                    {
                        group_macs.clear();
                        cout << "[members] cleared (now using all online by default)\n> ";
                        continue;
                    }
                    cerr << "Usage: /members list | add <alias> | del <alias|mac> | clear\n> ";
                    continue;
                }

                if (msg == "/online")
                {
                    auto list = peers.list_online();
                    cout << "[online] peers: " << list.size() << "\n";
                    auto now = chrono::steady_clock::now();
                    int idx = 1;
                    for (const auto &p : list)
                    {
                        auto age = chrono::duration_cast<chrono::seconds>(now - p.last_seen).count();
                        cout << "  " << idx++ << ") alias=" << (p.alias.empty()?"(anon)":p.alias)
                             << " mac=" << p.mac_ascii
                             << " last=" << age << "s ago\n";
                    }
                    cout << "> ";
                    continue;
                }

                if (msg == "/contacts")
                {
                    auto list = peers.list_all();
                    cout << "[contacts] total: " << list.size() << "\n";
                    auto now = chrono::steady_clock::now();
                    int idx = 1;
                    for (const auto &p : list)
                    {
                        bool online = (now - p.last_seen <= chrono::seconds(30));
                        cout << "  " << idx++ << ") alias=" << (p.alias.empty()?"(anon)":p.alias)
                             << " mac=" << p.mac_ascii
                             << " status=" << (online?"online":"offline") << "\n";
                    }
                    cout << "> ";
                    continue;
                }

                if (msg.rfind("/sendfile ", 0) == 0)
                {
                    string path = msg.substr(string("/sendfile ").size());
                    vector<uint8_t> bytes;
                    if (!read_file(path, bytes))
                    {
                        cerr << "[ERR] cannot read file: " << path << "\n> ";
                        continue;
                    }
                    auto wrapped = wrap_file_with_name(path, bytes);
                    auto on = peers.list_online();
                    vector<PeerInfo> targets;
                    if (group_macs.empty())
                        targets = move(on);
                    else
                    {
                        for (const auto &p : on)
                            if (group_macs.find(p.mac_ascii) != group_macs.end()) targets.push_back(p);
                    }
                    if (targets.empty()) { cerr << "[ERR] no online members. Try later.\n> "; continue; }
                    for (const auto &p : targets)
                        app.send_bytes_to(p.mac, wrapped, Type::FILE);
                    cout << "[file sent] " << fs::path(path).filename().string() << " to " << targets.size() << " member(s)\n> ";
                    continue;
                }

                // send text to all online
                auto on = peers.list_online();
                vector<PeerInfo> targets;
                if (group_macs.empty())
                    targets = move(on);
                else
                {
                    for (const auto &p : on)
                        if (group_macs.find(p.mac_ascii) != group_macs.end()) targets.push_back(p);
                }
                if (targets.empty()) { cerr << "[ERR] no online members. Try later.\n> "; continue; }
                vector<uint8_t> bytes(msg.begin(), msg.end());
                for (const auto &p : targets)
                    app.send_bytes_to(p.mac, bytes, Type::MSG);
                cout << "> ";
            }

            g_running.store(false);
            if (tick_thr.joinable()) tick_thr.join();
            disc_run.store(false);
            if (disc_thr.joinable()) disc_thr.join();
            unbind_app_from_eth(handle);
            g_running.store(true);
            continue;
        }

        if (cmd == "send")
        {
            string path;
            ss >> path;
            if (path.empty())
            {
                cerr << "Usage: send <file>\n";
                continue;
            }
            if (cfg.ifname.empty() || cfg.dst_mac.empty())
            {
                cerr << "[ERR] please run 'config' first.\n";
                continue;
            }

            SenderConfig scfg{};
            scfg.mtu = cfg.mtu;
            scfg.window = cfg.window;
            scfg.rto_ms = cfg.rto_ms;

            EthConfig ecfg{};
            ecfg.ifname = cfg.ifname;
            ecfg.ether_type = cfg.ethertype;
            ecfg.frame_mtu = static_cast<size_t>(cfg.mtu);
            if (!parse_mac(cfg.dst_mac, ecfg.dst_mac))
            {
                cerr << "[ERR] invalid destination MAC format.\n";
                continue;
            }

            LinkchatApp app(scfg);
            AppEthHandle handle{};
            if (!bind_app_to_eth(app, ecfg, handle))
            {
                cerr << "[ERR] bind failed\n";
                continue;
            }

            vector<uint8_t> bytes;
            if (!read_file(path, bytes))
            {
                cerr << "[ERR] cannot read file: " << path << "\n";
                unbind_app_from_eth(handle);
                continue;
            }
            auto wrapped = wrap_file_with_name(path, bytes);
            app.send_bytes_to(ecfg.dst_mac, wrapped, Type::FILE);
            cout << "[file sent] " << fs::path(path).filename().string() << " (" << bytes.size() << " bytes)\n";

            auto until = chrono::steady_clock::now() + 1500ms;
            while (chrono::steady_clock::now() < until)
            {
                app.tick();
                this_thread::sleep_for(10ms);
            }
            unbind_app_from_eth(handle);
            continue;
        }

        if (cmd == "discover")
        {
            if (cfg.ifname.empty())
            {
                cerr << "[ERR] please run 'config' first.\n";
                continue;
            }

            linkchat::SenderConfig scfg{};
            scfg.mtu = cfg.mtu;
            scfg.window = cfg.window;
            scfg.rto_ms = cfg.rto_ms;
            linkchat::EthConfig ecfg{};
            ecfg.ifname = cfg.ifname;
            ecfg.ether_type = cfg.ethertype;
            ecfg.frame_mtu = (size_t)cfg.mtu;

            if (!linkchat::parse_mac("ff:ff:ff:ff:ff:ff", ecfg.dst_mac))
            {
                cerr << "[ERR] cannot parse broadcast MAC\n";
                continue;
            }

            linkchat::LinkchatApp app(scfg);
            // Reutiliza el mismo set_on_deliver de arriba si está en scope; si no, registra uno local simplificado
            app.set_on_deliver([&](uint32_t, linkchat::Type type, const vector<uint8_t> &data, const linkchat::Mac &src)
                               {
                                    if (type != linkchat::Type::HELLO) return;
                                    string nick; if (!data.empty()) {
                                    auto nlen = data[0];
                                    if (data.size() >= 1 + nlen) nick.assign((const char*)&data[1], nlen);
                                }
            cout<< "[discover] " << (nick.empty() ? "(anon)" : nick)
                << "  mac=" << linkchat::mac_to_string(src) << "\n"; });

            linkchat::AppEthHandle h{};
            if (!linkchat::bind_app_to_eth(app, ecfg, h))
            {
                cerr << "[ERR] bind failed\n";
                continue;
            }

            // build HELLO with alias and real local MAC in ascii
            string my_mac_ascii = get_local_mac_ascii(cfg.ifname);
            auto payload = build_hello_payload(cfg.alias, my_mac_ascii);
            // send one-shot HELLO via broadcast (no reliable sender)
            auto build_single_pdu = [&](linkchat::Type t, const vector<uint8_t>& pl){
                linkchat::Header h{}; h.type=t; h.msg_id=0; h.seq=0; h.total=1; h.payload_len=static_cast<uint16_t>(pl.size());
                vector<uint8_t> pdu(linkchat::kHeaderSize + pl.size() + linkchat::kCrcSize);
                size_t n = linkchat::build_pdu(h, pl.data(), pl.size(), pdu.data(), pdu.size());
                if (n != pdu.size()) return vector<uint8_t>{};
                return pdu;
            };
            {
                linkchat::Mac bcast{}; fill(std::begin(bcast.bytes), std::end(bcast.bytes), 0xFF);
                auto pdu = build_single_pdu(linkchat::Type::HELLO, payload);
                if (!pdu.empty()) (void)linkchat::eth_send_pdu_to(bcast, pdu);
            }

            cout << "[discover] HELLO broadcast sent (nick=" << cfg.alias
                 << ", mac=" << (my_mac_ascii.empty() ? "unknown" : my_mac_ascii) << "). Listening 10s...\n";
            auto end = chrono::steady_clock::now() + chrono::seconds(10);
            while (chrono::steady_clock::now() < end)
            {
                // periodically re-announce to catch late listeners
                linkchat::Mac bcast{}; fill(std::begin(bcast.bytes), std::end(bcast.bytes), 0xFF);
                auto pdu = build_single_pdu(linkchat::Type::HELLO, payload);
                if (!pdu.empty()) (void)linkchat::eth_send_pdu_to(bcast, pdu);
                for (int i = 0; i < 100; i++)
                {
                    app.tick();
                    this_thread::sleep_for(chrono::milliseconds(10));
                }
            }
            linkchat::unbind_app_from_eth(h);
            cout << "[discover] done. Set peer MAC in 'config' and use 'chat'.\n";
            continue;
        }

        cout << "Unknown command. Type 'help' for commands.\n";
    }

    cout << "Bye.\n";
    return 0;
}
