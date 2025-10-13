#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <unistd.h>
#include <poll.h>
#include <netinet/in.h>

#include <atomic>
#include <vector>
#include <string>
#include <cstring>
#include <fstream>

#include "eth_adapter.hpp"
#include "../util/mac.hpp"

using namespace std;

namespace linkchat
{
    static int g_rx_fd = -1;
    static int g_tx_fd = -1;
    static atomic<bool> g_running{false};
    static int g_ifindex = -1;
    static EthConfig g_cfg;
    static bool is_open() noexcept
    {

        return (g_rx_fd >= 0 && g_tx_fd >= 0);
    }

    static bool read_sysfs_ifindex(const string &ifname, int &out_ifindex) noexcept
    {
        string path = "/sys/class/net/" + ifname + "/ifindex";
        ifstream file(path);
        if (!file)
            return false;
        string line;
        getline(file, line);
        if (line.empty())
            return false;
        while (!line.empty() && isspace((unsigned char)line.back()))
            line.pop_back();
        char *end = nullptr;
        long v = strtol(line.c_str(), &end, 10);
        if (!(v > 0 && v <= INT_MAX && *end == '\0'))
            return false;
        out_ifindex = static_cast<int>(v);
        return true;
    }

    static bool read_sysfs_mac(const string &ifname, Mac &out_mac) noexcept
    {
        string path = "/sys/class/net/" + ifname + "/address";
        ifstream file(path);
        if (!file)
            return false;
        string line;
        getline(file, line);
        if (line.empty())
            return false;
        while (!line.empty() && isspace((unsigned char)line.back()))
            line.pop_back();

        return parse_mac(line, out_mac);
    }

    static bool read_sysfs_mtu(const string &ifname, size_t &out_mtu) noexcept
    {
        string path = "/sys/class/net/" + ifname + "/mtu";
        ifstream file(path);
        if (!file)
            return false;
        string line;
        getline(file, line);
        if (line.empty())
            return false;
        while (!line.empty() && isspace((unsigned char)line.back()))
            line.pop_back();
        char *end = nullptr;
        long v = strtol(line.c_str(), &end, 10);
        if (!(v > 0 && v <= SIZE_MAX && *end == '\0'))
            return false;
        out_mtu = static_cast<size_t>(v);
        return true;
    }

    static size_t build_eth_header(uint8_t *dst, const Mac &dst_mac, const Mac &src_mac, uint16_t ether_type) noexcept
    {
        for (int i = 0; i < 6; i++)
            dst[i] = dst_mac.bytes[i];
        for (int i = 0; i < 6; i++)
            dst[6 + i] = src_mac.bytes[i];
        dst[12] = (ether_type >> 8) & 0xFF;
        dst[13] = ether_type & 0xFF;
        return 14;
    }

    bool eth_init(const EthConfig &cfg) noexcept
    {
        if (cfg.ifname.empty() || cfg.ether_type == 0)
            return false;
        if (g_running.load() || g_rx_fd >= 0 || g_tx_fd >= 0)
            return false;

        int ifindex = -1;
        if (!read_sysfs_ifindex(cfg.ifname, ifindex) || ifindex <= 0)
            return false;

        Mac src = cfg.src_mac;
        if (is_zero(src))
        {
            if (!read_sysfs_mac(cfg.ifname, src))
                return false;
        }

        size_t mtu_sys = 0;
        if (!read_sysfs_mtu(cfg.ifname, mtu_sys) || mtu_sys < 64)
            return false;
        size_t frame_mtu = (cfg.frame_mtu == 0) ? mtu_sys : min(cfg.frame_mtu, mtu_sys);

        int rxfd = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (rxfd < 0)
            return false;

        sockaddr_ll sll{};
        sll.sll_family = AF_PACKET;
        sll.sll_protocol = htons(ETH_P_ALL);
        sll.sll_ifindex = ifindex;

        if (::bind(rxfd, reinterpret_cast<sockaddr *>(&sll), sizeof(sll)) < 0)
        {
            ::close(rxfd);
            return false;
        }

        int txfd = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (txfd < 0)
        {
            ::close(rxfd);
            return false;
        }
        if (::bind(txfd, reinterpret_cast<sockaddr *>(&sll), sizeof(sll)) < 0)
        {
            ::close(rxfd);
            ::close(txfd);
            return false;
        }

        g_rx_fd = rxfd;
        g_tx_fd = txfd;
        g_ifindex = ifindex;
        g_cfg = cfg;
        g_cfg.src_mac = src;
        g_cfg.frame_mtu = frame_mtu;

        g_running = true;
        return true;
    }

    bool eth_send_pdu(const vector<uint8_t> &pdu) noexcept
    {
        if (!g_running.load() || g_tx_fd < 0)
            return false;

        if (pdu.empty())
            return false;

        if (pdu.size() > g_cfg.frame_mtu)
            return false;

        size_t frame_len = kEthHdr + pdu.size();
        if (frame_len < 60)
            frame_len = 60; 

        vector<uint8_t> frame(frame_len, 0);
        build_eth_header(frame.data(), g_cfg.dst_mac, g_cfg.src_mac, g_cfg.ether_type);
        if (!pdu.empty())
        {
            for (int i = 0; i < pdu.size(); i++)
                frame[kEthHdr + i] = pdu[i];
        }

        sockaddr_ll to{};
        to.sll_family = AF_PACKET;
        to.sll_protocol = htons(ETH_P_ALL);
        to.sll_ifindex = g_ifindex;
        to.sll_halen = 6;
        for (int i = 0; i < 6; i++)
            to.sll_addr[i] = g_cfg.dst_mac.bytes[i];

        const ssize_t sent = ::sendto(g_tx_fd,
                                      frame.data(),
                                      static_cast<int>(frame.size()),
                                      0,
                                      reinterpret_cast<sockaddr *>(&to),
                                      static_cast<socklen_t>(sizeof(to)));

        return (sent == static_cast<ssize_t>(frame.size()));
    }

    void eth_rx_loop(function<void(const uint8_t *, size_t)> on_pdu) noexcept
    {
        if (g_rx_fd < 0)
            return;
        if (!on_pdu)
        {
            on_pdu = [](const uint8_t *, size_t) {};
        }

        const size_t bufcap = max<size_t>(g_cfg.frame_mtu + 64, 2048);
        vector<uint8_t> buf(bufcap);

        pollfd pfd;
        pfd.fd = g_rx_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        while (g_running.load())
        {
            pfd.revents = 0;
            int pr = ::poll(&pfd, 1, 250);
            if (pr < 0)
            {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (pr == 0)
                continue;
            if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
                break;
            if ((pfd.revents & POLLIN) == 0)
                continue;

            sockaddr_ll src{};
            socklen_t alen = sizeof(src);
            const ssize_t rcv = ::recvfrom(g_rx_fd,
                                           buf.data(),
                                           static_cast<int>(buf.size()),
                                           0,
                                           reinterpret_cast<sockaddr *>(&src),
                                           &alen);
            if (rcv <= 0)
            {
                if (rcv < 0 && errno == EINTR)
                    continue;
                break;
            }

            size_t len = static_cast<size_t>(rcv);
            if (len < kEthHdr)
                continue;

            const uint16_t et = (static_cast<uint16_t>(buf[12]) << 8) | (static_cast<uint16_t>(buf[13]));
            const int pkttype = src.sll_pkttype;

            if (et != g_cfg.ether_type)
                continue;

            if (pkttype != PACKET_HOST && pkttype != PACKET_BROADCAST && pkttype != PACKET_MULTICAST && pkttype != PACKET_OUTGOING)
                continue;

            Mac dst{};
             for (int i = 0; i < 6; ++i)
                 dst.bytes[i] = buf[0 + i];

            if (!(dst == g_cfg.src_mac) && !is_broadcast(dst) && pkttype != PACKET_OUTGOING)
                continue;

            const uint8_t *payload_ptr = buf.data() + kEthHdr;
            const size_t payload_len = len - kEthHdr;

            on_pdu(payload_ptr, payload_len);
        }
    }

    void eth_shutdown() noexcept
    {
        g_running = false;
        if (g_tx_fd >= 0 || g_rx_fd >= 0)
        {
            ::close(g_tx_fd);
            ::close(g_rx_fd);
            g_tx_fd = -1;
            g_rx_fd = -1;
        }
        g_ifindex = -1;
        g_cfg = {};
    }

}
