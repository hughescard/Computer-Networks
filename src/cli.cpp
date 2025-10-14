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
            send <path>           Send a file directly and return to prompt
            discover              Send HELLO packet to discover peers
            info                  Show current configuration
            exit                  Quit

            While in chat:
            Type messages and press Enter to send
            Use /sendfile <path> to send files
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
}

int run_cli()
{
    using namespace linkchat;

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

            cout << "Destination MAC: ";
            getline(cin, cfg.dst_mac);

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

            app.set_on_deliver([&](uint32_t msg_id, Type type, const vector<uint8_t> &data, const Mac &src_mac)
                               {
                                    if (type == Type::HELLO)
                                    {
                                        string alias, peer_mac_ascii;
                                        if (parse_hello_payload(data, alias, peer_mac_ascii))
                                        {
                                            cout << "\n[hello] peer=" << (alias.empty() ? "LinkChat User" : alias)
                                                 << " mac=" << peer_mac_ascii << "\n";
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
                                            cout << "\n[hello] peer=" << (alias2.empty() ? "LinkChat User" : alias2)
                                                 << " mac=" << mac_to_string(src_mac) << "\n";
                                        }
                                        return;
                                    }
                                    if (type == Type::FILE) 
                                    {
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
                                                cout<< "\n[file recv] saved " << outpath
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
                                                cout << "\n[file recv] saved " << outpath
                                                    << " (" << data.size() << " bytes)\n> ";
                                            } 
                                            else 
                                            {
                                                cerr << "\n[ERR] failed to save file msg_id=" << msg_id << "\n> ";
                                            }
                                        }
                                        return;
                                    }

                                    cout << "\n[" << msg_id << "] " << string(data.begin(), data.end()) << "\n> "; });

            AppEthHandle handle{};
            if (!bind_app_to_eth(app, ecfg, handle))
            {
                cerr << "[ERR] bind failed (eth init / RX thread)\n";
                continue;
            }

            cout << "[chat] connected. Type messages, /sendfile <path> to send file, /quit to exit.\n";

            thread tick_thr([&]()
                            {
                while (g_running.load()) {
                    app.tick();
                    this_thread::sleep_for(10ms);
                } });

            string msg;
            while (getline(cin, msg))
            {
                if (msg == "/quit")
                    break;

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
                    app.send_bytes(wrapped, Type::FILE);
                    cout << "[file sent] " << fs::path(path).filename().string()
                         << " (" << bytes.size() << " bytes)\n> ";
                    continue;
                }

                vector<uint8_t> bytes(msg.begin(), msg.end());
                app.send_bytes(bytes, Type::MSG);
                cout << "> ";
            }

            g_running.store(false);
            if (tick_thr.joinable())
                tick_thr.join();
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
            app.send_bytes(wrapped, Type::FILE);
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
            app.send_bytes(payload, Type::HELLO);

            cout << "[discover] HELLO broadcast sent (nick=" << cfg.alias
                 << ", mac=" << (my_mac_ascii.empty() ? "unknown" : my_mac_ascii) << "). Listening 10s...\n";
            auto end = chrono::steady_clock::now() + chrono::seconds(10);
            while (chrono::steady_clock::now() < end)
            {
                app.send_hello(cfg.alias); 
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
