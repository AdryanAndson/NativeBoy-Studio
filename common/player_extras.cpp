// GamePlayer extras: rewind, save-state slots with thumbnails, "continue" prompt,
// soundtrack replacement, cheat finder and colour-blind correction.
#include "player.h"
#include "platform.h"
#include "ui_common.h"
#include "../core/apu.h"
#include "imgui.h"
#include "delta.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace gb;

// ------------------------------------------------------------------ rewind
// Every second game frame a state is stored. Most are stored as the XOR difference to the last
// full state ("key"), run-length encoded, so a minute of rewind costs only a few megabytes.
// Each moment also keeps the sound played since the previous one (heard backwards while rewinding)
// and, once per second, a picture for the timeline in the pause menu.
static int16_t to_s16(float v) { v = std::max(-1.f, std::min(1.f, v)); return (int16_t)(v * 32767); }
// stored as mono at half the rate: it is only heard backwards, and 5 minutes then take about 13 MB
static void keep_sound(const std::vector<float>& out, size_t from, std::vector<int16_t>& dst) {
    from &= ~(size_t)1;
    for (size_t i = from; i + 3 < out.size(); i += 4) dst.push_back(to_s16((out[i] + out[i + 1] + out[i + 2] + out[i + 3]) * 0.25f));
}

void GamePlayer::rewind_audio_mark() {
    auto& out = m.apu->out;
    if (cfg.getb("rebobinar", true) && !fidelity()) keep_sound(out, rw_out_mark, rw_pending);
    if (rw_pending.size() > 48000) rw_pending.clear();                // nothing stored for a while: don't grow
    rw_out_mark = 0;
}

void GamePlayer::rewind_capture() {
    size_t cap = (size_t)std::max(10, std::min(300, cfg.geti("segundos_rebobinar", 60))) * 30;   // 30 states per second
    if (rewind_ring.size() != cap) { rewind_ring.assign(cap, Snap()); rw_head = rw_count = 0; rw_key.reset(); }
    std::vector<u8> st = m.save_state();
    Snap sn;
    if (!rw_key || rw_key->size() != st.size() || rw_since_key >= 60) {
        rw_key = std::make_shared<std::vector<u8>>(std::move(st));
        rw_since_key = 0;
        sn.key = rw_key;
    } else {
        sn.key = rw_key;
        rle_xor(st, *rw_key, sn.delta);
    }
    rw_since_key++;
    // sound since the previous moment
    auto& out = m.apu->out;
    sn.audio.swap(rw_pending);
    keep_sound(out, rw_out_mark, sn.audio);
    rw_out_mark = out.size();
    rw_pending.clear();
    // a picture once per second
    if (rw_since_thumb <= 0) {
        sn.thumb.resize(160 * 144);
        for (int i = 0; i < 160 * 144; i++) {
            u32 c = m.fb[i];
            sn.thumb[i] = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
        }
        rw_since_thumb = 30;
    }
    rw_since_thumb--;
    sn.when = play_seconds;
    rewind_ring[rw_head] = std::move(sn);
    rw_head = (rw_head + 1) % cap;
    if (rw_count < cap) rw_count++;
    rw_serial++;
}

double GamePlayer::rewind_seconds() const { return rw_count / 30.0; }

bool GamePlayer::rewind_step(int n) {
    if (rw_count == 0 || rewind_ring.empty()) return false;
    size_t cap = rewind_ring.size();
    Snap sn;
    std::vector<int16_t> sound;     // the sound of the moments skipped, played backwards
    for (int k = 0; k < n && rw_count > 0; k++) {
        rw_head = (rw_head + cap - 1) % cap;
        rw_count--;
        sn = std::move(rewind_ring[rw_head]);
        rewind_ring[rw_head] = Snap();
        sound.insert(sound.end(), sn.audio.rbegin(), sn.audio.rend());     // backwards
    }
    if (!sn.key) return false;
    std::vector<u8> st;
    if (sn.delta.empty()) st = *sn.key;
    else if (!un_rle_xor(sn.delta, *sn.key, st)) return false;
    bool ok = m.load_state(st, true);        // the remembered scenery (wide screen) stays: it is the same place
    // a saved state has no picture: one frame is drawn from that moment, so the screen shows the past while rewinding
    if (ok) { m.run_frame(native); m.apu->out.clear(); }
    // the next captures must not reference a key that was just rewound past
    rw_key.reset();
    rw_pending.clear(); rw_out_mark = 0;
    rw_since_thumb = 0;
    music_last = -1;
    if (ok) { play_seconds = sn.when; rw_back_seconds += n / 30.0; }
    rw_serial++;
    // squeezed into one screen step (16 ms) of stereo sound, a bit quieter
    if (audio && n <= 8 && !sound.empty() && cfg.getb("rebobinar_som", true) && SDL_GetQueuedAudioSize(audio) < (Uint32)(m.apu->sample_rate / 60 * 4 * sizeof(float) * 2)) {
        const size_t target = (size_t)m.apu->sample_rate * 16 / 1000;
        std::vector<float> f(target * 2);
        for (size_t i = 0; i < target; i++) {
            float v = sound[std::min(sound.size() - 1, i * sound.size() / target)] / 32768.f * 0.6f;
            f[i * 2] = f[i * 2 + 1] = v;
        }
        SDL_QueueAudio(audio, f.data(), (Uint32)(f.size() * sizeof(float)));
    }
    return ok;
}

bool GamePlayer::rewind_jump(size_t back) {
    if (back >= rw_count) return false;
    return rewind_step((int)back + 1);
}

void GamePlayer::draw_timeline_section() {
    if (!cfg.getb("rebobinar", true) || fidelity()) return;
    ImGui::SeparatorText("Voltar no tempo");
    if (rw_count == 0) { ImGui::TextDisabled("Jogue um pouco: os últimos momentos aparecem aqui para você voltar."); return; }
    const size_t cap = rewind_ring.size();
    auto idx_back = [&](size_t back) { return (rw_head + cap - 1 - back) % cap; };
    if (tl_serial != rw_serial) {         // timeline changed: forget the pictures
        for (auto& t : tl_tex) if (t.second) SDL_DestroyTexture(t.second);
        tl_tex.clear(); tl_serial = rw_serial; tl_sel = -1;
    }
    // moments with a picture, newest first
    std::vector<size_t> withpic;
    for (size_t b = 0; b < rw_count; b++) if (!rewind_ring[idx_back(b)].thumb.empty()) withpic.push_back(b);
    if (withpic.empty()) return;
    auto tex_for = [&](size_t back) -> SDL_Texture* {
        size_t i = idx_back(back);
        auto it = tl_tex.find(i);
        if (it != tl_tex.end()) return it->second;
        SDL_Texture* t = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STATIC, 160, 144) : nullptr;
        if (t) { SDL_UpdateTexture(t, nullptr, rewind_ring[i].thumb.data(), 160 * 2); SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest); }
        tl_tex[i] = t;
        return t;
    };
    const double now_t = play_seconds;
    auto ago = [&](size_t back) {
        int s = (int)std::lround(std::max(0.0, now_t - rewind_ring[idx_back(back)].when));
        char b[32]; if (s >= 60) snprintf(b, sizeof b, "-%d:%02d", s / 60, s % 60); else if (s > 0) snprintf(b, sizeof b, "-%d s", s); else snprintf(b, sizeof b, "agora");
        return std::string(b);
    };
    const float uis = std::max(100, std::min(200, cfg.geti("escala_interface", 100))) / 100.f;
    ImGui::TextDisabled("%.0f s guardados. Escolha uma imagem e volte para aquele momento.", rewind_seconds());
    // strip: up to 8 pictures spread over the stored time (oldest on the left)
    const float avail = ImGui::GetContentRegionAvail().x;
    const int nshow = std::max(2, std::min(8, std::min((int)withpic.size(), (int)(avail / (84 * uis)))));
    const ImVec2 fp = ImGui::GetStyle().FramePadding;
    const float tw = std::min(120.f * uis, (avail - ImGui::GetStyle().ItemSpacing.x * (nshow - 1)) / nshow - 1);
    const float imw = tw - 2 * fp.x, imh = imw * 144 / 160;
    for (int k = 0; k < nshow; k++) {
        size_t pick = withpic[(size_t)((withpic.size() - 1) * (nshow - 1 - k) / std::max(1, nshow - 1))];
        if (k) ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushID(k);
        SDL_Texture* t = tex_for(pick);
        bool sel = tl_sel == (int)pick;
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.47f, 0.78f, 1.f, 1.f));
        if (t && ImGui::ImageButton("m", ui::tex_ref(t), ImVec2(imw, imh))) tl_sel = (int)pick;
        if (sel) ImGui::PopStyleColor();
        std::string a = ago(pick);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (tw - ImGui::CalcTextSize(a.c_str()).x) / 2);
        if (sel) ImGui::TextUnformatted(a.c_str()); else ImGui::TextDisabled("%s", a.c_str());
        ImGui::PopID();
        ImGui::EndGroup();
    }
    if (tl_sel >= 0) {
        std::string label = "Voltar para este momento (" + ago((size_t)tl_sel) + ")";
        if (ImGui::Button(label.c_str())) {
            const std::string how_far = ago((size_t)tl_sel);
            if (rewind_jump((size_t)tl_sel)) { notify("Voltou no tempo (" + how_far + ")"); menu_open = false; frame_ready = true; }
            tl_sel = -1;
        }
        ImGui::SameLine(); ImGui::TextDisabled("O que veio depois é descartado.");
    }
}

// ------------------------------------------------------------------ save-state slots
std::string GamePlayer::state_path(int slot) const {
    return plat::join(saves_dir(), game_id + (slot == 0 ? std::string(".estado_auto") : ".estado" + std::to_string(slot)));
}

std::string GamePlayer::slot_info(int slot) const {
    std::string t;
    if (!plat::file_exists(state_path(slot))) return "";
    if (!plat::read_text(state_path(slot) + ".txt", t) || t.size() < 16) return "salvo";
    // 2026-11-07_08-42-00 -> 07/11/2026 08:42
    return t.substr(8, 2) + "/" + t.substr(5, 2) + "/" + t.substr(0, 4) + " " + t.substr(11, 2) + ":" + t.substr(14, 2);
}

SDL_Texture* GamePlayer::thumb(int slot) {
    auto it = thumbs.find(slot);
    if (it != thumbs.end()) return it->second;
    if (!ren) return nullptr;          // not drawn yet: try again next frame
    SDL_Texture* t = nullptr;
    std::vector<uint32_t> px; int w, h;
    if (load_png(state_path(slot) + ".png", px, w, h)) {
        t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (t) SDL_UpdateTexture(t, nullptr, px.data(), w * 4);
    }
    thumbs[slot] = t;
    return t;
}

void GamePlayer::drop_thumb(int slot) {
    auto it = thumbs.find(slot);
    if (it == thumbs.end()) return;
    if (it->second) SDL_DestroyTexture(it->second);
    thumbs.erase(it);
}

void GamePlayer::draw_states_section() {
    ImGui::SeparatorText("Estados salvos");
    const float tw = 168, th = 151;
    float avail = ImGui::GetContentRegionAvail().x;
    int per_row = std::max(1, (int)(avail / (tw + 24)));
    for (int s = 1; s <= 8; s++) {
        ImGui::PushID(s);
        if ((s - 1) % per_row) ImGui::SameLine();
        ImGui::BeginGroup();
        SDL_Texture* t = thumb(s);
        if (t) ImGui::Image((ImTextureID)(intptr_t)t, ImVec2(tw, th));
        else {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + tw, p.y + th), IM_COL32(120, 120, 120, 160), 4);
            ImGui::Dummy(ImVec2(tw, th));
        }
        std::string info = slot_info(s);
        ImGui::Text("%d: %s", s, info.empty() ? "vazio" : info.c_str());
        if (ImGui::Button("Salvar", ImVec2(tw / 2 - 2, 0))) save_state(s);
        ImGui::SameLine(0, 4);
        ImGui::BeginDisabled(info.empty());
        if (ImGui::Button("Carregar", ImVec2(tw / 2 - 2, 0))) { if (load_state(s)) { menu_open = false; start_screen = false; } }
        ImGui::EndDisabled();
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if (plat::file_exists(state_path(0))) {
        ImGui::TextDisabled("Salvamento automático de %s", slot_info(0).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Carregar##auto")) { if (load_state(0)) menu_open = false; }
    }
}

void GamePlayer::draw_continue_prompt(const SDL_Rect& area) {
    ImGui::SetNextWindowPos(ImVec2(area.x + area.w * 0.5f, area.y + area.h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Continuar?", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    float uis = std::max(100, std::min(200, cfg.geti("escala_interface", 100))) / 100.f;
    ImGui::SetWindowFontScale(uis * 1.15f);
    ImGui::Text("%s", game_name.c_str());
    ImGui::TextDisabled("Você parou em %s", slot_info(0).c_str());
    if (SDL_Texture* t = thumb(0)) ImGui::Image((ImTextureID)(intptr_t)t, ImVec2(240 * uis, 216 * uis));
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool go = ImGui::Button("Continuar de onde parei", ImVec2(240 * uis, 0));
    if (go) { ask_continue = false; load_state(0); }
    if (ImGui::Button("Começar do início", ImVec2(240 * uis, 0))) { ask_continue = false; notify("Começando do início (o save do cartucho continua valendo)"); }
    ImGui::TextDisabled("Controle: A confirma");
    ImGui::End();
}

// ------------------------------------------------------------------ soundtrack replacement
void GamePlayer::music_update() {
    if (music.empty()) return;
    int v = rd(m, (u16)music.addr);
    if (v == music_last) return;
    music_last = v;
    auto it = music.files.find(v);
    bool was = music_active;
    if (it != music.files.end()) {
        music_out.rate = m.apu->sample_rate;
        music_out.play(plat::join(music.dir, it->second), music.volume);
        music_active = music_out.playing();
    } else { music_out.stop(); music_active = false; }
    if (was != music_active) apply_mutes();
}

// ------------------------------------------------------------------ cheat finder
u8 GamePlayer::peek(u16 a) const { return rd(const_cast<Machine&>(m), a); }

static bool searchable(const Machine& m, u16 a) {
    if (a >= 0xC000 && a < 0xE000) return true;
    if (a >= 0xFF80 && a < 0xFFFF) return true;
    if (a >= 0xA000 && a < 0xC000) return m.cart.ram_enabled && !m.cart.ram.empty() && m.cart.info.mbc != 7 && m.cart.info.mbc < 0xFE;
    return false;
}

void GamePlayer::search_begin() {
    cands.clear();
    for (u32 a = 0xA000; a < 0x10000; a++)
        if (searchable(m, (u16)a)) cands.push_back({(u16)a, peek((u16)a)});
    search_started = true;
}

void GamePlayer::search_filter(int mode) {
    std::vector<Cand> out;
    for (auto& c : cands) {
        u8 v = peek(c.addr);
        bool keep = false;
        switch (mode) {
        case 0: keep = v == (u8)search_value; break;
        case 1: keep = v != c.prev; break;
        case 2: keep = v == c.prev; break;
        case 3: keep = v > c.prev; break;
        case 4: keep = v < c.prev; break;
        }
        if (keep) out.push_back({c.addr, v});
    }
    cands.swap(out);
}

void GamePlayer::add_user_cheat(u16 addr, u8 val) {
    char b[64];
    snprintf(b, sizeof b, "01%02X%02X%02X", val, addr & 0xFF, addr >> 8);
    std::string t;
    plat::read_text(plat::join(config_dir(), "cheats.txt"), t);
    if (t.empty()) t = "# Trapaças do jogador (formato GameShark 01VVAAAA). Apague uma linha para desligar.\n";
    if (!t.empty() && t.back() != '\n') t += '\n';
    t += std::string(b) + "\n";
    plat::write_text(plat::join(config_dir(), "cheats.txt"), t);
    for (auto& c : cheats) if (c.addr == addr) { c.val = val; return; }
    cheats.push_back({addr, val});
}

void GamePlayer::draw_cheat_tab() {
    if (fidelity()) { ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Modo fidelidade ativo: trapaças desligadas."); return; }
    ImGui::TextWrapped("Busca na memória: encontre onde o jogo guarda um número (vidas, dinheiro, energia) e trave o valor. "
                       "Exemplo: com 3 vidas, procure 3; perca uma vida, procure 2; repita até sobrar pouco endereço.");
    if (!search_started) {
        if (ImGui::Button("Começar uma busca", ImVec2(220, 0))) search_begin();
    } else {
        ImGui::Text("%zu endereço(s) possível(is)", cands.size());
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt("##valor", &search_value);
        search_value = std::max(0, std::min(255, search_value));
        ImGui::SameLine();
        if (ImGui::Button("Valor atual é este")) search_filter(0);
        if (ImGui::Button("Mudou")) search_filter(1);
        ImGui::SameLine(); if (ImGui::Button("Não mudou")) search_filter(2);
        ImGui::SameLine(); if (ImGui::Button("Aumentou")) search_filter(3);
        ImGui::SameLine(); if (ImGui::Button("Diminuiu")) search_filter(4);
        ImGui::SameLine(); if (ImGui::Button("Recomeçar")) search_begin();
        ImGui::TextDisabled("Dica: feche o menu, jogue até o número mudar, volte e filtre.");
        if (cands.size() <= 200 && ImGui::BeginTable("cands", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 180))) {
            ImGui::TableSetupColumn("Endereço"); ImGui::TableSetupColumn("Agora"); ImGui::TableSetupColumn("Novo valor"); ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();
            static int newval[0x10000];
            for (auto& c : cands) {
                ImGui::PushID(c.addr);
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%04X", c.addr);
                u8 v = peek(c.addr);
                ImGui::TableNextColumn(); ImGui::Text("%3d ($%02X)", v, v);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(90);
                if (newval[c.addr] == 0 && v) newval[c.addr] = v;
                ImGui::InputInt("##n", &newval[c.addr], 0);
                newval[c.addr] = std::max(0, std::min(255, newval[c.addr]));
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Mudar")) wr(m, c.addr, (u8)newval[c.addr]);
                ImGui::SameLine();
                if (ImGui::SmallButton("Travar")) { add_user_cheat(c.addr, (u8)newval[c.addr]); notify("Trapaça criada e salva"); }
                ImGui::PopID();
            }
            ImGui::EndTable();
        } else if (cands.size() > 200) ImGui::TextDisabled("Filtre mais para ver a lista (até 200 endereços).");
    }
    ImGui::SeparatorText("Trapaças ativas");
    if (cheats.empty()) ImGui::TextDisabled("Nenhuma.");
    for (size_t i = 0; i < cheats.size(); i++) {
        ImGui::PushID((int)i);
        ImGui::Text("Endereço %04X travado em %d", cheats[i].addr, cheats[i].val);
        ImGui::PopID();
    }
    if (ImGui::Button("Desligar as trapaças do jogador")) {
        plat::remove_file(plat::join(config_dir(), "cheats.txt"));
        std::vector<u8> r = rom_original; load_mods_and_rom(r);   // reloads only the cheats lists (the ROM copy is discarded)
        notify("Trapaças do jogador desligadas");
    }
    ImGui::SameLine();
    if (ImGui::Button("Abrir arquivo de trapaças")) { plat::make_dirs(config_dir()); if (!plat::file_exists(plat::join(config_dir(), "cheats.txt"))) plat::write_text(plat::join(config_dir(), "cheats.txt"), "# 01VVAAAA\n"); plat::open_in_explorer(plat::join(config_dir(), "cheats.txt")); }
}

// ------------------------------------------------------------------ colour-blind correction
// Daltonisation (Fidaner et al.): simulate the deficiency in LMS space, take the error, and move it
// to channels the viewer distinguishes. Being linear, it collapses into one 3x3 matrix.
void daltonize(uint32_t* px, size_t n, const std::string& type) {
    static std::string cached; static int M[9];
    if (type != cached) {
        const double rgb2lms[9] = {17.8824, 43.5161, 4.11935, 3.45565, 27.1554, 3.86714, 0.0299566, 0.184309, 1.46709};
        const double lms2rgb[9] = {0.0809444479, -0.130504409, 0.116721066, -0.0102485335, 0.0540193266, -0.113614708,
                                   -0.000365296938, -0.00412161469, 0.693511405};
        double sim[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        if (type == "protanopia") { double t[9] = {0, 2.02344, -2.52581, 0, 1, 0, 0, 0, 1}; memcpy(sim, t, sizeof t); }
        else if (type == "deuteranopia") { double t[9] = {1, 0, 0, 0.494207, 0, 1.24827, 0, 0, 1}; memcpy(sim, t, sizeof t); }
        else if (type == "tritanopia") { double t[9] = {1, 0, 0, 0, 1, 0, -0.395913, 0.801109, 0}; memcpy(sim, t, sizeof t); }
        auto mul = [](const double* a, const double* b, double* o) {
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) { o[r * 3 + c] = 0; for (int k = 0; k < 3; k++) o[r * 3 + c] += a[r * 3 + k] * b[k * 3 + c]; }
        };
        double t1[9], S[9];
        mul(sim, rgb2lms, t1); mul(lms2rgb, t1, S);              // RGB -> simulated RGB
        const double shift[9] = {0, 0, 0, 0.7, 1, 0, 0.7, 0, 1};  // where the lost information goes
        double E[9], F[9];
        for (int i = 0; i < 9; i++) E[i] = (i % 4 == 0 ? 1 : 0) - S[i];   // I - S
        mul(shift, E, F);
        for (int i = 0; i < 9; i++) M[i] = (int)std::lround(((i % 4 == 0 ? 1 : 0) + F[i]) * 1024);
        cached = type;
    }
    for (size_t i = 0; i < n; i++) {
        uint32_t c = px[i];
        int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
        int R = (M[0] * r + M[1] * g + M[2] * b) >> 10, G = (M[3] * r + M[4] * g + M[5] * b) >> 10, B = (M[6] * r + M[7] * g + M[8] * b) >> 10;
        R = R < 0 ? 0 : R > 255 ? 255 : R; G = G < 0 ? 0 : G > 255 ? 255 : G; B = B < 0 ? 0 : B > 255 ? 255 : B;
        px[i] = (c & 0xFF000000u) | ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
    }
}

// ------------------------------------------------------------------ achievements
// assets/conquistas.txt (or the Studio project's mods/conquistas.txt), one per line:
//   id|Nome|Descrição|ENDEREÇO(hex)|operador|valor        operador: = != > >= < <=
static int op_from(const std::string& s) {
    static const char* ops[] = {"=", "!=", ">", ">=", "<", "<="};
    for (int i = 0; i < 6; i++) if (s == ops[i]) return i;
    return 0;
}
static bool op_test(int op, int a, int b) {
    switch (op) { case 0: return a == b; case 1: return a != b; case 2: return a > b; case 3: return a >= b; case 4: return a < b; default: return a <= b; }
}

void GamePlayer::load_achievements() {
    achs.clear();
    std::string t;
    if (!plat::read_text(plat::join(base_dir, "assets/conquistas.txt"), t) && !studio_mods_dir.empty())
        plat::read_text(plat::join(studio_mods_dir, "conquistas.txt"), t);
    for (auto& line : split_lines(t)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f; size_t p = 0;
        for (;;) { size_t q = line.find('|', p); f.push_back(line.substr(p, q == std::string::npos ? std::string::npos : q - p)); if (q == std::string::npos) break; p = q + 1; }
        if (f.size() < 6) continue;
        Ach a; a.id = f[0]; a.name = f[1]; a.desc = f[2];
        a.addr = (u16)strtol(f[3].c_str(), nullptr, 16); a.op = op_from(trim(f[4])); a.val = atoi(f[5].c_str());
        achs.push_back(a);
    }
    std::string done;
    if (plat::read_text(plat::join(saves_dir(), game_id + ".conquistas"), done))
        for (auto& line : split_lines(done)) {
            size_t b = line.find('|');
            std::string id = line.substr(0, b);
            for (auto& a : achs) if (a.id == id) { a.done = true; a.when = b == std::string::npos ? "" : line.substr(b + 1); }
        }
}

void GamePlayer::save_achievements() {
    if (achs.empty()) return;
    std::string s;
    for (auto& a : achs) if (a.done) s += a.id + "|" + a.when + "\n";
    plat::write_text(plat::join(saves_dir(), game_id + ".conquistas"), s);
}

void GamePlayer::check_achievements() {
    if (achs.empty() || fidelity() || !cheats.empty()) return;     // trapaças desligam as conquistas
    for (auto& a : achs) {
        if (a.done) continue;
        bool cond = op_test(a.op, rd(m, a.addr), a.val);
        if (!cond) { a.armed = true; continue; }                    // must be false at least once first
        if (!a.armed || m.frames < 120) continue;                   // RAM still being initialised by the game
        a.done = true;
        a.when = plat::now_stamp();
        toasts.push_back({"Conquista desbloqueada!", a.name, SDL_GetTicks() + 5000});
        save_achievements();
    }
}

void GamePlayer::draw_achievements_tab() {
    if (achs.empty()) { ImGui::TextDisabled("Este jogo não tem conquistas (o criador pode adicionar no NativeBoy Studio)."); return; }
    int n = 0; for (auto& a : achs) n += a.done;
    ImGui::Text("%d de %zu conquistas", n, achs.size());
    ImGui::ProgressBar(achs.empty() ? 0 : (float)n / achs.size(), ImVec2(-1, 0));
    if (!cheats.empty()) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Com trapaças ativas, as conquistas não são registradas.");
    for (auto& a : achs) {
        ImGui::PushID(a.id.c_str());
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(ImVec2(p.x + 16, p.y + 18), 14, a.done ? IM_COL32(230, 180, 40, 255) : IM_COL32(70, 70, 80, 255));
        dl->AddCircleFilled(ImVec2(p.x + 16, p.y + 18), 9, a.done ? IM_COL32(255, 220, 110, 255) : IM_COL32(95, 95, 105, 255));
        ImGui::Dummy(ImVec2(36, 36)); ImGui::SameLine();
        ImGui::BeginGroup();
        if (a.done) ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "%s", a.name.c_str());
        else ImGui::TextDisabled("%s", a.name.c_str());
        std::string sub = a.desc;
        if (a.done && a.when.size() >= 16) sub += "   (" + a.when.substr(8, 2) + "/" + a.when.substr(5, 2) + "/" + a.when.substr(0, 4) + ")";
        ImGui::TextDisabled("%s", sub.c_str());
        ImGui::EndGroup();
        ImGui::PopID();
    }
}

// ------------------------------------------------------------------ play time
void GamePlayer::save_play_time() {
    if (in_studio) return;
    char b[32]; snprintf(b, sizeof b, "%.0f", play_seconds);
    plat::write_text(plat::join(saves_dir(), game_id + ".tempo"), b);
    play_saved = SDL_GetTicks();
}

std::string GamePlayer::play_time_text() const {
    int s = (int)play_seconds, h = s / 3600, mi = (s / 60) % 60;
    if (h > 0) return std::to_string(h) + " h " + std::to_string(mi) + " min";
    return std::to_string(std::max(mi, s > 0 ? 1 : 0)) + " min";
}

// ------------------------------------------------------------------ title screen
void GamePlayer::draw_start_screen(const SDL_Rect& area) {
    float uis = std::max(100, std::min(200, cfg.geti("escala_interface", 100))) / 100.f;
    float bw = 380 * uis;
    ImGui::SetNextWindowPos(ImVec2((float)area.x, (float)area.y));
    ImGui::SetNextWindowSize(ImVec2((float)area.w, (float)area.h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##inicio", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    float cx = area.w * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // the game's icon above the name
    if (!title_icon_tried && ren) {
        title_icon_tried = true;
        std::vector<uint32_t> px; int w = 0, h = 0;
        if (load_png(plat::join(base_dir, "assets/icone.png"), px, w, h)) {
            title_icon = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
            if (title_icon) { SDL_UpdateTexture(title_icon, nullptr, px.data(), w * 4); SDL_SetTextureBlendMode(title_icon, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(title_icon, w < 128 ? SDL_ScaleModeNearest : SDL_ScaleModeLinear); }
        }
    }
    float title_y = area.h * 0.14f;
    if (title_icon && area.h >= 380) {
        const float is = std::min(84.f * uis, area.h * 0.13f);
        const ImVec2 i0(area.x + cx - is / 2, area.y + area.h * 0.06f);
        dl->AddRectFilled(ImVec2(i0.x - 6, i0.y - 6), ImVec2(i0.x + is + 6, i0.y + is + 6), IM_COL32(20, 14, 40, 200), 14);
        dl->AddImageRounded((ImTextureID)(intptr_t)title_icon, i0, ImVec2(i0.x + is, i0.y + is), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 10);
        title_y = area.h * 0.06f + is + 14;
    }
    // title
    ImGui::PushFont(nullptr, std::min(64.f, area.h / 9.f) * uis);
    ImVec2 ts = ImGui::CalcTextSize(game_name.c_str());
    ImGui::SetCursorPos(ImVec2(cx - ts.x / 2 + 3, title_y + 3));
    ImGui::TextColored(ImVec4(0, 0, 0, 0.7f), "%s", game_name.c_str());
    ImGui::SetCursorPos(ImVec2(cx - ts.x / 2, title_y));
    ImGui::TextUnformatted(game_name.c_str());
    ImGui::PopFont();
    std::string sub = play_seconds >= 60 ? "Tempo de jogo: " + play_time_text() : std::string("Pronto para jogar");
    if (!achs.empty()) { int n = 0; for (auto& a : achs) n += a.done; sub += "   |   Conquistas: " + std::to_string(n) + "/" + std::to_string(achs.size()); }
    ImVec2 ss = ImGui::CalcTextSize(sub.c_str());
    ImGui::SetCursorPos(ImVec2(cx - ss.x / 2, ImGui::GetCursorPosY() + 4));
    ImGui::TextDisabled("%s", sub.c_str());

    ImGui::SetWindowFontScale(1.25f * uis);
    float y = std::max(ImGui::GetCursorPosY() + 30, area.h * 0.42f);
    bool has_auto = plat::file_exists(state_path(0));
    if (start_load) {
        ImGui::SetWindowFontScale(uis);
        ImGui::SetCursorPos(ImVec2(std::max(10.f, cx - 400 * uis), y));
        ImGui::BeginChild("load", ImVec2(std::min(800 * uis, area.w - 20.f), area.h - y - 20), ImGuiChildFlags_Borders);
        draw_states_section();
        if (ImGui::Button("Voltar", ImVec2(200, 0))) start_load = false;
        ImGui::EndChild();
        ImGui::End();
        if (!start_screen) start_load = false;      // a state was loaded: the game starts
        return;
    }
    // the selection moves with keyboard / controller (see handle_event) or the mouse
    enum { I_CONT, I_NEW, I_LOAD, I_OPT, I_QUIT };
    std::vector<std::pair<int, std::string>> items;
    if (has_auto) items.push_back({I_CONT, "Continuar  (" + slot_info(0) + ")"});
    items.push_back({I_NEW, "Novo jogo"});
    items.push_back({I_LOAD, "Carregar estado salvo"});
    items.push_back({I_OPT, "Opções"});
    items.push_back({I_QUIT, "Sair"});
    int n = (int)items.size();
    start_sel = ((start_sel + start_move) % n + n) % n;
    start_move = 0;
    int chosen = -1;
    if (start_go) { chosen = items[start_sel].first; start_go = false; }
    {   // a translucent panel with the colour stripe behind the options
        const float ph = n * (ImGui::GetFrameHeight() + 12) + 30, px0 = area.x + cx - bw / 2 - 26, py0 = area.y + y - 22;
        dl->AddRectFilled(ImVec2(px0, py0), ImVec2(px0 + bw + 52, py0 + ph), IM_COL32(18, 12, 40, 190), 18);
        const float sw = (bw + 52 - 36) / 5;
        for (int i = 0; i < 5; i++) dl->AddRectFilled(ImVec2(px0 + 18 + i * sw, py0 + 8), ImVec2(px0 + 18 + (i + 1) * sw, py0 + 11), ui::accent(i));
    }
    for (int i = 0; i < n; i++) {
        ImGui::SetCursorPos(ImVec2(cx - bw / 2, y));
        bool sel = i == start_sel;
        if (sel) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.62f, 0.48f, 1));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.9f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        }
        if (ImGui::Button(items[i].second.c_str(), ImVec2(bw, 0))) chosen = items[i].first;
        ImVec2 md = ImGui::GetIO().MouseDelta;
        if (ImGui::IsItemHovered() && (md.x != 0 || md.y != 0)) start_sel = i;
        if (sel) { ImGui::PopStyleVar(); ImGui::PopStyleColor(2); }
        y += ImGui::GetFrameHeight() + 12;
    }
    switch (chosen) {
    case I_CONT: load_state(0); start_screen = false; break;
    case I_NEW: reset(); start_screen = false; notify(has_auto ? "Novo jogo (o save do cartucho continua valendo)" : "Bom jogo!"); break;
    case I_LOAD: start_load = true; break;
    case I_OPT: menu_open = true; want_tab = 1; break;
    case I_QUIT: want_quit = true; break;
    default: break;
    }
    ImGui::SetWindowFontScale(uis);
#ifdef NB_MOBILE
    const char* hint = "Toque numa opção    Voltar do celular: opções";
#else
    const char* hint = "Setas ou direcional: escolher    Enter, A ou Start: confirmar    Esc: opções";
#endif
    ImVec2 hs = ImGui::CalcTextSize(hint);
    ImGui::SetCursorPos(ImVec2(cx - hs.x / 2, area.h - hs.y - 18));
    ImGui::TextDisabled("%s", hint);
    ImGui::End();
}

// ------------------------------------------------------------------ bloom
// bright parts, shrunk to 1/4 and stretched back with smooth filtering, added on top of the picture
void GamePlayer::render_bloom(SDL_Renderer* r, const SDL_Rect& dst) {
    int bw = vs.base_w / 4, bh = vs.base_h / 4;
    if (bw < 1 || bh < 1) return;
    if (!bloom_tex || bloom_w != bw || bloom_h != bh) {
        if (bloom_tex) SDL_DestroyTexture(bloom_tex);
        bloom_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, bw, bh);
        if (!bloom_tex) return;
        SDL_SetTextureScaleMode(bloom_tex, SDL_ScaleModeLinear);
        SDL_SetTextureBlendMode(bloom_tex, SDL_BLENDMODE_ADD);
        bloom_w = bw; bloom_h = bh;
    }
    static std::vector<uint32_t> px;
    px.assign((size_t)bw * bh, 0);
    const uint32_t* s = vs.base_px;
    int W = vs.base_w;
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            unsigned R = 0, G = 0, B = 0;
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++) {
                    uint32_t c = s[(size_t)(y * 4 + j) * W + x * 4 + i];
                    int r_ = (c >> 16) & 255, g_ = (c >> 8) & 255, b_ = c & 255;
                    int l = (r_ * 77 + g_ * 150 + b_ * 29) >> 8;
                    if (l > 150) { int k = (l - 150) * 2; R += r_ * k >> 8; G += g_ * k >> 8; B += b_ * k >> 8; }
                }
            px[(size_t)y * bw + x] = 0xFF000000u | (std::min(255u, R / 16) << 16) | (std::min(255u, G / 16) << 8) | std::min(255u, B / 16);
        }
    SDL_UpdateTexture(bloom_tex, nullptr, px.data(), bw * 4);
    int a = std::max(0, std::min(100, cfg.geti("brilho", 0)));
    SDL_SetTextureAlphaMod(bloom_tex, (Uint8)(a * 255 / 100));
    // a little bigger than the picture so the glow spreads past the edges of bright areas
    SDL_Rect g{dst.x - dst.w / (bw * 2), dst.y - dst.h / (bh * 2), dst.w + dst.w / bw, dst.h + dst.h / bh};
    SDL_RenderCopy(r, bloom_tex, nullptr, &g);
}

// ------------------------------------------------------------------ recording (GIF / video) and gallery
std::string GamePlayer::captures_dir() const { return plat::join(user_dir.empty() ? base_dir : user_dir, "capturas"); }

static void frame_source(const Machine& m, const uint32_t*& px, int& w, int& h) {
    if (m.widescreen) { px = m.fbw; w = Machine::WIDE_W; } else { px = m.fb; w = 160; }
    h = 144;
}

void GamePlayer::toggle_gif() {
    if (gif.on()) { gif.stop(); notify("GIF salvo: " + plat::file_name(gif.path)); return; }
    const uint32_t* px; int w, h; frame_source(m, px, w, h);
    plat::make_dirs(captures_dir());
    if (gif.start(plat::join(captures_dir(), game_id + "_" + plat::now_stamp() + ".gif"), w * 2, h * 2)) notify("Gravando GIF (até 1 minuto). F6 para parar.");
    else notify("Não foi possível criar o GIF");
}

void GamePlayer::toggle_video() {
    if (video.on()) { video.stop(); notify("Vídeo salvo: " + plat::file_name(video.path)); return; }
    const uint32_t* px; int w, h; frame_source(m, px, w, h);
    plat::make_dirs(captures_dir());
    if (video.start(plat::join(captures_dir(), game_id + "_" + plat::now_stamp() + ".avi"), w * 3, h * 3, m.apu->sample_rate)) notify("Gravando vídeo com som. F7 para parar.");
    else notify("Não foi possível criar o vídeo");
}

void GamePlayer::record_frame() {
    const uint32_t* px; int w, h; frame_source(m, px, w, h);
    if (gif.on()) { gif.add_frame(px, w, h, 2); if (!gif.on()) notify("GIF salvo (limite de 1 minuto)"); }
    if (video.on()) { video.add_frame(px, w, h, 3); if (!video.on()) notify("Vídeo salvo"); }
}

void GamePlayer::draw_gallery_tab() {
    std::string dir = captures_dir();
    ImGui::TextWrapped("Capturas de tela (F12), GIFs (F6), vídeos com som (F7) e áudio gravado ficam na pasta capturas do jogo. "
                       "Os vídeos são .avi (abrem no Filmes e TV/Media Player do Windows, no VLC e em editores de vídeo).");
    if (ImGui::Button("Abrir pasta de capturas")) { plat::make_dirs(dir); plat::open_in_explorer(dir); }
    ImGui::SameLine();
    if (ImGui::Button(gif.on() ? "Parar GIF (F6)" : "Gravar GIF (F6)")) { toggle_gif(); menu_open = false; }
    ImGui::SameLine();
    if (ImGui::Button(video.on() ? "Parar vídeo (F7)" : "Gravar vídeo (F7)")) { toggle_video(); menu_open = false; }
    std::vector<std::string> files;
    for (auto& f : plat::list_dir(dir, false)) {
        std::string l = f; for (auto& c : l) c = (char)tolower((unsigned char)c);
        if (l.size() > 4 && (l.rfind(".png") == l.size() - 4 || l.rfind(".gif") == l.size() - 4 || l.rfind(".avi") == l.size() - 4 || l.rfind(".wav") == l.size() - 4)) files.push_back(f);
    }
    std::sort(files.begin(), files.end(), [&](const std::string& a, const std::string& b) { return plat::file_mtime(plat::join(dir, a)) > plat::file_mtime(plat::join(dir, b)); });
    if (files.empty()) { ImGui::TextDisabled("Nenhuma captura ainda."); return; }
    ImGui::BeginChild("gal", ImVec2(0, 0));
    const float tw = 176, th = 158;
    int per_row = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (tw + 16)));
    int shown = 0, del = -1;
    for (size_t i = 0; i < files.size() && shown < 60; i++, shown++) {
        const std::string full = plat::join(dir, files[i]);
        ImGui::PushID((int)i);
        if (i % per_row) ImGui::SameLine();
        ImGui::BeginGroup();
        std::string ext = files[i].substr(files[i].size() - 3);
        SDL_Texture* t = nullptr;
        if (ren && (ext == "png" || ext == "gif" || ext == "PNG" || ext == "GIF")) {
            auto it = gallery_thumbs.find(full);
            if (it != gallery_thumbs.end()) t = it->second;
            else {
                std::vector<uint32_t> px; int w, h;
                if (load_png(full, px, w, h)) {    // stb also reads the first frame of a GIF
                    t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
                    if (t) SDL_UpdateTexture(t, nullptr, px.data(), w * 4);
                }
                gallery_thumbs[full] = t;
            }
        }
        if (t) ImGui::Image((ImTextureID)(intptr_t)t, ImVec2(tw, th));
        else {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + tw, p.y + th), IM_COL32(40, 42, 50, 255), 6);
            const char* lbl = ext == "avi" ? "VÍDEO" : ext == "wav" ? "ÁUDIO" : "ARQUIVO";
            ImVec2 ts = ImGui::CalcTextSize(lbl);
            if (ext == "avi") dl->AddTriangleFilled(ImVec2(p.x + tw / 2 - 14, p.y + th / 2 - 30), ImVec2(p.x + tw / 2 - 14, p.y + th / 2 + 2), ImVec2(p.x + tw / 2 + 16, p.y + th / 2 - 14), IM_COL32(230, 230, 230, 255));
            dl->AddText(ImVec2(p.x + (tw - ts.x) / 2, p.y + th / 2 + 14), IM_COL32(200, 200, 200, 255), lbl);
            ImGui::Dummy(ImVec2(tw, th));
        }
        std::string label = files[i];
        if (label.size() > 24) label = "..." + label.substr(label.size() - 21);
        ImGui::TextDisabled("%s", label.c_str());
        if (ImGui::SmallButton("Abrir")) plat::open_url(full);
        ImGui::SameLine();
        if (ImGui::SmallButton("Apagar")) del = (int)i;
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (del >= 0) {
        std::string full = plat::join(dir, files[del]);
        auto it = gallery_thumbs.find(full);
        if (it != gallery_thumbs.end()) { if (it->second) SDL_DestroyTexture(it->second); gallery_thumbs.erase(it); }
        plat::remove_file(full);
    }
}

// ------------------------------------------------------------------ top bar (appears when the mouse goes to the top)
void GamePlayer::draw_topbar(const SDL_Rect& area) {
    if (in_studio || !cfg.getb("barra_superior", true) || mouse_idle) return;
    ImVec2 mp = ImGui::GetIO().MousePos;
    static float shown = 0;
    bool want = mp.y >= area.y && mp.y < area.y + 70 && mp.x >= area.x && mp.x < area.x + area.w;
    shown = std::max(0.f, std::min(1.f, shown + (want ? 0.2f : -0.1f)));
    if (shown <= 0) return;
    ImGui::SetNextWindowPos(ImVec2((float)area.x, (float)area.y));
    ImGui::SetNextWindowSize(ImVec2((float)area.w, 0));
    ImGui::SetNextWindowBgAlpha(0.78f * shown);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, shown);
    ImGui::Begin("##barra", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    if (ImGui::Button("Menu")) menu_open = true;
    ImGui::SameLine(); if (ImGui::Button(paused ? "Continuar" : "Pausar")) paused = !paused;
    ImGui::SameLine(); if (ImGui::Button("Salvar estado")) save_state(1);
    ImGui::SameLine(); if (ImGui::Button("Carregar")) load_state(1);
    ImGui::SameLine(); if (ImGui::Button("Captura")) screenshot();
    ImGui::SameLine(); if (ImGui::Button(video.on() ? "Parar vídeo" : "Gravar vídeo")) toggle_video();
    ImGui::SameLine(); if (ImGui::Button(cfg.getb("fullscreen", false) ? "Sair da tela cheia" : "Tela cheia")) want_fullscreen_toggle = true;
    std::string right = game_name + (emulation ? "  ·  emulação" : "  ·  nativo") + (play_seconds >= 60 ? "  ·  " + play_time_text() : "");
    float rw = ImGui::CalcTextSize(right.c_str()).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20, (float)area.w - rw - 20));
    ImGui::TextDisabled("%s", right.c_str());
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ------------------------------------------------------------------ remembered scenery on disk
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);
extern "C" char* stbi_zlib_decode_malloc(const char* buffer, int len, int* outlen);

void GamePlayer::scenery_save() {
    if (in_studio || fidelity() || !m.widescreen || !cfg.getb("cenario_salvo", true) || game_id.empty()) return;
    std::vector<u8> d = m.wide_export();
    if (d.size() < 64 || d.size() > 200u * 1024 * 1024) return;
    int clen = 0;
    unsigned char* c = stbi_zlib_compress(d.data(), (int)d.size(), &clen, 5);
    if (!c) return;
    std::vector<u8> out(8 + (size_t)clen);
    uint32_t raw = (uint32_t)d.size();
    memcpy(out.data(), "NBCZ", 4); memcpy(out.data() + 4, &raw, 4); memcpy(out.data() + 8, c, (size_t)clen);
    free(c);
    plat::make_dirs(saves_dir());
    plat::write_file(plat::join(saves_dir(), game_id + ".cenario"), out.data(), out.size());
}

void GamePlayer::scenery_load() {
    if (in_studio || fidelity() || !cfg.getb("cenario_salvo", true) || game_id.empty()) return;
    std::vector<u8> in;
    if (!plat::read_file(plat::join(saves_dir(), game_id + ".cenario"), in) || in.size() < 16 || memcmp(in.data(), "NBCZ", 4)) return;
    int n = 0;
    char* raw = stbi_zlib_decode_malloc((const char*)in.data() + 8, (int)in.size() - 8, &n);
    if (!raw) return;
    std::vector<u8> d((u8*)raw, (u8*)raw + n);
    free(raw);
    m.wide_import(d);
}

// ------------------------------------------------------------------ link cable (menu)
void GamePlayer::draw_link_section(float uis) {
    const int st = netlink.status();
    const ImVec4 ok_col(0.55f, 0.9f, 0.6f, 1), warn_col(1.f, 0.8f, 0.4f, 1), bad_col(1.f, 0.5f, 0.5f, 1);
    auto copy_button = [&](const char* id, const std::string& text) {
        ImGui::SameLine();
        if (ImGui::SmallButton(id)) { SDL_SetClipboardText(text.c_str()); notify("Copiado: " + text); }
    };
    if (st == 0) {
        if (!link_scanning) link_scanning = netlink.lan_scan_start();
        if (ImGui::Button("Hospedar (o outro se conecta a mim)")) {
            std::string e;
            if (netlink.host(NB_LINK_PORT, e)) notify("Aguardando o outro jogador");
            else notify(e);
        }
        ImGui::TextDisabled("Entrar no jogo de um amigo: cole o código de convite dele (ou o endereço IP).");
        ImGui::SetNextItemWidth(std::min(260.f * uis, ImGui::GetContentRegionAvail().x * 0.6f));
        bool enter = ImGui::InputTextWithHint("##convite", "NB-XXXXX-XXXXX-X  ou  192.168.0.10", link_addr, sizeof link_addr, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Conectar") || enter) && link_addr[0]) {
            std::string ip, e; int port = NB_LINK_PORT;
            if (!NetLink::parse_invite(link_addr, ip, port)) notify("Código inválido: confira as letras (o código tem o formato NB-XXXXX-XXXXX-X).");
            else if (netlink.connect_to(ip, port, e)) { notify("Cabo link conectado"); netlink.lan_scan_stop(); link_scanning = false; }
            else notify(e);
        }
        auto lan = netlink.lan_games();
        if (link_scanning) {
            if (lan.empty()) ImGui::TextDisabled("Ninguém hospedando este jogo na sua rede (Wi-Fi) agora.");
            for (size_t i = 0; i < lan.size(); i++) {
                ImGui::PushID((int)i);
                ImGui::TextColored(ok_col, "Na sua rede: %s em %s", lan[i].name.c_str(), lan[i].ip.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Entrar")) {
                    std::string e;
                    if (netlink.connect_to(lan[i].ip, lan[i].port, e)) { notify("Cabo link conectado"); netlink.lan_scan_stop(); link_scanning = false; }
                    else notify(e);
                }
                ImGui::PopID();
            }
        }
        return;
    }
    if (link_scanning) { netlink.lan_scan_stop(); link_scanning = false; }
    if (st == 1) {
        ImGui::TextWrapped("%s", netlink.status_text().c_str());
        auto ips = NetLink::local_ips();
        if (!ips.empty()) {
            std::string c = NetLink::invite_code(ips[0], NB_LINK_PORT);
            ImGui::TextWrapped("Na mesma rede (Wi-Fi), o outro jogador vê você na lista dele. Ou mande este código:");
            ImGui::Text("Rede local: %s", c.c_str());
            copy_button("Copiar##l", c);
        }
        const int us = netlink.upnp_state();
        if (us == 0 || us == 3) {
            if (ImGui::Button("Abrir a porta no roteador (para jogar pela internet)")) netlink.upnp_open_async(NB_LINK_PORT);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pede ao roteador (UPnP) para deixar o outro jogador chegar até você pela internet.\nA porta é fechada de novo quando o jogo termina.");
        }
        if (us == 1) ImGui::TextDisabled("Falando com o roteador...");
        if (us != 0) {
            std::string msg = netlink.upnp_message();
            ImGui::PushStyleColor(ImGuiCol_Text, us == 2 && !NetLink::is_private_ip(netlink.external_ip()) ? ok_col : us == 1 ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : warn_col);
            ImGui::TextWrapped("%s", msg.c_str());
            ImGui::PopStyleColor();
        }
        std::string ext = netlink.external_ip();
        if (us == 2 && !ext.empty() && !NetLink::is_private_ip(ext)) {
            std::string c = NetLink::invite_code(ext, NB_LINK_PORT);
            ImGui::Text("Internet: %s", c.c_str());
            copy_button("Copiar##i", c);
            ImGui::TextDisabled("Mande este código ao amigo; ele cola em \"Entrar no jogo de um amigo\".");
        }
        if (ImGui::Button("Cancelar")) netlink.close();
        return;
    }
    ImGui::TextColored(ok_col, "%s", netlink.status_text().c_str());
    if (netlink.partner_game_differs()) ImGui::TextColored(bad_col, "O outro jogador está com outro jogo (ou outra versão dele): a troca pode não funcionar.");
    const int p = netlink.ping_ms();
    if (p > 80) ImGui::TextColored(warn_col, "Atraso alto (%d ms): trocas e batalhas ficam lentas e alguns jogos podem desistir da conexão.", p);
    else if (p >= 0) ImGui::TextDisabled("Conexão boa para o cabo link.");
    if (ImGui::Button("Desconectar o cabo")) { netlink.close(); m.link = nullptr; }
}
