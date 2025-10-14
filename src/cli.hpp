#pragma once
#include <string>
#include <cstdint>

enum class CliCmd {
    None,
    Config,
    Chat,
    Send,
    Info,
    Exit
};

struct RuntimeConfig {
    std::string ifname;      // interface (eth0, wlo1...)
    std::string dst_mac;     // MAC dst
    std::string outdir   = "inbox"; //  downloads here 
    std::string alias = "LinkChat User"; //user alias 
    int         mtu      = 1500;
    int         window   = 1;
    int         rto_ms   = 300;
    uint16_t    ethertype = 0x88B5;
};

int run_cli();  
