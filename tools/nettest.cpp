// Link cable over the internet, second generation: invite codes, finding a partner on the local network,
// game check and delay, and opening the router port (UPnP, against a fake router given by NB_UPNP_DESC).
// usage: nettest [upnp]
#include "../core/gb.h"
#include "../core/apu.h"
#include "../common/link.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
using namespace gb;
static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "upnp")) {
        int st = 0;
        std::string msg, ip;
        {
            NetLink l;
            l.upnp_open_async(5738);
            for (int i = 0; i < 300 && (l.upnp_state() == 1); i++) sleep_ms(20);
            st = l.upnp_state(); msg = l.upnp_message(); ip = l.external_ip();
        }   // closing removes the port again
        printf("upnp_estado=%d ip_externo=%s\nmensagem=%s\n", st, ip.c_str(), msg.c_str());
        return st == 2 ? 0 : 1;
    }
    bool ok = true;
    // 1. invite codes
    std::string code = NetLink::invite_code("203.0.113.45", 5738), ip; int port = 0;
    bool c1 = NetLink::parse_invite(code, ip, port) && ip == "203.0.113.45" && port == 5738;
    std::string typed = code; for (auto& c : typed) c = (char)tolower((unsigned char)c);   // typed in lower case with spaces
    typed.insert(5, " ");
    bool c2 = NetLink::parse_invite(typed, ip, port) && ip == "203.0.113.45";
    std::string bad = code; bad[4] = bad[4] == 'A' ? 'B' : 'A';                              // one wrong letter
    bool c3 = !NetLink::parse_invite(bad, ip, port);
    bool c4 = NetLink::parse_invite("192.168.0.10:6000", ip, port) && ip == "192.168.0.10" && port == 6000;
    printf("convite=%s ida_e_volta=%d minusculas=%d letra_errada_recusada=%d endereco_direto=%d\n", code.c_str(), c1, c2, c3, c4);
    ok &= c1 && c2 && c3 && c4;

    // 2. local network: B finds A, which hosts the same game; a host with another game is not listed
    NetLink a, b, other;
    a.set_game(0x1234ABCD, "Jogo de teste"); b.set_game(0x1234ABCD, "Jogo de teste"); other.set_game(0x55555555, "Outro jogo");
    std::string err;
    bool scan = b.lan_scan_start();
    bool ha = a.host(25738, err), ho = other.host(25740, err);
    std::vector<LanGame> found;
    for (int i = 0; i < 150 && found.empty(); i++) { a.update(); other.update(); b.update(); found = b.lan_games(); sleep_ms(20); }
    bool lan_ok = scan && ha && ho && found.size() == 1 && found[0].port == 25738 && found[0].name == "Jogo de teste";
    printf("rede_local: escutando=%d encontrados=%zu porta=%d nome=%s\n", scan, found.size(), found.empty() ? 0 : found[0].port, found.empty() ? "" : found[0].name.c_str());
    ok &= lan_ok;
    other.close();

    // 3. connect through what was found; game check and delay measured both ways
    auto ma = std::make_unique<Machine>(), mb = std::make_unique<Machine>();
    bool conn = !found.empty() && b.connect_to("127.0.0.1", found[0].port, err);
    for (int i = 0; i < 200 && conn; i++) {
        a.update(); b.update(); a.poll(*ma); b.poll(*mb);
        if (a.ping_ms() >= 0 && b.ping_ms() >= 0 && i > 60) break;
        sleep_ms(20);
    }
    bool same = conn && a.status() == 2 && !a.partner_game_differs() && !b.partner_game_differs();
    printf("conexao=%d mesmo_jogo=%d atraso_a=%d ms atraso_b=%d ms\n", conn, same, a.ping_ms(), b.ping_ms());
    ok &= same && a.ping_ms() >= 0 && b.ping_ms() >= 0;
    a.close(); b.close();
    // a partner with another game is noticed
    NetLink c, d; c.set_game(1, "A"); d.set_game(2, "B");
    bool cd = c.host(25742, err) && d.connect_to("127.0.0.1", 25742, err);
    for (int i = 0; i < 100 && cd; i++) { c.update(); d.update(); c.poll(*ma); d.poll(*mb); if (c.partner_game_differs() && d.partner_game_differs()) break; sleep_ms(10); }
    bool diff = cd && c.partner_game_differs() && d.partner_game_differs();
    printf("jogo_diferente_avisado=%d\n", diff);
    ok &= diff;
    puts(ok ? "NET OK" : "NET FAIL");
    return ok ? 0 : 1;
}
