#include "link.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
static const long long BAD = (long long)INVALID_SOCKET;
static void sclose(long long s) { closesocket((SOCKET)s); }
static void set_nonblock(long long s, bool nb) { u_long m = nb ? 1 : 0; ioctlsocket((SOCKET)s, FIONBIO, &m); }
static bool would_block() { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
typedef int sock_t;
static const long long BAD = -1;
static void sclose(long long s) { ::close((int)s); }
static void set_nonblock(long long s, bool nb) { int f = fcntl((int)s, F_GETFL, 0); fcntl((int)s, F_SETFL, nb ? (f | O_NONBLOCK) : (f & ~O_NONBLOCK)); }
static bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
#endif

using namespace gb;

static double now_s() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
// waits until the socket can be read (or written); false on timeout
static bool wait_fd(long long s, int ms, bool write = false) {
    fd_set set; FD_ZERO(&set); FD_SET((sock_t)s, &set);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    int r = write ? select((int)s + 1, nullptr, &set, nullptr, &tv) : select((int)s + 1, &set, nullptr, nullptr, &tv);
    return r == 1;
}

NetLink::NetLink() {
#ifdef _WIN32
    static bool init = false;
    if (!init) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); init = true; }
#endif
}
NetLink::~NetLink() {
    close();
    lan_scan_stop();
    if (upnp_thread.joinable()) upnp_thread.join();
    upnp_close();
}

static void tune(long long s) {
    int one = 1;
    setsockopt((sock_t)s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);   // one byte at a time: no batching
}

bool NetLink::host(int port, std::string& err) {
    close();
    long long s = (long long)socket(AF_INET, SOCK_STREAM, 0);
    if (s == BAD) { err = "Não foi possível criar a conexão de rede."; return false; }
    int one = 1;
    setsockopt((sock_t)s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind((sock_t)s, (sockaddr*)&a, sizeof a) != 0 || listen((sock_t)s, 1) != 0) {
        sclose(s); err = "A porta " + std::to_string(port) + " já está em uso ou foi bloqueada (firewall)."; return false;
    }
    set_nonblock(s, true);
    listen_fd = s;
    host_port = port;
    // announces itself on the local network, so a partner on the same Wi-Fi finds it without typing anything
    long long b = (long long)socket(AF_INET, SOCK_DGRAM, 0);
    if (b != BAD) { setsockopt((sock_t)b, SOL_SOCKET, SO_BROADCAST, (const char*)&one, sizeof one); beacon_fd = b; }
    last_beacon = 0;
    return true;
}

bool NetLink::connect_to(const std::string& host, int port, std::string& err) {
    close();
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) { err = "Endereço não encontrado: " + host; return false; }
    long long s = (long long)socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == BAD) { freeaddrinfo(res); err = "Não foi possível criar a conexão de rede."; return false; }
    set_nonblock(s, true);
    int r = ::connect((sock_t)s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if (r != 0 && !would_block()) { sclose(s); err = "Não foi possível conectar a " + host + "."; return false; }
    bool ok = wait_fd(s, 5000, true);          // up to 5 s
    int soerr = 0; socklen_t len = sizeof soerr;
    if (ok) getsockopt((sock_t)s, SOL_SOCKET, SO_ERROR, (char*)&soerr, &len);
    if (!ok || soerr) {
        sclose(s);
        err = "Ninguém respondeu em " + host + ":" + std::to_string(port) + ". O outro jogador precisa escolher \"Hospedar\" primeiro";
        err += is_private_ip(host) ? "." : ", e a porta dele precisa estar aberta no roteador (o botão \"Abrir a porta no roteador\" tenta fazer isso sozinho).";
        return false;
    }
    set_nonblock(s, false);
    tune(s);
    fd = s; peer = host;
    hello_sent = false; partner_game_known = false; crc_got = 0; rtt_ms = -1; last_ping = 0;
    return true;
}

void NetLink::close() {
    if (fd != BAD) sclose(fd);
    if (listen_fd != BAD) sclose(listen_fd);
    if (beacon_fd != BAD) sclose(beacon_fd);
    fd = listen_fd = beacon_fd = BAD;
    partner_game_known = false; crc_got = 0; rtt_ms = -1; hello_sent = false;
}

void NetLink::update() {
    const double t = now_s();
    if (lan_fd != BAD) lan_receive();
    if (listen_fd != BAD && fd == BAD) {
        // announce: "NBLINK1|crc|port|name" to the whole local network (and this computer, for two windows)
        if (beacon_fd != BAD && t - last_beacon >= 1.0) {
            last_beacon = t;
            char msg[160]; snprintf(msg, sizeof msg, "NBLINK1|%08X|%d|%s", game_crc, host_port, game_name.substr(0, 100).c_str());
            sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)NB_LAN_PORT);
            a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            sendto((sock_t)beacon_fd, msg, (int)strlen(msg), 0, (sockaddr*)&a, sizeof a);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            sendto((sock_t)beacon_fd, msg, (int)strlen(msg), 0, (sockaddr*)&a, sizeof a);
        }
        sockaddr_in a{}; socklen_t len = sizeof a;
        long long s = (long long)accept((sock_t)listen_fd, (sockaddr*)&a, &len);
        if (s != BAD) {
            set_nonblock(s, false);
            tune(s);
            char ip[64] = "?"; inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
            peer = ip;
            fd = s;
            sclose(listen_fd); listen_fd = BAD;
            if (beacon_fd != BAD) { sclose(beacon_fd); beacon_fd = BAD; }
            hello_sent = false; partner_game_known = false; crc_got = 0; rtt_ms = -1; last_ping = 0;
        }
    }
    if (fd != BAD) {
        if (!hello_sent) {        // which game this side runs (4 bytes of its CRC)
            hello_sent = true;
            for (int i = 0; i < 4; i++) send2('G', (u8)(game_crc >> (i * 8)));
        }
        if (t - last_ping >= 1.0) {
            last_ping = t;
            ping_seq = (ping_seq + 1) & 255;
            ping_sent_at[ping_seq] = t;
            send2('P', (u8)ping_seq);
        }
    }
}

int NetLink::status() const { return fd != BAD ? 2 : listen_fd != BAD ? 1 : 0; }

std::string NetLink::status_text() const {
    if (fd != BAD) {
        std::string s = "Conectado a " + peer;
        if (rtt_ms >= 0) s += " (atraso " + std::to_string(rtt_ms) + " ms)";
        s += ", " + std::to_string(exchanged) + " bytes trocados";
        return s;
    }
    if (listen_fd != BAD) return "Aguardando o outro jogador na porta " + std::to_string(host_port) + "...";
    return last_error.empty() ? "Desconectado" : "Desconectado: " + last_error;
}

void NetLink::drop(const std::string& why) {
    last_error = why;
    if (fd != BAD) sclose(fd);
    fd = BAD;
}

bool NetLink::send2(u8 type, u8 v) {
    if (fd == BAD) return false;
    char b[2] = {(char)type, (char)v};
    int sent = 0;
    while (sent < 2) {
        int r = (int)send((sock_t)fd, b + sent, 2 - sent, 0);
        if (r <= 0) { drop("a conexão caiu"); return false; }
        sent += r;
    }
    return true;
}

int NetLink::recv2(u8& type, u8& v, int wait_ms) {
    if (fd == BAD) return -1;
    if (!wait_fd(fd, wait_ms)) return 0;
    char b[2]; int got = 0;
    while (got < 2) {
        int n = (int)recv((sock_t)fd, b + got, 2 - got, 0);
        if (n <= 0) { drop("o outro jogador desconectou"); return -1; }
        got += n;
    }
    type = (u8)b[0]; v = (u8)b[1];
    return 1;
}

// messages that are not cable bytes: delay measurement and which game the partner runs
// (older versions ignore them, so they still connect)
bool NetLink::side_message(u8 t, u8 v) {
    if (t == 'P') { send2('Q', v); return true; }
    if (t == 'Q') { if (ping_sent_at[v] > 0) rtt_ms = (int)((now_s() - ping_sent_at[v]) * 1000 + 0.5); return true; }
    if (t == 'G') {
        crc_acc = crc_got == 0 ? v : crc_acc | ((uint32_t)v << (crc_got * 8));
        if (++crc_got == 4) { partner_crc = crc_acc; partner_game_known = true; crc_got = 0; }
        return true;
    }
    return false;
}

// Our side drives the clock: send the byte and wait for the partner's byte.
u8 NetLink::exchange_master(Machine& m, u8 out) {
    (void)m;
    if (fd == BAD) return 0xFF;
    if (!send2('M', out)) return 0xFF;
    auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        int left = timeout_ms - (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (left <= 0) return 0xFF;                    // partner too slow: the line reads as idle (high)
        u8 t, v;
        int r = recv2(t, v, left);
        if (r < 0) return 0xFF;
        if (r == 0) continue;
        if (t == 'R') { exchanged++; return v; }
        if (t == 'M') send2('R', 0xFF);                // both sides drive the clock: neither receives anything
        else side_message(t, v);
    }
}

// The partner may be driving the clock: answer each byte it sends.
void NetLink::poll(Machine& m) {
    for (int i = 0; i < 64; i++) {
        u8 t, v;
        if (recv2(t, v, 0) != 1) return;
        if (t == 'M') { u8 back = m.serial_slave_exchange(v); send2('R', back); exchanged++; }
        else side_message(t, v);
        // a late 'R' (after a timeout) is dropped
    }
}

// ------------------------------------------------------------------ invite codes
// 4 bytes of address + 2 of port = 48 bits, written with 10 letters/digits (no I, L, O, U) and 1 check letter
static const char* B32 = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
std::string NetLink::invite_code(const std::string& ip, int port) {
    in_addr a{};
    if (inet_pton(AF_INET, ip.c_str(), &a) != 1) return "";
    uint64_t v = ((uint64_t)ntohl(a.s_addr) << 16) | (uint16_t)port;
    char d[11]; int sum = 0;
    for (int i = 9; i >= 0; i--) { d[i] = B32[v & 31]; sum += (int)(v & 31); v >>= 5; }
    std::string s = "NB-";
    s.append(d, 5); s += '-'; s.append(d + 5, 5); s += '-'; s += B32[sum % 32];
    return s;
}
bool NetLink::parse_invite(const std::string& text, std::string& ip, int& port) {
    std::string t;
    for (char c : text) if (c != ' ' && c != '\t' && c != '\r' && c != '\n') t += c;
    if (t.empty()) return false;
    // plain address "1.2.3.4" or "1.2.3.4:5738" (or a name)
    if (t.find('.') != std::string::npos) {
        size_t c = t.rfind(':');
        port = NB_LINK_PORT;
        if (c != std::string::npos) { port = atoi(t.c_str() + c + 1); t = t.substr(0, c); }
        ip = t;
        return port > 0 && port < 65536;
    }
    std::string u;
    for (char c : t) {
        if (c == '-') continue;
        c = (char)toupper((unsigned char)c);
        if (c == 'O') c = '0';
        if (c == 'I' || c == 'L') c = '1';
        u += c;
    }
    if (u.size() != 13 || u.compare(0, 2, "NB") != 0) return false;
    uint64_t v = 0; int sum = 0;
    for (int i = 2; i < 12; i++) {
        const char* p = strchr(B32, u[i]);
        if (!p || !*p) return false;
        v = (v << 5) | (uint64_t)(p - B32); sum += (int)(p - B32);
    }
    if (u[12] != B32[sum % 32] || (v >> 48)) return false;
    port = (int)(v & 0xFFFF);
    in_addr a{}; a.s_addr = htonl((uint32_t)(v >> 16));
    char b[64]; inet_ntop(AF_INET, &a, b, sizeof b);
    ip = b;
    return port > 0;
}
std::vector<std::string> NetLink::local_ips() {
    std::vector<std::string> out;
    // the address used to reach the internet: a UDP "connection" sends nothing, it only picks the route
    long long s = (long long)socket(AF_INET, SOCK_DGRAM, 0);
    if (s == BAD) return out;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &a.sin_addr);
    if (::connect((sock_t)s, (sockaddr*)&a, sizeof a) == 0) {
        sockaddr_in me{}; socklen_t len = sizeof me;
        if (getsockname((sock_t)s, (sockaddr*)&me, &len) == 0) {
            char b[64]; inet_ntop(AF_INET, &me.sin_addr, b, sizeof b);
            if (strcmp(b, "0.0.0.0") != 0) out.push_back(b);
        }
    }
    sclose(s);
    return out;
}
bool NetLink::is_private_ip(const std::string& ip) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return false;
    return a == 10 || a == 127 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b < 32) || (a == 100 && b >= 64 && b < 128) || (a == 169 && b == 254);
}

// ------------------------------------------------------------------ local network
bool NetLink::lan_scan_start() {
    if (lan_fd != BAD) return true;
    long long s = (long long)socket(AF_INET, SOCK_DGRAM, 0);
    if (s == BAD) return false;
    int one = 1;
    setsockopt((sock_t)s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
#ifdef SO_REUSEPORT
    setsockopt((sock_t)s, SOL_SOCKET, SO_REUSEPORT, (const char*)&one, sizeof one);
#endif
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((unsigned short)NB_LAN_PORT); a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind((sock_t)s, (sockaddr*)&a, sizeof a) != 0) { sclose(s); return false; }
    set_nonblock(s, true);
    lan_fd = s;
    return true;
}
void NetLink::lan_scan_stop() { if (lan_fd != BAD) sclose(lan_fd); lan_fd = BAD; lan.clear(); }
void NetLink::lan_receive() {
    const double t = now_s();
    for (int i = 0; i < 32; i++) {
        char b[256]; sockaddr_in from{}; socklen_t len = sizeof from;
        int n = (int)recvfrom((sock_t)lan_fd, b, sizeof b - 1, 0, (sockaddr*)&from, &len);
        if (n <= 0) break;
        b[n] = 0;
        unsigned crc = 0; int port = 0; int used = 0;
        if (sscanf(b, "NBLINK1|%8X|%d|%n", &crc, &port, &used) < 2 || used <= 0) continue;
        char ip[64]; inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
        std::string sip = ip;
        if (sip == "127.0.0.1") { auto l = local_ips(); if (!l.empty()) sip = l[0]; }     // this computer (another window)
        auto it = std::find_if(lan.begin(), lan.end(), [&](const LanGame& g) { return g.ip == sip && g.port == port; });
        if (it == lan.end()) { lan.push_back(LanGame()); it = lan.end() - 1; }
        it->ip = sip; it->port = port; it->game = crc; it->name = b + used; it->seen = t;
    }
    lan.erase(std::remove_if(lan.begin(), lan.end(), [&](const LanGame& g) { return t - g.seen > 4; }), lan.end());
}
std::vector<LanGame> NetLink::lan_games() const {
    std::vector<LanGame> out;
    for (auto& g : lan) if (g.game == game_crc && !(listen_fd != BAD && g.port == host_port)) out.push_back(g);
    return out;
}

// ------------------------------------------------------------------ router port (UPnP IGD)
namespace {
struct Url { std::string host, path; int port = 80; };
bool parse_url(const std::string& u, Url& out) {
    size_t p = u.find("://");
    if (p == std::string::npos) return false;
    std::string rest = u.substr(p + 3);
    size_t sl = rest.find('/');
    std::string hp = rest.substr(0, sl);
    out.path = sl == std::string::npos ? "/" : rest.substr(sl);
    size_t c = hp.find(':');
    out.host = hp.substr(0, c);
    out.port = c == std::string::npos ? 80 : atoi(hp.c_str() + c + 1);
    return !out.host.empty();
}
// minimal HTTP/1.1 client (Connection: close). Returns the status code (0 = failed); local_ip = our address on that route
int http(const Url& u, const std::string& method, const std::string& extra, const std::string& body, std::string& resp, std::string* local_ip = nullptr) {
    addrinfo hints{}, *res = nullptr; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(u.host.c_str(), std::to_string(u.port).c_str(), &hints, &res) != 0 || !res) return 0;
    long long s = (long long)socket(AF_INET, SOCK_STREAM, 0);
    if (s == BAD) { freeaddrinfo(res); return 0; }
    set_nonblock(s, true);
    int r = ::connect((sock_t)s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if ((r != 0 && !would_block()) || !wait_fd(s, 3000, true)) { sclose(s); return 0; }
    int soerr = 0; socklen_t sl = sizeof soerr; getsockopt((sock_t)s, SOL_SOCKET, SO_ERROR, (char*)&soerr, &sl);
    if (soerr) { sclose(s); return 0; }
    if (local_ip) {
        sockaddr_in me{}; socklen_t len = sizeof me;
        if (getsockname((sock_t)s, (sockaddr*)&me, &len) == 0) { char b[64]; inet_ntop(AF_INET, &me.sin_addr, b, sizeof b); *local_ip = b; }
    }
    set_nonblock(s, false);
    std::string req = method + " " + u.path + " HTTP/1.1\r\nHost: " + u.host + ":" + std::to_string(u.port) + "\r\nConnection: close\r\n" + extra;
    if (!body.empty() || method == "POST") req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    req += "\r\n" + body;
    size_t sent = 0;
    while (sent < req.size()) { int n = (int)send((sock_t)s, req.data() + sent, (int)(req.size() - sent), 0); if (n <= 0) { sclose(s); return 0; } sent += n; }
    std::string all;
    while (wait_fd(s, 4000)) {
        char b[4096]; int n = (int)recv((sock_t)s, b, sizeof b, 0);
        if (n <= 0) break;
        all.append(b, n);
        if (all.size() > (1 << 20)) break;
    }
    sclose(s);
    size_t he = all.find("\r\n\r\n");
    if (he == std::string::npos || all.compare(0, 5, "HTTP/") != 0) return 0;
    int code = atoi(all.c_str() + all.find(' ') + 1);
    std::string head = all.substr(0, he), b = all.substr(he + 4);
    std::string lh = head; for (auto& c : lh) c = (char)tolower((unsigned char)c);
    if (lh.find("transfer-encoding: chunked") != std::string::npos) {     // routers often answer in chunks
        std::string out; size_t p = 0;
        while (p < b.size()) {
            size_t e = b.find("\r\n", p); if (e == std::string::npos) break;
            long n = strtol(b.substr(p, e - p).c_str(), nullptr, 16);
            if (n <= 0) break;
            out += b.substr(e + 2, (size_t)n); p = e + 2 + n + 2;
        }
        b = out;
    }
    resp = b;
    return code;
}
std::string tag(const std::string& xml, const std::string& name, size_t from = 0) {
    size_t a = xml.find("<" + name + ">", from);
    if (a == std::string::npos) {     // with a namespace prefix: <s:name> / <u:name>
        size_t p = from;
        while ((p = xml.find(":" + name + ">", p)) != std::string::npos) { size_t lt = xml.rfind('<', p); if (lt != std::string::npos && xml[lt + 1] != '/') { a = lt; break; } p++; }
        if (a == std::string::npos) return "";
    }
    size_t s = xml.find('>', a) + 1, e = xml.find("</", s);
    return e == std::string::npos ? "" : xml.substr(s, e - s);
}
std::string soap(const std::string& ctrl, const std::string& service, const std::string& action, const std::string& args, int& code) {
    Url u; std::string resp;
    if (!parse_url(ctrl, u)) { code = 0; return ""; }
    std::string body = "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                       "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" + action + " xmlns:u=\"" + service + "\">" +
                       args + "</u:" + action + "></s:Body></s:Envelope>\r\n";
    code = http(u, "POST", "Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + service + "#" + action + "\"\r\n", body, resp);
    return resp;
}
// finds the router: answer to the SSDP search on the local network (NB_UPNP_DESC skips it, for tests)
std::string find_router() {
    if (const char* e = getenv("NB_UPNP_DESC")) return e;
    long long s = (long long)socket(AF_INET, SOCK_DGRAM, 0);
    if (s == BAD) return "";
    int ttl = 2; setsockopt((sock_t)s, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof ttl);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(1900); inet_pton(AF_INET, "239.255.255.250", &a.sin_addr);
    std::string found;
    for (const char* st : {"urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:service:WANIPConnection:1", "upnp:rootdevice"}) {
        std::string m = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") + st + "\r\n\r\n";
        sendto((sock_t)s, m.data(), (int)m.size(), 0, (sockaddr*)&a, sizeof a);
        double t0 = now_s();
        while (now_s() - t0 < 2.5) {
            if (!wait_fd(s, 300)) continue;
            char b[2048]; int n = (int)recv((sock_t)s, b, sizeof b - 1, 0);
            if (n <= 0) continue;
            b[n] = 0;
            std::string r = b, l = r; for (auto& c : l) c = (char)tolower((unsigned char)c);
            size_t p = l.find("\nlocation:");
            if (p == std::string::npos) continue;
            size_t e = r.find("\r\n", p + 10);
            std::string loc = r.substr(p + 10, e - p - 10);
            while (!loc.empty() && loc[0] == ' ') loc.erase(0, 1);
            if (l.find("internetgatewaydevice") != std::string::npos || l.find("wanipconnection") != std::string::npos || found.empty()) found = loc;
            if (l.find("internetgatewaydevice") != std::string::npos) break;
        }
        if (!found.empty()) break;
    }
    sclose(s);
    return found;
}
} // namespace

void NetLink::upnp_open_async(int port) {
    if (upnp_st.load() == 1) return;
    if (upnp_thread.joinable()) upnp_thread.join();
    upnp_st = 1;
    { std::lock_guard<std::mutex> g(upnp_mx); upnp_msg = "Procurando o roteador..."; }
    upnp_thread = std::thread([this, port] {
        auto fail = [&](const std::string& m) { std::lock_guard<std::mutex> g(upnp_mx); upnp_msg = m; upnp_st = 3; };
        std::string loc = find_router();
        if (loc.empty()) { fail("O roteador não respondeu (UPnP desligado ou ausente). Abra a porta " + std::to_string(port) + " (TCP) manualmente na configuração do roteador."); return; }
        Url u; std::string desc, local;
        if (!parse_url(loc, u) || http(u, "GET", "", "", desc, &local) != 200) { fail("Não foi possível ler as informações do roteador."); return; }
        std::string service, ctrl;
        for (const char* sv : {"urn:schemas-upnp-org:service:WANIPConnection:2", "urn:schemas-upnp-org:service:WANIPConnection:1", "urn:schemas-upnp-org:service:WANPPPConnection:1"}) {
            size_t p = desc.find(sv);
            if (p == std::string::npos) continue;
            ctrl = tag(desc, "controlURL", p);
            if (!ctrl.empty()) { service = sv; break; }
        }
        if (ctrl.empty()) { fail("O roteador não oferece abertura de portas (UPnP)."); return; }
        if (ctrl.find("://") == std::string::npos) {      // relative to the description's address
            std::string base = "http://" + u.host + ":" + std::to_string(u.port);
            std::string ub = tag(desc, "URLBase");
            if (!ub.empty()) { base = ub; while (!base.empty() && base.back() == '/') base.pop_back(); }
            ctrl = base + (ctrl[0] == '/' ? "" : "/") + ctrl;
        }
        const std::string ps = std::to_string(port);
        int code = 0;
        std::string args = "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + ps + "</NewExternalPort><NewProtocol>TCP</NewProtocol>"
                           "<NewInternalPort>" + ps + "</NewInternalPort><NewInternalClient>" + local + "</NewInternalClient>"
                           "<NewEnabled>1</NewEnabled><NewPortMappingDescription>NativeBoy cabo link</NewPortMappingDescription>";
        soap(ctrl, service, "AddPortMapping", args + "<NewLeaseDuration>7200</NewLeaseDuration>", code);
        if (code != 200) soap(ctrl, service, "AddPortMapping", args + "<NewLeaseDuration>0</NewLeaseDuration>", code);   // some only accept 0
        if (code != 200) { fail("O roteador recusou abrir a porta " + ps + " (código " + std::to_string(code) + "). Abra-a manualmente na configuração do roteador."); return; }
        std::string ip = tag(soap(ctrl, service, "GetExternalIPAddress", "", code), "NewExternalIPAddress");
        std::lock_guard<std::mutex> g(upnp_mx);
        upnp_ctrl = ctrl; upnp_service = service; upnp_port = port; upnp_internal = local; upnp_ext_ip = ip;
        if (ip.empty()) upnp_msg = "Porta " + ps + " aberta no roteador.";
        else if (is_private_ip(ip))
            upnp_msg = "Porta aberta no roteador, mas a sua operadora usa um endereço compartilhado (" + ip + ", CGNAT): quem está fora da sua rede não consegue chegar. "
                       "Use uma rede virtual (como ZeroTier ou Tailscale) ou peça um IP público à operadora.";
        else upnp_msg = "Porta " + ps + " aberta no roteador. Seu endereço na internet: " + ip;
        upnp_st = 2;
    });
}
std::string NetLink::upnp_message() const { std::lock_guard<std::mutex> g(upnp_mx); return upnp_msg; }
std::string NetLink::external_ip() const { std::lock_guard<std::mutex> g(upnp_mx); return upnp_ext_ip; }
void NetLink::upnp_close() {     // the port is closed again when the game ends
    std::string ctrl, service; int port = 0;
    { std::lock_guard<std::mutex> g(upnp_mx); ctrl = upnp_ctrl; service = upnp_service; port = upnp_port; upnp_ctrl.clear(); }
    if (ctrl.empty() || !port) return;
    int code = 0;
    soap(ctrl, service, "DeletePortMapping", "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) + "</NewExternalPort><NewProtocol>TCP</NewProtocol>", code);
}
