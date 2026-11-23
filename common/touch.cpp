// NativeBoy - touch controls for phones and tablets (Android), and touch screens on PCs.
// Each finger is followed separately, so the d-pad and A/B can be held together and a finger
// can slide over the d-pad. On the drawn console the same fingers press its buttons.
#include "player.h"
#include "../core/apu.h"
#include "platform.h"
#include "ui_common.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>

using namespace gb;

static const uint16_t TB_REWIND = 0x100, TB_FAST = 0x200, TB_MENU = 0x400;

bool GamePlayer::console_gbc() const {
    const std::string v = cfg.get("visual_console", "desligado");
    return v == "gbc" || (v == "auto" && cfg.get("estilo_console_auto", "gbc") == "gbc");
}

bool GamePlayer::touch_controls_on() const {
    const std::string v = cfg.get("controles_toque", "auto");
    if (v == "nunca" || con.on) return false;          // (the drawn console has its own buttons)
    if (v == "sempre") return true;
    // automatic: on phones from the start, elsewhere after the screen was touched; a keyboard or
    // controller makes them fade away until the next touch
    bool mobile = false;
#ifdef NB_MOBILE
    mobile = true;
#endif
    return (mobile || touch_seen) && !touch_hidden_at;
}

SDL_Rect GamePlayer::touch_game_area(const SDL_Rect& area) const {
    if (!touch_controls_on() || !portrait) return area;
    // standing up without the console: the picture at the top, the controls below it
    SDL_Rect r = area;
    r.h = std::min(area.h, (int)(area.w * 144.0 / 160.0) + area.h / 20);
    return r;
}

void GamePlayer::touch_layout(const SDL_Rect& area) {
    TouchPad& t = tpad;
    portrait = area.h > area.w;
    t.on = touch_controls_on();
    if (!t.on) return;
    const float size = std::max(60, std::min(150, cfg.geti("toque_tamanho", 100))) / 100.f;
    const float W = (float)area.w, H = (float)area.h, X = (float)area.x, Y = (float)area.y;
    const float m = std::min(W, H);
    if (!portrait) {
        t.dpad_r = m * 0.17f * size;
        t.dpad = {X + t.dpad_r * 1.25f + W * 0.02f, Y + H * 0.60f};
        t.ab_r = m * 0.085f * size;
        t.a = {X + W - t.ab_r * 1.5f - W * 0.02f, Y + H * 0.52f};
        t.b = {t.a.x - t.ab_r * 2.35f, t.a.y + t.ab_r * 1.05f};
        const float pw = m * 0.16f * size, ph = m * 0.065f * size;
        t.select = {X + W / 2 - pw - m * 0.02f, Y + H - ph - m * 0.03f, pw, ph};
        t.start = {X + W / 2 + m * 0.02f, Y + H - ph - m * 0.03f, pw, ph};
    } else {
        const SDL_Rect g = touch_game_area(area);
        const float top = (float)(g.y + g.h), room = Y + H - top;
        t.dpad_r = std::min(W * 0.21f, room * 0.30f) * size;
        t.dpad = {X + W * 0.27f, top + room * 0.42f};
        t.ab_r = std::min(W * 0.10f, room * 0.15f) * size;
        t.a = {X + W * 0.83f, top + room * 0.34f};
        t.b = {t.a.x - t.ab_r * 2.35f, t.a.y + t.ab_r * 1.05f};
        const float pw = W * 0.18f * size, ph = W * 0.065f * size;
        t.select = {X + W / 2 - pw - W * 0.03f, top + room * 0.82f, pw, ph};
        t.start = {X + W / 2 + W * 0.03f, top + room * 0.82f, pw, ph};
    }
    const float bs = m * 0.085f;
    t.menu = {X + W - bs - m * 0.025f, Y + m * 0.025f, bs, bs};
    t.rewind = {X + m * 0.025f, Y + m * 0.025f, bs * 1.3f, bs};
    t.fast = {t.rewind.x + t.rewind.w + m * 0.02f, t.rewind.y, bs * 1.3f, bs};
}

// buttons under a point. first = the finger just touched (the menu button only reacts then;
// a finger sliding from the d-pad may go a bit outside it and still press it)
uint16_t GamePlayer::touch_hit(float x, float y, bool first) const {
    if (con.on) {
        ConsoleHit h = console_hit(x, y);
        if (h.kind == ConsoleHit::BUTTON) return h.buttons;
        if (h.kind == ConsoleHit::MENU && first) return TB_MENU;
        return 0;
    }
    const TouchPad& t = tpad;
    if (!t.on) return 0;
    auto in = [&](const SDL_FRect& r, float grow) { return x >= r.x - grow && x < r.x + r.w + grow && y >= r.y - grow && y < r.y + r.h + grow; };
    const float g = std::min(t.ab_r, t.dpad_r) * 0.25f;
    if (first && in(t.menu, g)) return TB_MENU;
    if (in(t.rewind, g)) return TB_REWIND;
    if (in(t.fast, g)) return TB_FAST;
    if (in(t.start, g)) return BTN_START;
    if (in(t.select, g)) return BTN_SELECT;
    // d-pad: the direction of the finger from its centre (8 directions, diagonals included)
    const float dx = x - t.dpad.x, dy = y - t.dpad.y, d = std::sqrt(dx * dx + dy * dy);
    if (d < t.dpad_r * (first ? 1.15f : 1.6f)) {
        if (d < t.dpad_r * 0.18f) return 0;
        uint16_t b = 0;
        if (dx > d * 0.38f) b |= BTN_RIGHT;
        if (dx < -d * 0.38f) b |= BTN_LEFT;
        if (dy > d * 0.38f) b |= BTN_DOWN;
        if (dy < -d * 0.38f) b |= BTN_UP;
        return b;
    }
    // A and B; between them both at once
    auto dist = [&](SDL_FPoint p) { return std::sqrt((x - p.x) * (x - p.x) + (y - p.y) * (y - p.y)); };
    const float da = dist(t.a), db = dist(t.b), r = t.ab_r * (first ? 1.3f : 1.6f);
    const SDL_FPoint mid{(t.a.x + t.b.x) / 2, (t.a.y + t.b.y) / 2};
    if (dist(mid) < t.ab_r * 0.55f) return BTN_A | BTN_B;
    if (da < r && da <= db) return BTN_A;
    if (db < r) return BTN_B;
    return 0;
}

void GamePlayer::touch_event(const SDL_Event& e) {
    if (e.type == SDL_CONTROLLERBUTTONDOWN && fingers.empty()) {
        // a controller is being used: the touch controls get out of the way until the next touch
        touch_hidden_at = SDL_GetTicks();
        return;
    }
    if (e.type != SDL_FINGERDOWN && e.type != SDL_FINGERMOTION && e.type != SDL_FINGERUP) return;
    if (!touch_seen || touch_hidden_at) { touch_seen = true; touch_hidden_at = 0; frame_ready = true; }
    int ow = 1, oh = 1;
    if (ren) SDL_GetRendererOutputSize(ren, &ow, &oh);
    const float x = e.tfinger.x * ow, y = e.tfinger.y * oh;
    const int64_t id = (int64_t)e.tfinger.fingerId;
    const bool playing = !menu_open && !start_screen && !ask_continue && !splash_on;
    const uint8_t before = touch_bits;
    if (e.type == SDL_FINGERUP || !playing) fingers.erase(id);
    else {
        const bool first = e.type == SDL_FINGERDOWN;
        if (first || fingers.count(id)) {              // (a finger that started on a menu is not followed)
            const uint16_t b = touch_hit(x, y, first);
            if (b & TB_MENU) { menu_open = true; fingers.clear(); }
            else fingers[id] = b;
        }
    }
    if (!playing) fingers.clear();
    touch_bits = 0; rewind_touch_hold = fast_touch_hold = false;
    for (auto& f : fingers) {
        touch_bits |= (uint8_t)(f.second & 0xFF);
        if (f.second & TB_REWIND) rewind_touch_hold = true;
        if (f.second & TB_FAST) fast_touch_hold = true;
    }
    // a short buzz when a button is newly pressed (phones)
    if ((touch_bits & ~before) && haptic && cfg.getb("vibrar_toque", true)) SDL_HapticRumblePlay(haptic, 0.35f, 18);
}

void GamePlayer::draw_touch(ImDrawList* dl) {
    const TouchPad& t = tpad;
    if (!t.on || menu_open || start_screen || ask_continue || splash_on || power_state) return;
    const float op = std::max(15, std::min(100, cfg.geti("toque_opacidade", 55))) / 100.f;
    const uint8_t held = (uint8_t)(m.buttons | touch_bits);
    auto col = [&](int r, int g, int b, float a) { return IM_COL32(r, g, b, (int)(255 * a * op)); };
    const ImU32 line = col(255, 255, 255, 0.85f), fill = col(20, 20, 30, 0.55f), lit = col(140, 200, 255, 0.85f);
    // d-pad: a rounded cross
    const float r = t.dpad_r, w = r * 0.62f;
    const ImVec2 c(t.dpad.x, t.dpad.y);
    dl->AddCircleFilled(c, r * 1.05f, col(0, 0, 0, 0.25f), 48);
    dl->AddRectFilled(ImVec2(c.x - w / 2, c.y - r), ImVec2(c.x + w / 2, c.y + r), fill, w * 0.25f);
    dl->AddRectFilled(ImVec2(c.x - r, c.y - w / 2), ImVec2(c.x + r, c.y + w / 2), fill, w * 0.25f);
    auto arm = [&](uint8_t bit, float ax, float ay) {
        const ImVec2 p0(c.x + ax * (w / 2 + 2) - (ay ? w / 2 : 0), c.y + ay * (w / 2 + 2) - (ax ? w / 2 : 0));
        const ImVec2 a0(ax ? (ax > 0 ? c.x + w / 2 : c.x - r) : c.x - w / 2, ay ? (ay > 0 ? c.y + w / 2 : c.y - r) : c.y - w / 2);
        const ImVec2 a1(ax ? (ax > 0 ? c.x + r : c.x - w / 2) : c.x + w / 2, ay ? (ay > 0 ? c.y + r : c.y - w / 2) : c.y + w / 2);
        (void)p0;
        if (held & bit) dl->AddRectFilled(a0, a1, lit, w * 0.2f);
        // little triangle pointing out
        const float k = w * 0.22f, mx = (a0.x + a1.x) / 2, my = (a0.y + a1.y) / 2;
        if (ax) dl->AddTriangleFilled(ImVec2(mx + ax * k, my), ImVec2(mx - ax * k * 0.6f, my - k), ImVec2(mx - ax * k * 0.6f, my + k), line);
        else dl->AddTriangleFilled(ImVec2(mx, my + ay * k), ImVec2(mx - k, my - ay * k * 0.6f), ImVec2(mx + k, my - ay * k * 0.6f), line);
    };
    arm(BTN_RIGHT, 1, 0); arm(BTN_LEFT, -1, 0); arm(BTN_UP, 0, -1); arm(BTN_DOWN, 0, 1);
    dl->AddRect(ImVec2(c.x - w / 2, c.y - r), ImVec2(c.x + w / 2, c.y + r), line, w * 0.25f, 0, 2.f);
    dl->AddRect(ImVec2(c.x - r, c.y - w / 2), ImVec2(c.x + r, c.y + w / 2), line, w * 0.25f, 0, 2.f);
    // A and B
    const float fs = t.ab_r * 0.8f;
    auto round_button = [&](SDL_FPoint p, bool on, const char* label) {
        dl->AddCircleFilled(ImVec2(p.x, p.y), t.ab_r, on ? lit : fill, 40);
        dl->AddCircle(ImVec2(p.x, p.y), t.ab_r, line, 40, 2.f);
        ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0, label);
        dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x - ts.x / 2, p.y - ts.y / 2), line, label);
    };
    round_button(t.a, held & BTN_A, "A");
    round_button(t.b, held & BTN_B, "B");
    // Start, Select and the top buttons
    auto pill = [&](const SDL_FRect& rr, bool on, const char* label) {
        dl->AddRectFilled(ImVec2(rr.x, rr.y), ImVec2(rr.x + rr.w, rr.y + rr.h), on ? lit : fill, rr.h / 2);
        dl->AddRect(ImVec2(rr.x, rr.y), ImVec2(rr.x + rr.w, rr.y + rr.h), line, rr.h / 2, 0, 2.f);
        const float f2 = rr.h * 0.5f;
        ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(f2, 1e9f, 0, label);
        dl->AddText(ImGui::GetFont(), f2, ImVec2(rr.x + (rr.w - ts.x) / 2, rr.y + (rr.h - ts.y) / 2), line, label);
    };
    pill(t.select, held & BTN_SELECT, "SELECT");
    pill(t.start, held & BTN_START, "START");
    pill(t.menu, false, "≡");
    if (cfg.getb("rebobinar", true) && !fidelity()) pill(t.rewind, rewind_touch_hold, "<<");
    pill(t.fast, fast_touch_hold, ">>");
}

// the phone sends the app to the background (home button, a call): save everything as if closing,
// because Android may end the app without warning while it is in the background
void GamePlayer::suspend_save() {
    ahead_off();
    if (!running) return;
    if (m.cart.info.has_battery) {
        auto d = m.save_data();
        plat::write_file(plat::join(saves_dir(), game_id + ".sav"), d.data(), d.size());
    }
    if (!ask_continue && !start_screen && !fidelity() && cfg.getb("continuar", true) && !in_studio) {
        auto st = m.save_state();
        plat::write_file(state_path(0), st.data(), st.size());
        save_png(state_path(0) + ".png", m.fb, 160, 144);
        plat::write_text(state_path(0) + ".txt", plat::now_stamp());
    }
    save_play_time();
    scenery_save();
    save_achievements();
    cfg.save();
}
