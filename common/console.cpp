// NativeBoy - "console" look for the native game window, and the power on / off animation.
//
// The window becomes a handheld drawn in the style of the classic (GB) or Color (GBC) consoles. It is an
// original drawing: no Nintendo names or logos, the name on it is "NativeBoy". The game picture goes in the
// screen, the drawn buttons sink while pressed (keyboard, controller or the mouse / a touch on them), the
// power light is on while the game runs, and in a window without borders the body is used to drag it.
//
// Layout is designed on a 1000-unit-wide body (GB 1000 x 1640, GBC 1000 x 1700) and scaled to the window.
#include "player.h"
#include "../core/apu.h"
#include "platform.h"
#include "ui_common.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace gb;

namespace {

const float PI_F = 3.14159265358979f;

SDL_Color mix(SDL_Color a, SDL_Color b, float t) {
    return SDL_Color{(Uint8)(a.r + (b.r - a.r) * t), (Uint8)(a.g + (b.g - a.g) * t), (Uint8)(a.b + (b.b - a.b) * t), (Uint8)(a.a + (b.a - a.a) * t)};
}
SDL_Color shade(SDL_Color c, float k, Uint8 alpha = 0) {
    auto f = [&](int v) { return (Uint8)std::max(0, std::min(255, (int)(v * k))); };
    return SDL_Color{f(c.r), f(c.g), f(c.b), alpha ? alpha : c.a};
}

// convex polygon (fan from its centre), colour by height: top colour -> bottom colour
void poly(SDL_Renderer* r, const std::vector<SDL_FPoint>& p, SDL_Color top, SDL_Color bottom) {
    if (p.size() < 3) return;
    float y0 = 1e9f, y1 = -1e9f, cx = 0, cy = 0;
    for (auto& q : p) { y0 = std::min(y0, q.y); y1 = std::max(y1, q.y); cx += q.x; cy += q.y; }
    cx /= p.size(); cy /= p.size();
    auto col = [&](float y) { return mix(top, bottom, y1 > y0 ? (y - y0) / (y1 - y0) : 0.f); };
    std::vector<SDL_Vertex> v;
    v.push_back({{cx, cy}, col(cy), {0, 0}});
    for (auto& q : p) v.push_back({q, col(q.y), {0, 0}});
    std::vector<int> idx;
    int n = (int)p.size();
    for (int i = 0; i < n; i++) { idx.push_back(0); idx.push_back(1 + i); idx.push_back(1 + (i + 1) % n); }
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}
// rounded rectangle with its own radius at each corner (top-left, top-right, bottom-right, bottom-left)
std::vector<SDL_FPoint> rrect_pts(float x, float y, float w, float h, float tl, float tr, float br, float bl) {
    std::vector<SDL_FPoint> p;
    auto arc = [&](float cx, float cy, float rad, float a0) {
        if (rad <= 0.5f) { p.push_back({cx, cy}); return; }
        for (int i = 0; i <= 10; i++) { float a = a0 + i * (PI_F / 2) / 10; p.push_back({cx + rad * std::cos(a), cy + rad * std::sin(a)}); }
    };
    arc(x + tl, y + tl, tl, PI_F);                 // top-left
    arc(x + w - tr, y + tr, tr, -PI_F / 2);        // top-right
    arc(x + w - br, y + h - br, br, 0);            // bottom-right
    arc(x + bl, y + h - bl, bl, PI_F / 2);         // bottom-left
    return p;
}
void rrect(SDL_Renderer* r, float x, float y, float w, float h, float rad, SDL_Color top, SDL_Color bottom) {
    rad = std::min(rad, std::min(w, h) / 2);
    poly(r, rrect_pts(x, y, w, h, rad, rad, rad, rad), top, bottom);
}
void circle(SDL_Renderer* r, float cx, float cy, float rad, SDL_Color top, SDL_Color bottom) {
    std::vector<SDL_FPoint> p;
    for (int i = 0; i < 36; i++) { float a = i * 2 * PI_F / 36; p.push_back({cx + rad * std::cos(a), cy + rad * std::sin(a)}); }
    poly(r, p, top, bottom);
}
// a rounded bar turned by `ang` radians around its centre
void capsule(SDL_Renderer* r, float cx, float cy, float w, float h, float ang, SDL_Color c) {
    auto p = rrect_pts(-w / 2, -h / 2, w, h, h / 2, h / 2, h / 2, h / 2);
    const float cs = std::cos(ang), sn = std::sin(ang);
    for (auto& q : p) { float x = q.x * cs - q.y * sn, y = q.x * sn + q.y * cs; q = {cx + x, cy + y}; }
    poly(r, p, c, c);
}

struct Shell { SDL_Color body, body2, bezel, ab, ab_dark, dpad, pill, text; bool clear; };
Shell shell_colours(const std::string& id, int style) {
    // body colour (top, bottom), screen surround, A/B buttons, direction pad, small buttons, name colour, see-through
    if (id == "roxo_translucido") return {{132, 104, 214, 205}, {96, 70, 176, 215}, {28, 26, 34, 255}, {40, 36, 52, 255}, {24, 22, 32, 255}, {34, 32, 44, 255}, {70, 58, 110, 255}, {240, 236, 255, 255}, true};
    if (id == "roxo") return {{104, 70, 186, 255}, {78, 50, 150, 255}, {28, 26, 34, 255}, {36, 32, 46, 255}, {22, 20, 30, 255}, {34, 32, 44, 255}, {60, 44, 104, 255}, {238, 232, 255, 255}, false};
    if (id == "amarelo") return {{244, 206, 66, 255}, {222, 178, 40, 255}, {30, 28, 34, 255}, {44, 42, 52, 255}, {26, 24, 30, 255}, {40, 38, 48, 255}, {150, 120, 40, 255}, {60, 46, 20, 255}, false};
    if (id == "azul_petroleo") return {{42, 156, 160, 255}, {26, 120, 126, 255}, {26, 28, 32, 255}, {36, 40, 48, 255}, {20, 22, 28, 255}, {32, 36, 44, 255}, {24, 86, 90, 255}, {230, 250, 250, 255}, false};
    if (id == "vermelho") return {{214, 60, 70, 255}, {176, 38, 50, 255}, {28, 26, 30, 255}, {40, 36, 44, 255}, {22, 20, 26, 255}, {36, 32, 40, 255}, {120, 30, 40, 255}, {255, 236, 236, 255}, false};
    if (id == "verde") return {{140, 200, 76, 255}, {106, 168, 52, 255}, {28, 30, 30, 255}, {40, 44, 44, 255}, {22, 26, 26, 255}, {36, 40, 40, 255}, {70, 110, 40, 255}, {30, 50, 20, 255}, false};
    if (id == "preto") return {{58, 58, 66, 255}, {36, 36, 42, 255}, {20, 20, 24, 255}, {70, 70, 80, 255}, {44, 44, 52, 255}, {24, 24, 28, 255}, {90, 90, 100, 255}, {220, 220, 230, 255}, false};
    // classic grey: magenta round buttons, dark grey surround
    return {{204, 200, 196, 255}, {182, 178, 176, 255}, style == 0 ? SDL_Color{92, 96, 108, 255} : SDL_Color{30, 30, 34, 255},
            {156, 40, 92, 255}, {110, 24, 62, 255}, {42, 42, 48, 255}, {120, 120, 130, 255}, {48, 48, 112, 255}, false};
}

} // namespace

bool GamePlayer::console_on() const {
    const std::string v = cfg.get("visual_console", "desligado");
    return v == "gb" || v == "gbc";
}

void GamePlayer::console_layout(const SDL_Rect& area) {
    ConsoleLayout& c = con;
    c.on = console_on();
    if (!c.on) return;
    c.style = cfg.get("visual_console", "gb") == "gbc" ? 1 : 0;
    const float W = 1000, H = c.style ? 1700.f : 1640.f;
    const float u = std::min(area.w * 0.97f / W, area.h * 0.97f / H);
    c.u = u;
    c.ox = area.x + (area.w - W * u) / 2; c.oy = area.y + (area.h - H * u) / 2;
    auto P = [&](float x, float y) { return SDL_FPoint{c.ox + x * u, c.oy + y * u}; };
    c.shell = {c.ox, c.oy, W * u, H * u};
    if (c.style == 0) {
        c.bezel = {c.ox + 80 * u, c.oy + 70 * u, 840 * u, 690 * u};
        c.screen = {(int)std::lround(c.ox + 200 * u), (int)std::lround(c.oy + 140 * u), (int)std::lround(600 * u), (int)std::lround(540 * u)};
        c.led = P(135, 330); c.dpad = P(250, 1130); c.b = P(690, 1185); c.a = P(850, 1105);
        c.select = P(420, 1400); c.start = P(580, 1400); c.logo = P(90, 790);
    } else {
        c.bezel = {c.ox + 70 * u, c.oy + 70 * u, 860 * u, 660 * u};
        c.screen = {(int)std::lround(c.ox + 200 * u), (int)std::lround(c.oy + 120 * u), (int)std::lround(600 * u), (int)std::lround(540 * u)};
        c.led = P(130, 250); c.dpad = P(240, 1060); c.b = P(680, 1090); c.a = P(840, 1015);
        c.select = P(430, 1320); c.start = P(570, 1320); c.logo = P(500, 760);
    }
    c.dpad_r = 140 * u; c.ab_r = 72 * u; c.pill_w = 130 * u; c.pill_h = 36 * u; c.icon_r = 22 * u;
    c.menu_btn = P(60, 42); c.min_btn = P(W - 120, 42); c.close_btn = P(W - 60, 42);
}

// the body, the screen surround, the buttons (drawn before the game picture)
void GamePlayer::console_draw(SDL_Renderer* r, const SDL_Rect& area) {
    const ConsoleLayout& c = con;
    const float u = c.u;
    const Shell s = shell_colours(cfg.get("console_cor", c.style ? "roxo_translucido" : "cinza"), c.style);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    // what is around the device (full screen or a wide window): a soft dark backdrop
    {
        SDL_Vertex v[4] = {{{(float)area.x, (float)area.y}, {34, 26, 52, 255}, {0, 0}}, {{(float)(area.x + area.w), (float)area.y}, {24, 18, 40, 255}, {0, 0}},
                           {{(float)(area.x + area.w), (float)(area.y + area.h)}, {12, 10, 22, 255}, {0, 0}}, {{(float)area.x, (float)(area.y + area.h)}, {18, 14, 30, 255}, {0, 0}}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
    }
    const float x = c.shell.x, y = c.shell.y, w = c.shell.w, h = c.shell.h;
    const float big = c.style == 0 ? 210 * u : 120 * u, small = c.style == 0 ? 36 * u : 120 * u;
    // shadow, then the body (the classic one has its famous large rounded corner at the bottom right)
    poly(r, rrect_pts(x + 10 * u, y + 16 * u, w, h, small, small, big, c.style ? big : small), {0, 0, 0, 90}, {0, 0, 0, 110});
    if (s.clear) {
        // see-through plastic: the board and parts inside, seen through the body
        poly(r, rrect_pts(x, y, w, h, small, small, big, c.style ? big : small), {30, 28, 40, 255}, {22, 20, 32, 255});
        rrect(r, x + 120 * u, y + 820 * u, w - 240 * u, h - 900 * u, 40 * u, {36, 70, 52, 255}, {28, 56, 42, 255});    // circuit board
        SDL_SetRenderDrawColor(r, 120, 170, 120, 70);
        for (int k = 0; k < 9; k++) {
            SDL_FRect ln{x + (170 + k * 75) * u, y + (860 + (k % 3) * 40) * u, 3 * u, (220 + (k * 37) % 300) * u};
            SDL_RenderFillRectF(r, &ln);
            SDL_FRect lh{x + 170 * u, y + (900 + k * 70) * u, (300 + (k * 53) % 380) * u, 3 * u};
            SDL_RenderFillRectF(r, &lh);
        }
        circle(r, x + 760 * u, y + 1520 * u, 110 * u, {60, 60, 70, 255}, {40, 40, 50, 255});                       // speaker
        rrect(r, x + 300 * u, y + 1420 * u, 380 * u, 160 * u, 20 * u, {70, 74, 84, 255}, {52, 56, 66, 255});      // battery
    }
    poly(r, rrect_pts(x, y, w, h, small, small, big, c.style ? big : small), s.body, s.body2);
    // a light edge along the top and a groove under the screen, like moulded plastic
    SDL_SetRenderDrawColor(r, 255, 255, 255, s.clear ? 40 : 55);
    SDL_FRect hl{x + small, y + 4 * u, w - 2 * small, 4 * u};
    SDL_RenderFillRectF(r, &hl);
    // window buttons on the body (menu, minimise, close)
    const bool winbtn = true;
    if (winbtn) {
        const SDL_Color ic = shade(s.body2, 0.78f);
        circle(r, c.menu_btn.x, c.menu_btn.y, c.icon_r, ic, ic);
        circle(r, c.min_btn.x, c.min_btn.y, c.icon_r, ic, ic);
        circle(r, c.close_btn.x, c.close_btn.y, c.icon_r, ic, ic);
    }
    // screen surround
    if (c.style == 0) poly(r, rrect_pts(c.bezel.x, c.bezel.y, c.bezel.w, c.bezel.h, 26 * u, 26 * u, 110 * u, 26 * u), shade(s.bezel, 1.05f), shade(s.bezel, 0.92f));
    else rrect(r, c.bezel.x, c.bezel.y, c.bezel.w, c.bezel.h, 60 * u, shade(s.bezel, 1.25f), s.bezel);
    // thin coloured lines across the surround (the classic one), or a glossy edge (the Color one)
    if (c.style == 0) {
        SDL_SetRenderDrawColor(r, 130, 40, 100, 255);
        SDL_FRect l1{c.bezel.x + 30 * u, c.bezel.y + 34 * u, c.bezel.w - 60 * u, 5 * u}; SDL_RenderFillRectF(r, &l1);
        SDL_SetRenderDrawColor(r, 40, 40, 110, 255);
        SDL_FRect l2{c.bezel.x + 30 * u, c.bezel.y + 46 * u, c.bezel.w - 60 * u, 5 * u}; SDL_RenderFillRectF(r, &l2);
    } else {
        SDL_SetRenderDrawColor(r, 255, 255, 255, 22);
        SDL_FRect g{c.bezel.x + 40 * u, c.bezel.y + 10 * u, c.bezel.w - 80 * u, 26 * u}; SDL_RenderFillRectF(r, &g);
    }
    // the screen itself (switched off: dark greyish green / dark grey)
    SDL_SetRenderDrawColor(r, c.style == 0 ? 56 : 18, c.style == 0 ? 64 : 20, c.style == 0 ? 50 : 22, 255);
    SDL_Rect sc{c.screen.x - (int)(6 * u), c.screen.y - (int)(6 * u), c.screen.w + (int)(12 * u), c.screen.h + (int)(12 * u)};
    SDL_RenderFillRect(r, &sc);
    // power light: on while the game runs, off during the power animations' dark moments
    float led = power_led();
    circle(r, c.led.x, c.led.y, 13 * u, {50, 20, 20, 255}, {40, 14, 14, 255});
    if (led > 0) {
        circle(r, c.led.x, c.led.y, 34 * u, {255, 40, 40, (Uint8)(50 * led)}, {255, 40, 40, (Uint8)(20 * led)});
        circle(r, c.led.x, c.led.y, 12 * u, {255, (Uint8)(90 + 60 * led), 90, (Uint8)(255 * led)}, {220, 30, 30, (Uint8)(255 * led)});
    }
    // what is held right now (keyboard, controller or the mouse on the drawn buttons)
    const u8 held = (running && !menu_open && !start_screen && !ask_continue && !splash_on && !power_state) ? (u8)(m.buttons | mouse_buttons) : 0;
    // direction pad: a cross with a dimple; the pressed arm sinks
    {
        const float L = c.dpad_r * 2, T = 92 * u, cx = c.dpad.x, cy = c.dpad.y;
        rrect(r, cx - L / 2 + 6 * u, cy - T / 2 + 10 * u, L, T, 12 * u, {0, 0, 0, 80}, {0, 0, 0, 80});
        rrect(r, cx - T / 2 + 6 * u, cy - L / 2 + 10 * u, T, L, 12 * u, {0, 0, 0, 80}, {0, 0, 0, 80});
        rrect(r, cx - L / 2, cy - T / 2, L, T, 12 * u, shade(s.dpad, 1.25f), s.dpad);
        rrect(r, cx - T / 2, cy - L / 2, T, L, 12 * u, shade(s.dpad, 1.25f), s.dpad);
        auto arm = [&](int bit, float dx, float dy) {
            if (!(held & bit)) return;
            SDL_FRect q{cx + dx * (L / 2 - T / 2) - T / 2 + 4 * u, cy + dy * (L / 2 - T / 2) - T / 2 + 4 * u, T - 8 * u, T - 8 * u};
            rrect(r, q.x, q.y, q.w, q.h, 10 * u, shade(s.dpad, 0.6f), shade(s.dpad, 0.7f));
        };
        arm(BTN_UP, 0, -1); arm(BTN_DOWN, 0, 1); arm(BTN_LEFT, -1, 0); arm(BTN_RIGHT, 1, 0);
        circle(r, cx, cy, 26 * u, shade(s.dpad, 0.8f), shade(s.dpad, 1.1f));
    }
    // A and B: round, they sink (smaller, darker, without shadow) while pressed
    auto roundb = [&](SDL_FPoint p, int bit) {
        const bool on = held & bit;
        const float rad = on ? c.ab_r * 0.93f : c.ab_r, dy = on ? 4 * u : 0;
        if (!on) circle(r, p.x + 5 * u, p.y + 9 * u, rad, {0, 0, 0, 90}, {0, 0, 0, 90});
        circle(r, p.x, p.y + dy, rad, on ? shade(s.ab, 0.75f) : shade(s.ab, 1.25f), on ? s.ab_dark : s.ab);
    };
    // the classic one has them on a darker slanted base
    if (c.style == 0) capsule(r, (c.a.x + c.b.x) / 2, (c.a.y + c.b.y) / 2, 330 * u, 170 * u, std::atan2(c.a.y - c.b.y, c.a.x - c.b.x), shade(s.body2, 0.88f));
    roundb(c.b, BTN_B); roundb(c.a, BTN_A);
    // SELECT and START: small slanted bars
    auto pill = [&](SDL_FPoint p, int bit) {
        const bool on = held & bit;
        if (c.style == 0) capsule(r, p.x, p.y, c.pill_w + 30 * u, c.pill_h + 30 * u, -0.45f, shade(s.body2, 0.88f));
        capsule(r, p.x + 3 * u, p.y + (on ? 3 : 6) * u, c.pill_w, c.pill_h, -0.45f, {0, 0, 0, (Uint8)(on ? 30 : 80)});
        capsule(r, p.x, p.y + (on ? 3 * u : 0), c.pill_w * (on ? 0.95f : 1.f), c.pill_h, -0.45f, on ? shade(s.pill, 0.7f) : s.pill);
    };
    pill(c.select, BTN_SELECT); pill(c.start, BTN_START);
    // speaker: slanted slots (classic) or a field of holes (Color)
    if (c.style == 0) {
        for (int k = 0; k < 6; k++) capsule(r, x + (760 + k * 40) * u, y + (1500 - k * 18) * u, 170 * u, 24 * u, -1.05f, shade(s.body2, 0.62f));
    } else {
        for (int row = 0; row < 5; row++) for (int col = 0; col < 5 - std::abs(row - 2) / 2; col++)
            circle(r, x + (760 + col * 40 + (row % 2) * 20) * u, y + (1460 + row * 38) * u, 9 * u, shade(s.body2, 0.55f), shade(s.body2, 0.6f));
    }
}

// glass over the screen: a faint diagonal reflection (after the game picture)
void GamePlayer::console_draw_front(SDL_Renderer* r) {
    const ConsoleLayout& c = con;
    if (!cfg.getb("console_reflexo", true)) return;
    const float x = (float)c.screen.x, y = (float)c.screen.y, w = (float)c.screen.w, h = (float)c.screen.h;
    SDL_Vertex v[4] = {{{x, y}, {255, 255, 255, 26}, {0, 0}}, {{x + w * 0.55f, y}, {255, 255, 255, 12}, {0, 0}},
                       {{x + w * 0.15f, y + h * 0.55f}, {255, 255, 255, 0}, {0, 0}}, {{x, y + h * 0.35f}, {255, 255, 255, 10}, {0, 0}}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

// names and small labels, with the interface font (ImGui background list: under menus, over the drawing)
void GamePlayer::console_labels() {
    const ConsoleLayout& c = con;
    if (!c.on) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float u = c.u;
    const Shell s = shell_colours(cfg.get("console_cor", c.style ? "roxo_translucido" : "cinza"), c.style);
    const ImU32 tc = IM_COL32(s.text.r, s.text.g, s.text.b, 230);
    ImFont* f = ImGui::GetFont();
    auto text = [&](float size, ImVec2 p, ImU32 col, const char* t, bool centre) {
        ImVec2 sz = f->CalcTextSizeA(size, FLT_MAX, 0, t);
        if (centre) p.x -= sz.x / 2;
        dl->AddText(f, size, p, col, t);
        return sz;
    };
    // the name, with the five colour dots of NativeBoy
    if (c.style == 0) {
        ImVec2 sz = text(64 * u, ImVec2(c.logo.x, c.logo.y), tc, "NativeBoy", false);
        for (int i = 0; i < 5; i++) dl->AddCircleFilled(ImVec2(c.logo.x + sz.x + (26 + i * 30) * u, c.logo.y + sz.y * 0.62f), 10 * u, ui::accent(i));
    } else {
        ImVec2 sz = text(56 * u, ImVec2(c.logo.x, c.logo.y), tc, "NativeBoy", true);
        for (int i = 0; i < 5; i++) dl->AddCircleFilled(ImVec2(c.logo.x - 60 * u + i * 30 * u, c.logo.y + sz.y + 20 * u), 10 * u, ui::accent(i));
    }
    const ImU32 lc = IM_COL32(s.text.r, s.text.g, s.text.b, 170);
    text(34 * u, ImVec2(c.b.x, c.b.y + c.ab_r + 10 * u), lc, "B", true);
    text(34 * u, ImVec2(c.a.x, c.a.y + c.ab_r + 10 * u), lc, "A", true);
    text(24 * u, ImVec2(c.select.x, c.select.y + 44 * u), lc, "SELECT", true);
    text(24 * u, ImVec2(c.start.x, c.start.y + 44 * u), lc, "START", true);
    // the power light's label, on the surround
    const ImU32 bl = c.style == 0 ? IM_COL32(205, 205, 215, 190) : IM_COL32(170, 170, 180, 170);
    text(20 * u, ImVec2(c.led.x, c.led.y + 26 * u), bl, "POWER", true);
    // window buttons: menu, minimise, close
    const ImU32 ic = IM_COL32(s.text.r, s.text.g, s.text.b, 210);
    const float ir = c.icon_r * 0.5f;
    for (int k = -1; k <= 1; k++) dl->AddLine(ImVec2(c.menu_btn.x - ir, c.menu_btn.y + k * ir * 0.6f), ImVec2(c.menu_btn.x + ir, c.menu_btn.y + k * ir * 0.6f), ic, 2.5f * std::max(0.6f, u * 2));
    dl->AddLine(ImVec2(c.min_btn.x - ir, c.min_btn.y), ImVec2(c.min_btn.x + ir, c.min_btn.y), ic, 2.5f * std::max(0.6f, u * 2));
    dl->AddLine(ImVec2(c.close_btn.x - ir, c.close_btn.y - ir), ImVec2(c.close_btn.x + ir, c.close_btn.y + ir), ic, 2.5f * std::max(0.6f, u * 2));
    dl->AddLine(ImVec2(c.close_btn.x - ir, c.close_btn.y + ir), ImVec2(c.close_btn.x + ir, c.close_btn.y - ir), ic, 2.5f * std::max(0.6f, u * 2));
}

// what is under a point of the window: buttons of the game, window buttons, the screen, the body
GamePlayer::ConsoleHit GamePlayer::console_hit(float px, float py) const {
    const ConsoleLayout& c = con;
    ConsoleHit h;
    if (!c.on) return h;
    auto d2 = [&](SDL_FPoint p) { return (px - p.x) * (px - p.x) + (py - p.y) * (py - p.y); };
    const float ir = c.icon_r * 1.4f;
    if (d2(c.menu_btn) < ir * ir) { h.kind = ConsoleHit::MENU; return h; }
    if (d2(c.min_btn) < ir * ir) { h.kind = ConsoleHit::MINIMIZE; return h; }
    if (d2(c.close_btn) < ir * ir) { h.kind = ConsoleHit::CLOSE; return h; }
    const float ar = c.ab_r * 1.25f;
    if (d2(c.a) < ar * ar) { h.kind = ConsoleHit::BUTTON; h.buttons = BTN_A; return h; }
    if (d2(c.b) < ar * ar) { h.kind = ConsoleHit::BUTTON; h.buttons = BTN_B; return h; }
    const float pr = c.pill_w * 0.75f;
    if (d2(c.start) < pr * pr * 0.45f) { h.kind = ConsoleHit::BUTTON; h.buttons = BTN_START; return h; }
    if (d2(c.select) < pr * pr * 0.45f) { h.kind = ConsoleHit::BUTTON; h.buttons = BTN_SELECT; return h; }
    {   // direction pad: by angle from the centre (diagonals give two directions)
        const float dx = px - c.dpad.x, dy = py - c.dpad.y, R = c.dpad_r * 1.15f;
        if (dx * dx + dy * dy < R * R && dx * dx + dy * dy > (12 * c.u) * (12 * c.u)) {
            const float ax = std::fabs(dx), ay = std::fabs(dy);
            u8 b = 0;
            if (ax > ay * 0.45f) b |= dx > 0 ? BTN_RIGHT : BTN_LEFT;
            if (ay > ax * 0.45f) b |= dy > 0 ? BTN_DOWN : BTN_UP;
            h.kind = ConsoleHit::BUTTON; h.buttons = b; return h;
        }
    }
    if (px >= c.screen.x && px < c.screen.x + c.screen.w && py >= c.screen.y && py < c.screen.y + c.screen.h) { h.kind = ConsoleHit::SCREEN; return h; }
    if (px >= c.shell.x && px < c.shell.x + c.shell.w && py >= c.shell.y && py < c.shell.y + c.shell.h) { h.kind = ConsoleHit::BODY; return h; }
    return h;
}

// ---------------------------------------------------------------------------- power on / off
// how bright the power light is (0..1) at this moment
float GamePlayer::power_led() const {
    if (!running) return 0;
    const Uint32 t = power_t0 ? SDL_GetTicks() - power_t0 : 0;
    if (power_state == 1) return t < 250 ? 0.f : std::min(1.f, (t - 250) / 150.f);
    if (power_state == 2) return t < 380 ? 1.f : std::max(0.f, 1.f - (t - 380) / 120.f);
    return 1;
}

void GamePlayer::power_on_begin() {
    if (in_studio || !cfg.getb("animacao_ligar", true)) return;
    power_state = 1; power_t0 = 0; power_chimed = false;
}

void GamePlayer::power_off_begin() {
    if (power_state == 2) return;
    if (in_studio || !cfg.getb("animacao_ligar", true) || !running) { power_state = 3; return; }   // 3 = done, nothing to show
    if (audio) SDL_ClearQueuedAudio(audio);
    power_state = 2; power_t0 = SDL_GetTicks();
}

bool GamePlayer::power_off_done() const {
    return power_state == 3 || (power_state == 2 && SDL_GetTicks() - power_t0 > 620);
}

// a short rising chime of our own (three soft notes), when the name lands
void GamePlayer::power_chime() {
    if (!audio || !cfg.getb("animacao_som", true)) return;
    const int rate = m.apu->sample_rate;
    const double notes[3] = {1046.50, 1318.51, 1567.98};      // C6, E6, G6
    const double starts[3] = {0.0, 0.09, 0.18}, lens[3] = {0.35, 0.35, 0.7};
    const int total = (int)(rate * 0.95);
    std::vector<float> buf((size_t)total * 2, 0.f);
    const float vol = std::max(0, std::min(100, cfg.geti("volume", 80))) / 100.f * 0.22f;
    for (int n = 0; n < 3; n++) {
        const int s0 = (int)(starts[n] * rate), len = (int)(lens[n] * rate);
        for (int i = 0; i < len && s0 + i < total; i++) {
            const double t = (double)i / rate;
            const double env = std::min(1.0, t / 0.006) * std::exp(-t * (n == 2 ? 5.0 : 9.0));
            const double v = (std::sin(2 * 3.14159265358979 * notes[n] * t) + 0.25 * std::sin(4 * 3.14159265358979 * notes[n] * t)) * env;
            buf[(size_t)(s0 + i) * 2] += (float)(v * vol);
            buf[(size_t)(s0 + i) * 2 + 1] += (float)(v * vol);
        }
    }
    SDL_QueueAudio(audio, buf.data(), (Uint32)(buf.size() * sizeof(float)));
}

// draws the animation in the screen area; returns true while it is still playing
bool GamePlayer::power_render(SDL_Renderer* r, const SDL_Rect& scr) {
    if (power_state != 1 && power_state != 2) return false;
    if (!power_t0) power_t0 = SDL_GetTicks();
    const Uint32 t = SDL_GetTicks() - power_t0;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const bool classic = con.on ? con.style == 0 : false;
    const SDL_Color off = classic ? SDL_Color{56, 64, 50, 255} : SDL_Color{14, 14, 18, 255};
    const SDL_Color lit = classic ? SDL_Color{168, 184, 120, 255} : SDL_Color{236, 236, 240, 255};
    if (power_state == 1) {
        const Uint32 total = 2500;
        if (t >= total) { power_state = 0; return false; }
        // dark -> the screen warms up -> holds -> fades to black just before the game
        float k = t < 300 ? 0.f : t < 700 ? (t - 300) / 400.f : 1.f;
        if (t > 300 && t < 700 && ((t / 40) % 3 == 0)) k *= 0.85f;     // a little flicker while it warms up
        SDL_Color c = mix(off, lit, k);
        if (t > total - 250) c = mix(c, {0, 0, 0, 255}, (t - (total - 250)) / 250.f);
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
        SDL_RenderFillRect(r, &scr);
        if (!power_chimed && t >= 1450) { power_chimed = true; power_chime(); }
        return true;
    }
    // turning off: the picture fades into the empty screen, a bright line closes, then dark
    const float k = std::min(1.f, t / 420.f);
    SDL_Color c = mix(SDL_Color{0, 0, 0, 0}, lit, k);
    c.a = (Uint8)(255 * std::min(1.f, k * 1.4f));
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &scr);
    if (t > 420) {
        const float q = std::min(1.f, (t - 420) / 180.f);
        SDL_SetRenderDrawColor(r, off.r, off.g, off.b, 255);
        SDL_RenderFillRect(r, &scr);
        const int lh = std::max(1, (int)(scr.h * 0.04f * (1 - q))), lw = (int)(scr.w * (1 - q * 0.9f));
        SDL_Rect line{scr.x + (scr.w - lw) / 2, scr.y + (scr.h - lh) / 2, lw, lh};
        SDL_SetRenderDrawColor(r, lit.r, lit.g, lit.b, (Uint8)(255 * (1 - q)));
        SDL_RenderFillRect(r, &line);
    }
    return true;
}

// the name falling into place and the five dots (ImGui, over the screen colour drawn above)
void GamePlayer::power_labels(const SDL_Rect& scr) {
    if (power_state != 1 || !power_t0) return;
    const Uint32 t = SDL_GetTicks() - power_t0;
    if (t < 600 || t > 2450) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const bool classic = con.on ? con.style == 0 : false;
    const float fs = scr.h * 0.16f;
    ImFont* f = ImGui::GetFont();
    const char* name = "NativeBoy";
    ImVec2 sz = f->CalcTextSizeA(fs, FLT_MAX, 0, name);
    float p = std::min(1.f, (t - 600) / 850.f);
    p = 1 - (1 - p) * (1 - p) * (1 - p);                         // eases into place
    const float yend = scr.y + scr.h * 0.40f - sz.y / 2, ystart = scr.y - sz.y;
    float a = t > 2250 ? std::max(0.f, 1 - (t - 2250) / 200.f) : 1.f;
    const ImU32 col = classic ? IM_COL32(40, 50, 40, (int)(255 * a)) : IM_COL32(60, 44, 130, (int)(255 * a));
    dl->PushClipRect(ImVec2((float)scr.x, (float)scr.y), ImVec2((float)(scr.x + scr.w), (float)(scr.y + scr.h)), true);
    dl->AddText(f, fs, ImVec2(scr.x + (scr.w - sz.x) / 2, ystart + (yend - ystart) * p), col, name);
    for (int i = 0; i < 5; i++) {
        const Uint32 ti = 1450 + i * 70;
        if (t < ti) break;
        const float g = std::min(1.f, (t - ti) / 120.f) * a;
        ImU32 ac = ui::accent(i);
        ac = (ac & 0x00FFFFFF) | ((ImU32)(255 * g) << 24);
        dl->AddCircleFilled(ImVec2(scr.x + scr.w / 2 + (i - 2) * fs * 0.42f, yend + sz.y + fs * 0.45f), fs * 0.13f * (0.6f + 0.4f * g), ac);
    }
    dl->PopClipRect();
}
