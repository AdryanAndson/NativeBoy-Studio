// Link cable over the network (TCP). One side hosts, the other connects; each byte the
// clock-driving side sends is answered with the partner's byte, like the real cable.
// Second generation: the host's router port can be opened automatically (UPnP), the address is
// shared as an invite code, players on the same network find each other, and the connection
// shows its delay and warns when the two players run different games.
#pragma once
#include "../core/gb.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// a game being hosted on the local network (found by its broadcast)
struct LanGame { std::string ip, name; int port = 0; uint32_t game = 0; double seen = 0; };

class NetLink : public gb::SerialLink {
public:
    NetLink();
    ~NetLink() override;
    bool host(int port, std::string& err);                       // wait for a partner on this port
    bool connect_to(const std::string& host, int port, std::string& err);
    void close();
    void update();              // accepts a waiting partner, pings, announces on the local network (call once per frame)
    int status() const;         // 0 = off, 1 = waiting for partner, 2 = connected
    std::string status_text() const;
    unsigned long long bytes_exchanged() const { return exchanged; }

    gb::u8 exchange_master(gb::Machine& m, gb::u8 out) override;
    void poll(gb::Machine& m) override;
    int timeout_ms = 400;       // how long the clock-driving side waits for an answer

    // which game this side runs (CRC of the ROM) and its name, sent to the partner and on the local network
    void set_game(uint32_t crc, const std::string& name) { game_crc = crc; game_name = name; }
    bool partner_game_differs() const { return partner_game_known && partner_crc != game_crc; }
    int ping_ms() const { return rtt_ms; }                       // -1 = not measured yet

    // invite code: the address and port in a short text ("NB-XXXX-XXXX-X") to send to a friend
    static std::string invite_code(const std::string& ip, int port);
    static bool parse_invite(const std::string& text, std::string& ip, int& port);   // also accepts "1.2.3.4[:port]"
    static std::vector<std::string> local_ips();                 // addresses of this computer on the local network

    // local network: while hosting this side announces itself; the other side lists who is hosting
    bool lan_scan_start();
    void lan_scan_stop();
    std::vector<LanGame> lan_games() const;                      // only those running the same game

    // router port (UPnP), done in the background: 0 not tried, 1 working, 2 open, 3 failed
    void upnp_open_async(int port);
    int upnp_state() const { return upnp_st.load(); }
    std::string upnp_message() const;
    std::string external_ip() const;                             // "" if unknown
    static bool is_private_ip(const std::string& ip);

private:
    long long listen_fd = -1, fd = -1, lan_fd = -1, beacon_fd = -1;
    std::string peer, last_error;
    unsigned long long exchanged = 0;
    uint32_t game_crc = 0, partner_crc = 0; std::string game_name;
    bool partner_game_known = false; int crc_got = 0; uint32_t crc_acc = 0;
    int host_port = 0;
    double last_ping = 0, last_beacon = 0; int ping_seq = 0, rtt_ms = -1; double ping_sent_at[256] = {};
    bool hello_sent = false;
    std::vector<LanGame> lan;
    bool send2(gb::u8 type, gb::u8 v);
    int recv2(gb::u8& type, gb::u8& v, int wait_ms);   // 1 = got message, 0 = nothing, -1 = closed
    bool side_message(gb::u8 t, gb::u8 v);              // pings and game check; true if it was one of them
    void drop(const std::string& why);
    void lan_receive();
    // UPnP
    std::atomic<int> upnp_st{0};
    mutable std::mutex upnp_mx;
    std::string upnp_msg, upnp_ext_ip, upnp_ctrl, upnp_service, upnp_internal;
    int upnp_port = 0;
    std::thread upnp_thread;
    void upnp_close();
};

// Default TCP port of the NativeBoy link cable, and the UDP port used to find players on the local network
static const int NB_LINK_PORT = 5738;
static const int NB_LAN_PORT = 5739;
