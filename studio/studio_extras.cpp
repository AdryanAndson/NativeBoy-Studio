#include "studio_extras.h"
#include "../common/ui_common.h"
#include "project.h"
#include "translate.h"
#include "../common/platform.h"
#include "../common/player.h"
#include "../common/music.h"
#include "../common/video.h"
#include "../common/settings.h"
#include "../recomp/disasm.h"
#include "../core/optable.h"
#include "../core/apu.h"
#include "imgui.h"
#include <algorithm>
#include <map>
#include <memory>
#include <array>
#include <cstring>
#include <cmath>

using namespace gb;

static const ImVec4 X_OK(0.35f, 0.85f, 0.55f, 1), X_WARN(1.0f, 0.78f, 0.30f, 1), X_ERR(1.0f, 0.42f, 0.38f, 1), X_DIM(0.6f, 0.62f, 0.66f, 1);

static std::vector<u8> original_rom(Project& p) {
    std::vector<u8> o;
    if (!plat::read_file(p.path("rom/" + p.ini.get("rom_original", "original.gb")), o)) o = p.rom;
    return o;
}

// big text editor helper (ImGui needs a resizable char buffer)
struct TextBuf {
    std::vector<char> b;
    void set(const std::string& s) { b.assign(s.begin(), s.end()); b.push_back(0); b.resize(std::max<size_t>(b.size() * 2, 4096), 0); }
    std::string str() const { return std::string(b.data()); }
};
static int textbuf_cb(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* tb = (TextBuf*)d->UserData;
        tb->b.resize((size_t)d->BufTextLen + 4096);
        d->Buf = tb->b.data();
    }
    return 0;
}
static bool edit_text(const char* id, TextBuf& tb, ImVec2 size) {
    if (tb.b.empty()) tb.set("");
    return ImGui::InputTextMultiline(id, tb.b.data(), tb.b.size(), size, ImGuiInputTextFlags_CallbackResize, textbuf_cb, &tb);
}

// ================================================================== translation
namespace {
struct TextRow { std::string prefix, orig, cur; size_t off = 0, cap = 0; int ptrs = -1; };
std::vector<TextRow> parse_text_rows(const std::string& file, const std::vector<u8>& rom, const TextTable& t) {
    std::vector<TextRow> rows;
    for (auto& line : split_lines(file)) {
        if (line.empty() || line[0] != '@') continue;
        unsigned bk, ad; size_t cap; size_t bar = line.find('|');
        if (bar == std::string::npos || sscanf(line.c_str(), "@%x:%x[%zu]", &bk, &ad, &cap) != 3) continue;
        TextRow r; r.prefix = line.substr(0, bar + 1); r.cur = line.substr(bar + 1); r.cap = cap;
        r.off = ad < 0x4000 ? ad : (size_t)bk * 0x4000 + (ad - 0x4000);
        if (r.off + cap <= rom.size()) r.orig = t.decode(&rom[r.off], cap);
        rows.push_back(r);
    }
    return rows;
}
std::string rows_to_file(const std::vector<TextRow>& rows) {
    std::string s = "# NativeBoy Studio - textos extraídos da ROM.\n"
                    "# Edite só o que vem depois de \"|\". O número entre [] é o espaço original em bytes.\n"
                    "# Textos maiores podem ser movidos para espaço livre (opção \"Mover textos maiores\"). {XX} = byte especial.\n";
    for (auto& r : rows) s += r.prefix + r.cur + "\n";
    return s;
}
} // namespace

void draw_translation_tab(Project& p, const StudioMsg& msg) {
    static std::string proj_dir;
    static char word[64] = "", filter[64] = "";
    static std::vector<RelHit> hits;
    static std::vector<TableGuess> guesses;
    static bool guessed = false, raw_view = false, relocate = true, only_changed = false;
    static TextBuf tbl, texts;
    static TextTable table;
    static std::vector<TextRow> rows;
    static std::vector<std::string> report, notes;
    static int min_len = 4, inserted = -1;
    static bool to_rom = false;
    const std::string dir = p.path("mods/traducao");
    std::vector<u8> orig = original_rom(p);
    auto reload_rows = [&]() { TextTable t; t.load_text(tbl.str()); rows = parse_text_rows(texts.str(), orig, t); };
    auto save_texts = [&]() { plat::make_dirs(dir); plat::write_text(plat::join(dir, "textos.txt"), texts.str()); };
    auto use_table = [&](const TextTable& t) {
        table = t;
        tbl.set(table.to_text());
        plat::make_dirs(dir); plat::write_text(plat::join(dir, "tabela.tbl"), tbl.str());
        reload_rows();
    };
    if (proj_dir != p.dir) {     // project changed: reload
        proj_dir = p.dir; hits.clear(); guesses.clear(); guessed = false; report.clear(); notes.clear(); inserted = -1;
        std::string t; plat::read_text(plat::join(dir, "tabela.tbl"), t); tbl.set(t); table.load_text(t);
        std::string x; plat::read_text(plat::join(dir, "textos.txt"), x); texts.set(x);
        reload_rows();
    }

    ImGui::TextWrapped("Traduza ou troque os textos do jogo. Os jogos de Game Boy não guardam letras em ASCII: cada jogo tem sua própria "
                       "tabela de letras. O Studio tenta descobrir essa tabela sozinho.");
    ImGui::SeparatorText("1. Tabela de letras");
    if (ImGui::Button("Detectar a tabela sozinho")) { guesses = auto_detect_tables(orig); guessed = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Procura palavras comuns de jogos (START, PRESS, GAME, THE, YOU...) em todas as posições possíveis do alfabeto.\nFunciona com jogos em inglês e outras línguas com o alfabeto latino; jogos em japonês usam outra escrita.");
    if (guessed && guesses.empty()) { ImGui::SameLine(); ImGui::TextColored(X_WARN, "Não achei. Procure por uma palavra que aparece na tela (abaixo)."); }
    for (size_t i = 0; i < guesses.size(); i++) {
        ImGui::PushID((int)i + 1000);
        const TableGuess& g = guesses[i];
        if (ImGui::SmallButton(i == 0 ? "Usar (mais provável)" : "Usar")) {
            use_table(table_from_guess(g));
            msg.notify("Tabela criada. Confira e complete letras com acento, números e pontuação, se precisar.");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%d palavras conhecidas:", g.words);
        ImGui::SameLine();
        ImGui::TextUnformatted(g.sample.empty() ? "(sem textos longos)" : g.sample.c_str());
        ImGui::PopID();
    }
    if (ImGui::TreeNode("Procurar por uma palavra que aparece no jogo")) {
        ImGui::SetNextItemWidth(260);
        ImGui::InputTextWithHint("##palavra", "Palavra do jogo (ex.: START)", word, sizeof word);
        ImGui::SameLine();
        if (ImGui::Button("Procurar")) {
            hits = relative_search(orig, word);
            hits.erase(std::remove_if(hits.begin(), hits.end(), [](const RelHit& h) { return h.off >= 0x134 && h.off < 0x144; }), hits.end());   // header title is ASCII
            if (hits.empty()) msg.notify("Nada encontrado. Tente outra palavra com 4+ letras, do jeito que aparece (maiúsculas/minúsculas).");
        }
        ImGui::TextDisabled("Funciona porque as letras quase sempre ficam em ordem alfabética na tabela (A, B, C...).");
        if (!hits.empty() && ImGui::BeginTable("hits", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 150))) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 130);
            ImGui::TableSetupColumn("Onde", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Prévia do texto ao redor");
            ImGui::TableHeadersRow();
            bool upper = true; for (char c : std::string(word)) if (islower((unsigned char)c)) upper = false;
            for (size_t i = 0; i < hits.size(); i++) {
                ImGui::PushID((int)i);
                TextTable t = upper ? table_from_bases(hits[i].base, -1, -1, -1, -1) : table_from_bases(-1, hits[i].base, -1, -1, -1);
                size_t a0 = hits[i].off > 8 ? hits[i].off - 8 : 0, n = std::min<size_t>(40, orig.size() - a0);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool use = ImGui::SmallButton("Usar esta tabela");
                ImGui::TableNextColumn(); ImGui::Text("%02X:%04X", (unsigned)(hits[i].off / 0x4000), (unsigned)(hits[i].off < 0x4000 ? hits[i].off : 0x4000 + hits[i].off % 0x4000));
                ImGui::TableNextColumn(); ImGui::TextUnformatted(t.decode(&orig[a0], n).c_str());
                if (use) {
                    int sp = guess_space(orig, t);
                    if (sp >= 0) t.chars[(u8)sp] = " ";
                    int te = guess_terminator(orig, t);
                    if (te >= 0) t.ends.push_back((u8)te);
                    use_table(t);
                    msg.notify("Tabela criada. Confira e complete na caixa abaixo.");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Tabela (tabela.tbl)")) {
        ImGui::TextDisabled("Uma linha por byte: 80=A. Linhas com / marcam o fim de um texto (/50=<FIM>). Complete letras com acento, números e pontuação.");
        edit_text("##tbl", tbl, ImVec2(-1, 140));
        if (ImGui::Button("Salvar tabela")) {
            if (!table.load_text(tbl.str())) msg.error("Tabela", "A tabela está vazia ou em formato inválido.");
            else { plat::make_dirs(dir); plat::write_text(plat::join(dir, "tabela.tbl"), tbl.str()); reload_rows(); msg.notify("Tabela salva"); }
        }
        ImGui::TreePop();
    }
    if (!table.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("Tabela: %zu caracteres%s", table.chars.size(), table.ends.empty() ? ", sem fim de texto" : "");
    }

    ImGui::SeparatorText("2. Textos do jogo");
    ImGui::BeginDisabled(table.empty());
    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("Tamanho mínimo", &min_len, 3, 12);
    ImGui::SameLine();
    if (ImGui::Button("Extrair textos da ROM")) {
        auto e = dump_texts(orig, table, min_len);
        texts.set(entries_to_file(e));
        save_texts(); reload_rows();
        msg.notify(std::to_string(e.size()) + " textos extraídos");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Abrir pasta")) { plat::make_dirs(dir); plat::open_in_explorer(dir); }
    ImGui::SameLine();
    if (ImGui::Checkbox("Editar como arquivo", &raw_view) && !raw_view) reload_rows();
    if (raw_view) {
        ImGui::TextDisabled("Cada linha: @BANCO:ENDEREÇO[espaço]|texto. Mude só o texto.");
        if (edit_text("##txt", texts, ImVec2(-1, 300))) save_texts();
    } else if (!rows.empty()) {
        ImGui::SetNextItemWidth(240);
        ImGui::InputTextWithHint("##filtro", "Procurar nos textos...", filter, sizeof filter);
        ImGui::SameLine(); ImGui::Checkbox("Só os alterados", &only_changed);
        std::string f = filter; for (auto& c : f) c = (char)tolower((unsigned char)c);
        std::vector<int> vis;
        for (int i = 0; i < (int)rows.size(); i++) {
            if (only_changed && rows[i].cur == rows[i].orig) continue;
            if (!f.empty()) {
                std::string h = rows[i].orig + " " + rows[i].cur; for (auto& c : h) c = (char)tolower((unsigned char)c);
                if (h.find(f) == std::string::npos) continue;
            }
            vis.push_back(i);
        }
        int changed = 0; for (auto& r : rows) changed += r.cur != r.orig;
        ImGui::SameLine(); ImGui::TextDisabled("%zu textos, %d alterados", rows.size(), changed);
        if (ImGui::BeginTable("textos", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable, ImVec2(0, 340))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Onde", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Original", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Novo texto", ImGuiTableColumnFlags_WidthStretch, 1.3f);
            ImGui::TableSetupColumn("Espaço", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000 de 000 (muda de lugar)").x + 8);
            ImGui::TableHeadersRow();
            ImGuiListClipper clip;
            clip.Begin((int)vis.size());
            bool edited_any = false;
            while (clip.Step())
                for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
                    TextRow& r = rows[vis[k]];
                    ImGui::PushID(vis[k]);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%s", r.prefix.substr(1, 7).c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.orig.c_str());
                    ImGui::TableNextColumn();
                    char buf[512]; snprintf(buf, sizeof buf, "%s", r.cur.c_str());
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::InputText("##t", buf, sizeof buf)) { r.cur = buf; edited_any = true; }
                    ImGui::TableNextColumn();
                    std::vector<u8> enc; std::string err;
                    if (!table.encode(r.cur, enc, err)) { ImGui::TextColored(X_ERR, "letra sem tabela"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", err.c_str()); }
                    else if (enc.size() <= r.cap) ImGui::TextColored(r.cur == r.orig ? X_DIM : X_OK, "%zu de %zu", enc.size(), r.cap);
                    else {
                        if (r.ptrs < 0) r.ptrs = (int)find_pointers(orig, r.off).size();
                        const bool movable = relocate && !table.ends.empty() && r.ptrs >= 1 && r.ptrs <= 4;
                        ImGui::TextColored(movable ? X_WARN : X_ERR, "%zu de %zu %s", enc.size(), r.cap, movable ? "(muda de lugar)" : "(não cabe)");
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(movable ? "Maior que o original: vai para um espaço livre do mesmo banco e o ponteiro do jogo é atualizado.\nTeste no jogo depois de aplicar."
                                                      : r.ptrs == 0 ? "Maior que o original e o Studio não achou o ponteiro para este texto. Encurte o texto."
                                                      : table.ends.empty() ? "Para mover textos maiores, a tabela precisa marcar o fim de texto (linha com /)."
                                                      : "Maior que o original. Encurte o texto ou ligue \"Mover textos maiores\".");
                    }
                    ImGui::PopID();
                }
            ImGui::EndTable();
            if (edited_any) { texts.set(rows_to_file(rows)); save_texts(); }
        }
    } else ImGui::TextDisabled("Crie a tabela e use \"Extrair textos da ROM\".");

    ImGui::SeparatorText("3. Aplicar");
    ImGui::Checkbox("Mover textos maiores para espaço livre (atualiza os ponteiros do jogo)", &relocate);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Recurso experimental: o Studio procura onde o jogo guarda o endereço do texto e o aponta para o novo lugar.\nTeste as telas com esses textos depois de aplicar.");
    ImGui::Checkbox("Também gravar direto na ROM do projeto (exige converter de novo)", &to_rom);
    if (ImGui::Button("Aplicar tradução", ImVec2(220, 0))) {
        TextTable t;
        if (!t.load_text(tbl.str())) msg.error("Tradução", "Crie a tabela primeiro.");
        else {
            if (!raw_view) texts.set(rows_to_file(rows));
            save_texts();
            std::vector<u8> mod = orig;
            report.clear(); notes.clear();
            inserted = insert_texts(mod, t, texts.str(), report, relocate, &notes);
            // a mod package (IPS) that the exported game can turn on and off...
            std::string md = p.path("mods/pacotes/traducao");
            plat::make_dirs(md);
            auto ips = make_ips(orig, mod);
            plat::write_file(plat::join(md, "patch.ips"), ips.data(), ips.size());
            plat::write_text(plat::join(md, "mod.ini"), "nome=Tradução\ndescricao=Textos traduzidos no NativeBoy Studio\n");
            // ...and, if asked, the project's own ROM
            if (to_rom) {
                std::vector<u8> w = p.rom;
                for (size_t i = 0; i < w.size() && i < mod.size(); i++) if (mod[i] != orig[i]) w[i] = mod[i];
                p.write_working_rom(w, "tradução");
            }
            msg.notify(std::to_string(inserted) + " textos aplicados" + (report.empty() ? "" : " (" + std::to_string(report.size()) + " com problema)"));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Testar no jogo")) msg.test_play();
    if (inserted >= 0) {
        ImGui::TextColored(report.empty() ? X_OK : X_WARN, "%d textos aplicados. Pacote de mod: mods/pacotes/traducao (vai junto no jogo exportado; ligue em Menu > Mods).", inserted);
        for (auto& r : notes) ImGui::TextColored(X_WARN, "%s", r.c_str());
        for (auto& r : report) ImGui::TextColored(X_ERR, "%s", r.c_str());
    }
}

// ================================================================== soundtrack
void draw_music_tab(Project& p, const StudioMsg& msg) {
    static std::string proj_dir;
    static MusicPack pack;
    static char addr[8] = "", idbuf[4] = "01";
    const std::string dir = p.path("mods/musica");
    if (proj_dir != p.dir) {
        proj_dir = p.dir; std::string e;
        pack = MusicPack();
        if (!pack.load(dir, e)) { pack.addr = -1; pack.files.clear(); }
        if (pack.addr >= 0) snprintf(addr, sizeof addr, "%04X", pack.addr); else addr[0] = 0;
    }
    auto save = [&]() { plat::make_dirs(dir); pack.addr = addr[0] ? (int)strtol(addr, nullptr, 16) : -1; pack.save(dir); };
    ImGui::TextWrapped("Troque as músicas do jogo por gravações novas (OGG). O jogo guarda o número da música que está tocando "
                       "em um endereço da memória; quando esse número muda, o NativeBoy toca o arquivo correspondente e silencia "
                       "os canais de música originais. Os efeitos sonoros que usam outros canais continuam.");
    ImGui::SeparatorText("1. Endereço da música atual");
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputTextWithHint("Endereço (hex)", "C0EF", addr, sizeof addr, ImGuiInputTextFlags_CharsHexadecimal)) save();
    ImGui::TextDisabled("Como achar: Jogar ROM > Menu > Trapaças. Comece uma busca, troque de música no jogo e filtre por \"Mudou\";\n"
                        "volte a uma música já ouvida e filtre por \"Valor atual é este\" com o número anotado. Sobra o endereço.");
    ImGui::SeparatorText("2. Canais silenciados durante a música nova");
    for (int i = 0; i < 4; i++) {
        bool on = pack.mute_mask >> i & 1;
        static const char* nm[4] = {"Pulso 1", "Pulso 2", "Onda", "Ruído"};
        if (i) ImGui::SameLine();
        if (ImGui::Checkbox(nm[i], &on)) { pack.mute_mask = (u8)(on ? pack.mute_mask | (1 << i) : pack.mute_mask & ~(1 << i)); save(); }
    }
    int vol = (int)(pack.volume * 100 + 0.5f);
    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderInt("Volume da trilha nova", &vol, 10, 200, "%d%%")) pack.volume = vol / 100.f;
    if (ImGui::IsItemDeactivatedAfterEdit()) save();
    ImGui::SeparatorText("3. Músicas");
    if (ImGui::BeginTable("mus", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Número", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Arquivo");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        int del = -1;
        for (auto& kv : pack.files) {
            ImGui::PushID(kv.first);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%02X (%d)", kv.first, kv.first);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(kv.second.c_str());
            if (!plat::file_exists(plat::join(dir, kv.second))) { ImGui::SameLine(); ImGui::TextColored(X_ERR, "(arquivo faltando)"); }
            ImGui::TableNextColumn(); if (ImGui::SmallButton("Remover")) del = kv.first;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (del >= 0) { pack.files.erase(del); save(); }
    }
    ImGui::SetNextItemWidth(60);
    ImGui::InputText("Número (hex)", idbuf, sizeof idbuf, ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    if (ImGui::Button("Escolher arquivo OGG...")) {
        std::string f = plat::open_file_dialog("Música (OGG Vorbis)", "OGG Vorbis", "*.ogg");
        if (!f.empty()) {
            std::vector<float> pcm; int rate; std::string err;
            if (!decode_ogg(f, pcm, rate, err)) msg.error("Música", err + "\nConverta para OGG Vorbis (ex.: no Audacity: Exportar > OGG).");
            else {
                plat::make_dirs(dir);
                std::string name = plat::file_name(f);
                plat::copy_file(f, plat::join(dir, name));
                pack.files[(int)strtol(idbuf, nullptr, 16) & 0xFF] = name;
                save();
                msg.notify("Música " + std::string(idbuf) + " = " + name + " (" + std::to_string((int)(pcm.size() / 2 / std::max(1, rate))) + " s)");
            }
        }
    }
    ImGui::Spacing();
    if (ImGui::Button("Testar no jogo")) { save(); msg.test_play(); }
    ImGui::SameLine();
    ImGui::TextDisabled("Para gravar a música original: Jogar ROM > Menu > Áudio > Gravar áudio em WAV.");
    ImGui::TextDisabled("A trilha vai em assets/musica do jogo exportado; o jogador pode desligá-la no menu Áudio.");
}

// ================================================================== icon and opening screen
static SDL_Texture* preview_tex(SDL_Renderer* r, const std::string& path, std::string& cached, SDL_Texture*& tex, int& w, int& h) {
    if (cached == path + std::to_string(plat::file_mtime(path))) return tex;
    if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
    cached = path + std::to_string(plat::file_mtime(path));
    std::vector<uint32_t> px;
    if (r && load_png(path, px, w, h)) {
        tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (tex) SDL_UpdateTexture(tex, nullptr, px.data(), w * 4);
    }
    return tex;
}

void draw_icon_tab(Project& p, const StudioMsg& msg) {
    static std::string c1, c2; static SDL_Texture *t1 = nullptr, *t2 = nullptr; static int w1, h1, w2, h2;
    SDL_Renderer* r = msg.ren;
    ImGui::SeparatorText("Ícone do jogo");
    ImGui::TextWrapped("Aparece no .exe do Windows, na barra de tarefas, na janela e no menu de aplicativos do Linux. "
                       "Sem um ícone escolhido, o Studio cria um a partir de uma tela do próprio jogo.");
    std::string ic = p.path("mods/icone.png");
    if (plat::file_exists(ic)) {
        if (SDL_Texture* t = preview_tex(r, ic, c1, t1, w1, h1)) ImGui::Image((ImTextureID)(intptr_t)t, ImVec2(96, 96));
    } else if (plat::file_exists(p.path("build/icone.png"))) {
        if (SDL_Texture* t = preview_tex(r, p.path("build/icone.png"), c1, t1, w1, h1)) { ImGui::Image((ImTextureID)(intptr_t)t, ImVec2(96, 96)); ImGui::SameLine(); ImGui::TextDisabled("(gerado automaticamente)"); }
    }
    if (ImGui::Button("Escolher ícone (PNG quadrado, 256x256 ideal)...")) {
        std::string f = plat::open_file_dialog("Ícone do jogo", "Imagem PNG", "*.png");
        std::vector<uint32_t> px; int w, h;
        if (!f.empty() && load_png(f, px, w, h)) { plat::make_dirs(p.path("mods")); plat::copy_file(f, ic); msg.notify("Ícone definido. Converta (ou exporte) de novo para aplicar."); }
        else if (!f.empty()) msg.error("Ícone", "Não foi possível ler a imagem. Use PNG.");
    }
    if (plat::file_exists(ic)) { ImGui::SameLine(); if (ImGui::Button("Usar ícone automático")) plat::remove_file(ic); }

    ImGui::SeparatorText("Tela de abertura");
    ImGui::TextWrapped("Uma imagem mostrada por 2,5 segundos quando o jogo abre (logo, capa, créditos). Qualquer tecla pula. O jogador pode desligar no menu.");
    std::string sp = p.path("mods/abertura.png");
    if (plat::file_exists(sp)) {
        if (SDL_Texture* t = preview_tex(r, sp, c2, t2, w2, h2)) {
            float s = std::min(320.f / w2, 180.f / h2);
            ImGui::Image((ImTextureID)(intptr_t)t, ImVec2(w2 * s, h2 * s));
        }
    }
    if (ImGui::Button("Escolher imagem de abertura (PNG)...")) {
        std::string f = plat::open_file_dialog("Tela de abertura", "Imagem PNG", "*.png");
        std::vector<uint32_t> px; int w, h;
        if (!f.empty() && load_png(f, px, w, h)) { plat::make_dirs(p.path("mods")); plat::copy_file(f, sp); msg.notify("Abertura definida. Exporte de novo para aplicar."); }
        else if (!f.empty()) msg.error("Abertura", "Não foi possível ler a imagem. Use PNG.");
    }
    if (plat::file_exists(sp)) { ImGui::SameLine(); if (ImGui::Button("Remover abertura")) plat::remove_file(sp); }
}

// ================================================================== achievements
void draw_achievements_editor(Project& p, const StudioMsg& msg) {
    struct Row { std::string id, name, desc, addr, op; int val; };
    static std::string proj_dir;
    static std::vector<Row> rows;
    static char nm[64], ds[128], ad[8];
    static int op = 3, val = 1;
    static const char* ops[] = {"=", "!=", ">", ">=", "<", "<="};
    static const char* op_names[] = {"igual a", "diferente de", "maior que", "maior ou igual a", "menor que", "menor ou igual a"};
    const std::string file = p.path("mods/conquistas.txt");
    auto save = [&]() {
        std::string t = "# Conquistas: id|Nome|Descrição|ENDEREÇO(hex)|operador|valor\n";
        for (auto& r : rows) t += r.id + "|" + r.name + "|" + r.desc + "|" + r.addr + "|" + r.op + "|" + std::to_string(r.val) + "\n";
        plat::make_dirs(p.path("mods")); plat::write_text(file, t);
    };
    if (proj_dir != p.dir) {
        proj_dir = p.dir; rows.clear();
        std::string t; plat::read_text(file, t);
        for (auto& line : split_lines(t)) {
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> f; size_t a = 0;
            for (;;) { size_t q = line.find('|', a); f.push_back(line.substr(a, q == std::string::npos ? std::string::npos : q - a)); if (q == std::string::npos) break; a = q + 1; }
            if (f.size() >= 6) rows.push_back({f[0], f[1], f[2], f[3], f[4], atoi(f[5].c_str())});
        }
    }
    ImGui::TextWrapped("Conquistas aparecem como no Steam ou no Xbox: um aviso no canto da tela quando o jogador consegue, e uma lista no menu. "
                       "Cada conquista olha um endereço da memória do jogo (dinheiro, fase, chefes derrotados...). "
                       "Para achar o endereço, use Jogar ROM > Menu > Trapaças > busca.");
    if (ImGui::BeginTable("ach", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Nome"); ImGui::TableSetupColumn("Descrição"); ImGui::TableSetupColumn("Condição"); ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        int del = -1;
        for (size_t i = 0; i < rows.size(); i++) {
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(rows[i].name.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(rows[i].desc.c_str());
            ImGui::TableNextColumn(); ImGui::Text("[%s] %s %d", rows[i].addr.c_str(), rows[i].op.c_str(), rows[i].val);
            ImGui::TableNextColumn(); if (ImGui::SmallButton("Remover")) del = (int)i;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (del >= 0) { rows.erase(rows.begin() + del); save(); }
    }
    ImGui::SeparatorText("Nova conquista");
    ImGui::SetNextItemWidth(260); ImGui::InputTextWithHint("Nome", "Milionário", nm, sizeof nm);
    ImGui::SetNextItemWidth(420); ImGui::InputTextWithHint("Descrição", "Junte 999 moedas", ds, sizeof ds);
    ImGui::SetNextItemWidth(80); ImGui::InputTextWithHint("Endereço (hex)", "C0A2", ad, sizeof ad, ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine(); ImGui::SetNextItemWidth(170); ImGui::Combo("##op", &op, op_names, 6);
    ImGui::SameLine(); ImGui::SetNextItemWidth(110); ImGui::InputInt("Valor", &val);
    val = std::max(0, std::min(255, val));
    if (ImGui::Button("Adicionar conquista")) {
        if (!nm[0] || !ad[0]) msg.error("Conquista", "Preencha pelo menos o nome e o endereço.");
        else {
            std::string id = "c" + plat::now_stamp();
            std::string n = nm, d = ds;
            for (auto* str : {&n, &d}) for (auto& c : *str) if (c == '|') c = '/';
            rows.push_back({id, n, d, ad, ops[op], val});
            save(); nm[0] = ds[0] = ad[0] = 0;
            msg.notify("Conquista adicionada");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Testar no jogo")) msg.test_play();
    ImGui::TextDisabled("A conquista só desbloqueia depois que a condição foi falsa ao menos uma vez (evita desbloquear ao ligar).\nCom trapaças ativas as conquistas não contam.");
}

// ================================================================== debugger
void draw_debugger(GamePlayer& pl, bool* open) {
    static std::vector<u8> bp(65536, 0);
    static char bpbuf[8] = "", memaddr[8] = "C000";
    static SDL_Texture* vtex = nullptr;
    Machine& m = pl.m;
    ImGui::SetNextWindowSize(ImVec2(560, 640), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Size.x - 580, 90), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Depurador", open)) { ImGui::End(); return; }
    bool any_bp = std::find(bp.begin(), bp.end(), 1) != bp.end();
    m.dbg_bp = any_bp ? &bp : nullptr;
    bool stopped = pl.paused || m.dbg_hit;
    if (ImGui::Button(stopped ? "Continuar (F9)" : "Pausar (F9)") || ImGui::IsKeyPressed(ImGuiKey_F9, false)) {
        if (stopped) { pl.paused = false; m.dbg_hit = false; m.dbg_skip = true; } else pl.paused = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!stopped);
    if (ImGui::Button("Passo: 1 instrução (F10)") || (stopped && ImGui::IsKeyPressed(ImGuiKey_F10))) { m.step_one(); pl.paused = true; pl.frame_ready = true; }
    ImGui::SameLine();
    if (ImGui::Button("Passo: 1 quadro")) { m.dbg_hit = false; m.dbg_skip = true; m.run_frame(nullptr); m.apu->out.clear(); pl.paused = true; pl.frame_ready = true; }
    ImGui::EndDisabled();
    if (m.dbg_hit) { ImGui::SameLine(); ImGui::TextColored(X_WARN, "parado no ponto de parada"); }

    ImGui::SeparatorText("Processador");
    ImGui::Text("AF %02X%02X  BC %02X%02X  DE %02X%02X  HL %02X%02X  SP %04X  PC %04X", m.a, m.f, m.b, m.c, m.d, m.e, m.h, m.l, m.sp, m.pc);
    ImGui::Text("Flags %c%c%c%c  IME %d  IE %02X  IF %02X  %s  banco ROM %d  LY %d  quadro %llu",
                m.f & 0x80 ? 'Z' : '-', m.f & 0x40 ? 'N' : '-', m.f & 0x20 ? 'H' : '-', m.f & 0x10 ? 'C' : '-',
                m.ime, m.ie, m.iflag, m.halted ? "HALT" : "", m.cart.rom_bank, (int)m.ly, (unsigned long long)m.frames);
    ImGui::SeparatorText("Código a partir do PC");
    u16 a = m.pc;
    for (int i = 0; i < 10; i++) {
        u8 b[3] = {rd(m, a), rd(m, (u16)(a + 1)), rd(m, (u16)(a + 2))};
        std::string t = nb::format_instr(b, a, [](u16) { return std::string(); });
        int len = b[0] == 0xCB ? 2 : std::max(1, (int)op_info(b[0]).len);
        ImGui::PushID(i);
        bool is_bp = bp[a];
        if (ImGui::SmallButton(is_bp ? "●" : "○")) bp[a] = !bp[a];
        ImGui::SameLine();
        ImGui::TextColored(i == 0 ? X_OK : ImVec4(0.9f, 0.9f, 0.9f, 1), "%04X  %-12s %s", a, [&] { static char hx[16]; hx[0] = 0; for (int k = 0; k < len; k++) snprintf(hx + k * 3, 4, "%02X ", b[k]); return hx; }(), t.c_str());
        ImGui::PopID();
        a = (u16)(a + len);
    }
    ImGui::SeparatorText("Pontos de parada");
    ImGui::SetNextItemWidth(80);
    ImGui::InputTextWithHint("##bp", "0150", bpbuf, sizeof bpbuf, ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    if (ImGui::Button("Adicionar") && bpbuf[0]) { bp[strtol(bpbuf, nullptr, 16) & 0xFFFF] = 1; bpbuf[0] = 0; }
    ImGui::SameLine(); if (ImGui::Button("Limpar todos")) std::fill(bp.begin(), bp.end(), 0);
    for (int i = 0; i < 65536; i++)
        if (bp[i]) { ImGui::PushID(i); ImGui::Text("%04X", i); ImGui::SameLine(); if (ImGui::SmallButton("remover")) bp[i] = 0; ImGui::SameLine(); ImGui::PopID(); }
    ImGui::NewLine();
    ImGui::TextDisabled("O jogo para antes de executar o endereço (no banco de ROM que estiver ativo).");

    if (ImGui::CollapsingHeader("Memória")) {
        ImGui::SetNextItemWidth(80);
        ImGui::InputText("Endereço", memaddr, sizeof memaddr, ImGuiInputTextFlags_CharsHexadecimal);
        u16 base = (u16)(strtol(memaddr, nullptr, 16) & 0xFFF0);
        for (int row = 0; row < 16; row++) {
            char line[128]; int k = snprintf(line, sizeof line, "%04X ", (u16)(base + row * 16));
            char asc[17] = {0};
            for (int c = 0; c < 16; c++) {
                u8 v = rd(m, (u16)(base + row * 16 + c));
                k += snprintf(line + k, sizeof line - k, " %02X", v);
                asc[c] = v >= 32 && v < 127 ? (char)v : '.';
            }
            ImGui::Text("%s  %s", line, asc);
        }
    }
    if (ImGui::CollapsingHeader("Tiles na VRAM")) {
        SDL_Renderer* r = pl.ren;
        int banks = m.cgb ? 2 : 1;
        const int W = 128 * banks, H = 192;
        if (r && !vtex) vtex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256, H);
        if (vtex) {
            static std::vector<uint32_t> px(256 * 192);
            static const uint32_t shade[4] = {0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000};
            std::fill(px.begin(), px.end(), 0xFF202020);
            for (int bk = 0; bk < banks; bk++)
                for (int t = 0; t < 384; t++)
                    for (int y = 0; y < 8; y++) {
                        u8 lo = m.vram[bk][t * 16 + y * 2], hi = m.vram[bk][t * 16 + y * 2 + 1];
                        for (int x = 0; x < 8; x++) {
                            int c = ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1);
                            px[(size_t)((t / 16) * 8 + y) * 256 + bk * 128 + (t % 16) * 8 + x] = shade[c];
                        }
                    }
            SDL_UpdateTexture(vtex, nullptr, px.data(), 256 * 4);
            ImGui::Image((ImTextureID)(intptr_t)vtex, ImVec2((float)W * 2, (float)H * 2), ImVec2(0, 0), ImVec2(W / 256.f, 1));
        }
    }
    if (ImGui::CollapsingHeader("Sprites (OAM)")) {
        if (ImGui::BeginTable("oam", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 200))) {
            ImGui::TableSetupColumn("#"); ImGui::TableSetupColumn("X"); ImGui::TableSetupColumn("Y"); ImGui::TableSetupColumn("Tile"); ImGui::TableSetupColumn("Atributos");
            ImGui::TableHeadersRow();
            for (int i = 0; i < 40; i++) {
                const u8* o = &m.oam[i * 4];
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%d", i);
                ImGui::TableNextColumn(); ImGui::Text("%d", o[1] - 8);
                ImGui::TableNextColumn(); ImGui::Text("%d", o[0] - 16);
                ImGui::TableNextColumn(); ImGui::Text("%02X", o[2]);
                ImGui::TableNextColumn(); ImGui::Text("%02X%s%s%s", o[3], o[3] & 0x80 ? " atrás" : "", o[3] & 0x40 ? " V" : "", o[3] & 0x20 ? " H" : "");
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
    if (!*open) { m.dbg_bp = nullptr; m.dbg_hit = false; }
}

// ================================================================== game library
namespace {
struct GameEntry {
    std::string dir, name, exe, platform, cover, official;
    std::string genre, year, publisher, developer, players, series, custom_cover;
    bool gbc = false;
    double play_seconds = 0;
    int ach_done = 0, ach_total = 0;
    int64_t last_played = 0, exported = 0;
    bool runnable = true;
};

std::vector<GameEntry> scan_games(const std::string& root) {
    std::vector<GameEntry> out;
    for (auto& d : plat::list_dir(root, true)) {
        if (d == "Executável único" || d == "Instaladores") continue;       // single-file copies of the games listed here
        GameEntry g;
        g.dir = plat::join(root, d);
        Settings gi; gi.load(plat::join(g.dir, "config/jogo.ini"));
        g.name = gi.get("nome", d);
        g.official = gi.get("nome_oficial", "");
        g.genre = gi.get("genero", ""); g.year = gi.get("ano", ""); g.publisher = gi.get("editora", "");
        g.developer = gi.get("desenvolvedora", ""); g.players = gi.get("jogadores", ""); g.series = gi.get("serie", "");
        g.gbc = gi.getb("gbc", false);
        g.exe = gi.get("executavel", "");
        g.platform = gi.get("plataforma", "");
        if (g.exe.empty()) {      // exported by an older Studio: look for the program
            for (auto& f : plat::list_dir(g.dir, false)) {
                std::string l = f; for (auto& c : l) c = (char)tolower((unsigned char)c);
                if (l.size() > 4 && l.compare(l.size() - 4, 4, ".exe") == 0) { g.exe = f; g.platform = "windows"; break; }
                if (plat::file_exists(plat::join(g.dir, f + ".desktop"))) { g.exe = f; g.platform = "linux"; }
            }
        }
        if (g.exe.empty() || !plat::file_exists(plat::join(g.dir, g.exe))) continue;
#ifdef _WIN32
        g.runnable = g.platform != "linux";
#else
        g.runnable = g.platform != "windows";
#endif
        for (const char* c : {"assets/capa.png", "assets/abertura.png", "assets/icone.png"})
            if (plat::file_exists(plat::join(g.dir, c))) { g.cover = plat::join(g.dir, c); break; }
        if (plat::file_exists(plat::join(g.dir, "assets/capa.png"))) g.custom_cover = plat::join(g.dir, "assets/capa.png");
        g.exported = plat::file_time_unix(plat::join(g.dir, "config/jogo.ini"));
        std::string sv = plat::join(g.dir, "saves");
        for (auto& f : plat::list_dir(sv, false)) {
            std::string full = plat::join(sv, f), t;
            g.last_played = std::max<int64_t>(g.last_played, plat::file_time_unix(full));
            if (f.size() > 6 && f.compare(f.size() - 6, 6, ".tempo") == 0 && plat::read_text(full, t)) g.play_seconds += atof(t.c_str());
            if (f.size() > 11 && f.compare(f.size() - 11, 11, ".conquistas") == 0 && plat::read_text(full, t))
                for (auto& line : split_lines(t)) if (!line.empty()) g.ach_done++;
        }
        std::string at;
        if (plat::read_text(plat::join(g.dir, "assets/conquistas.txt"), at))
            for (auto& line : split_lines(at)) if (!line.empty() && line[0] != '#') g.ach_total++;
        out.push_back(g);
    }
    std::sort(out.begin(), out.end(), [](const GameEntry& a, const GameEntry& b) {
        int64_t ta = std::max(a.last_played, a.exported), tb = std::max(b.last_played, b.exported);
        if (ta != tb) return ta > tb;
        return a.name < b.name;
    });
    return out;
}

std::string safe_file_name(std::string s) {
    for (auto& c : s) if (strchr("\\/:*?\"<>|", c)) c = '_';
    return s;
}
} // namespace

bool make_game_shortcuts(const std::string& game_dir, bool desktop, bool start_menu, std::string& err) {
    Settings gi; gi.load(plat::join(game_dir, "config/jogo.ini"));
    std::string name = gi.get("nome", plat::file_name(game_dir)), exe = gi.get("executavel", ""), platform = gi.get("plataforma", "");
    if (exe.empty() || !plat::file_exists(plat::join(game_dir, exe))) { err = "O executável do jogo não foi encontrado em " + game_dir; return false; }
#ifdef _WIN32
    if (platform == "linux") { err = "Este é o jogo para Linux."; return false; }
    std::string icon = plat::join(game_dir, exe);                       // the .exe carries the game's icon
#else
    if (platform == "windows") { err = "Esta é a versão para Windows: copie a pasta para um PC com Windows."; return false; }
    std::string icon = plat::file_exists(plat::join(game_dir, "assets/icone.png")) ? plat::join(game_dir, "assets/icone.png") : "";
#endif
    std::string target = plat::join(game_dir, exe), comment = "Jogo nativo gerado pelo NativeBoy Studio";
    bool ok = true;
    if (desktop) {
        std::string d = plat::desktop_dir();
        ok &= !d.empty() && plat::create_shortcut(plat::join(d, safe_file_name(name)), name, target, game_dir, icon, comment);
    }
    if (start_menu) {
        std::string m = plat::start_menu_dir();
#ifdef _WIN32
        ok &= !m.empty() && plat::create_shortcut(plat::join(plat::join(m, "Jogos NativeBoy"), safe_file_name(name)), name, target, game_dir, icon, comment);
#else
        ok &= !m.empty() && plat::create_shortcut(plat::join(m, "nativeboy-" + plat::stem(exe)), name, target, game_dir, icon, comment);
#endif
    }
    if (!ok) err = "Não foi possível criar o atalho (pasta sem permissão de escrita?).";
    return ok;
}

namespace {
// ---- library drawing helpers
ImU32 lib_col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
ImU32 lib_panel() { return ui::theme_retro ? lib_col(46, 46, 112, 240) : ui::theme_gbc ? lib_col(52, 38, 96, 235) : lib_col(32, 35, 42, 255); }
ImU32 lib_dark() { return ui::theme_retro ? lib_col(22, 22, 58) : ui::theme_gbc ? lib_col(28, 20, 54) : lib_col(20, 22, 28); }
std::string play_time_text(double secs) {
    int s = (int)secs;
    if (s < 60) return "Ainda não jogado";
    return s >= 3600 ? std::to_string(s / 3600) + " h " + std::to_string((s / 60) % 60) + " min" : std::to_string(s / 60) + " min jogados";
}
std::string players_text(const std::string& p) {
    if (p.empty()) return "";
    return p == "1" ? "1 jogador" : "1 a " + p + " jogadores";
}
struct CoverTex { SDL_Texture* t = nullptr; int w = 0, h = 0; ImU32 tint = 0; };
// average colour of the image, used for the cover's background and glow
ImU32 average_color(const std::vector<uint32_t>& px) {
    uint64_t r = 0, g = 0, b = 0, n = 0;
    for (uint32_t c : px) { if ((c >> 24) < 128) continue; r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++; }
    if (!n) return lib_col(90, 90, 160);
    return lib_col((int)(r / n), (int)(g / n), (int)(b / n));
}
ImU32 mix(ImU32 a, ImU32 b, float t) {
    auto ch = [&](int sh) { return (int)(((a >> sh) & 255) * (1 - t) + ((b >> sh) & 255) * t); };
    return IM_COL32(ch(0), ch(8), ch(16), 255);
}
// draws an image over a rectangle: big images fill (cropped), small icons stay sharp at an integer scale
void draw_cover_image(ImDrawList* dl, const CoverTex& c, ImVec2 a, ImVec2 b, float round, ImDrawFlags fl) {
    float w = b.x - a.x, h = b.y - a.y;
    if (!c.t || c.w <= 0) return;
    if (c.w < 200) {
        dl->AddRectFilledMultiColor(a, b, mix(c.tint, lib_dark(), 0.35f), mix(c.tint, lib_dark(), 0.35f), mix(c.tint, lib_dark(), 0.8f), mix(c.tint, lib_dark(), 0.8f));
        int k = std::max(1, (int)std::min((h - 20) / c.h, (w - 20) / c.w));
        float x0 = a.x + (w - c.w * k) / 2, y0 = a.y + (h - c.h * k) / 2;
        dl->AddImage(ui::tex_ref(c.t), ImVec2(x0, y0), ImVec2(x0 + c.w * k, y0 + c.h * k));
        return;
    }
    float s = std::max(w / c.w, h / (float)c.h);
    float vw = (w / s) / c.w, vh = (h / s) / c.h;
    ImVec2 uv0((1 - vw) / 2, (1 - vh) / 2), uv1(uv0.x + vw, uv0.y + vh);
    dl->AddImageRounded(ui::tex_ref(c.t), a, b, uv0, uv1, IM_COL32_WHITE, round, fl);
}
void chip(ImDrawList* dl, ImVec2& at, const std::string& text, ImU32 col, float max_x) {
    if (text.empty()) return;
    ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    if (at.x + ts.x + 16 > max_x) return;
    dl->AddRectFilled(at, ImVec2(at.x + ts.x + 16, at.y + ts.y + 6), (col & 0x00FFFFFF) | 0x50000000, 20);
    dl->AddRect(at, ImVec2(at.x + ts.x + 16, at.y + ts.y + 6), (col & 0x00FFFFFF) | 0xC0000000, 20);
    dl->AddText(ImVec2(at.x + 8, at.y + 3), IM_COL32(245, 245, 250, 255), text.c_str());
    at.x += ts.x + 22;
}
// text cut to fit a width, with "..."
std::string fit_text(const std::string& s, float w) {
    if (ImGui::CalcTextSize(s.c_str()).x <= w) return s;
    std::string t = s;
    while (!t.empty() && ImGui::CalcTextSize((t + "...").c_str()).x > w) {
        t.pop_back();
        while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80) t.pop_back();     // whole UTF-8 characters
        if (!t.empty() && ((unsigned char)t.back() & 0x80)) t.pop_back();
    }
    return t + "...";
}
} // namespace

void draw_library(const std::string& root, const StudioMsg& msg) {
    static std::vector<GameEntry> games;
    static Uint32 last_scan = 0;
    static std::string last_root, featured;
    static std::map<std::string, CoverTex> covers;
    static char filter[64] = "";
    static int genre_sel = 0, sys_sel = 0, sort_sel = 0;
    static float cover_w = 170;
    if (root != last_root || SDL_GetTicks() - last_scan > 3000) { games = scan_games(root); last_scan = SDL_GetTicks(); last_root = root; }
    auto cover_of = [&](const GameEntry& g) -> CoverTex {
        if (g.cover.empty() || !msg.ren) return CoverTex();
        auto it = covers.find(g.cover);
        if (it == covers.end()) {
            CoverTex c; std::vector<uint32_t> px;
            if (load_png(g.cover, px, c.w, c.h)) {
                c.t = SDL_CreateTexture(msg.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, c.w, c.h);
                if (c.t) { SDL_UpdateTexture(c.t, nullptr, px.data(), c.w * 4); SDL_SetTextureScaleMode(c.t, c.w < 200 ? SDL_ScaleModeNearest : SDL_ScaleModeLinear); }
                c.tint = average_color(px);
            }
            it = covers.emplace(g.cover, c).first;
        }
        return it->second;
    };
    auto forget_cover = [&](const std::string& path) {
        auto it = covers.find(path);
        if (it != covers.end()) { if (it->second.t) SDL_DestroyTexture(it->second.t); covers.erase(it); }
    };
    auto play = [&](const GameEntry& g) {
        if (!plat::launch(plat::join(g.dir, g.exe), "", g.dir)) msg.error("Jogar", "Não foi possível abrir " + g.exe);
        else msg.notify("Abrindo " + g.name);
    };

    if (games.empty()) {
        ui::push_size(1.9f); ImGui::TextUnformatted("Biblioteca"); ui::pop_size();
        ImGui::Spacing();
        ImGui::TextWrapped("Nenhum jogo convertido ainda. Abra uma ROM, use \"Converter para PC\" e o jogo aparece aqui com capa, pronto para jogar.");
        if (ImGui::Button("Abrir pasta Meus Jogos")) { plat::make_dirs(root); plat::open_in_explorer(root); }
        return;
    }
    const GameEntry* hero = &games[0];      // last played game (or the newest one), unless the player picked another
    for (auto& g : games) if (g.last_played > hero->last_played) hero = &g;
    for (auto& g : games) if (g.dir == featured) hero = &g;

    ImGui::BeginChild("lib", ImVec2(0, 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = ImGui::GetContentRegionAvail().x;
    // ---------------------------------------------------------------- featured game (big banner)
    {
        const GameEntry& g = *hero;
        CoverTex c = cover_of(g);
        const float H = std::clamp(W * 0.30f, 230.f, 330.f);
        ImVec2 p = ImGui::GetCursorScreenPos(), q(p.x + W, p.y + H);
        ImU32 tint = c.t ? c.tint : lib_col(90, 90, 170);
        dl->AddRectFilled(p, q, lib_dark(), 14);
        // the game's picture on the right, fading into the panel on the left
        float img_w = std::min(W * 0.55f, H * 1.6f);
        ImVec2 ia(q.x - img_w, p.y), ib = q;
        if (c.t) draw_cover_image(dl, c, ia, ib, 14, ImDrawFlags_RoundCornersRight);
        ImU32 dark = lib_dark(), clear = dark & 0x00FFFFFF;
        dl->AddRectFilledMultiColor(ImVec2(ia.x - 2, ia.y), ImVec2(ia.x + img_w * 0.55f, ib.y), dark, clear, clear, dark);
        dl->AddRectFilledMultiColor(p, ImVec2(p.x + W * 0.55f, q.y), (mix(tint, dark, 0.55f) & 0x00FFFFFF) | 0x90000000, clear, clear, (mix(tint, dark, 0.55f) & 0x00FFFFFF) | 0x90000000);
        dl->AddRect(p, q, (tint & 0x00FFFFFF) | 0x90000000, 14.f, 0, 2.f);
        // stripe of the five accent colours
        for (int i = 0; i < 5; i++) dl->AddRectFilled(ImVec2(p.x + 26 + i * 26, p.y + 22), ImVec2(p.x + 48 + i * 26, p.y + 26), ui::accent(i), 2);
        float x = p.x + 26, tw = W * 0.52f;
        dl->AddText(ImVec2(x, p.y + 36), lib_col(200, 200, 230), g.last_played ? "CONTINUAR JOGANDO" : "PRONTO PARA JOGAR");
        ui::push_size(2.0f);
        std::string nm = fit_text(g.name, tw);
        dl->AddText(ImVec2(x, p.y + 58), IM_COL32_WHITE, nm.c_str());
        float name_h = ImGui::GetTextLineHeight();
        ui::pop_size();
        float y = p.y + 64 + name_h;
        if (!g.official.empty() && g.official != g.name) { dl->AddText(ImVec2(x, y), lib_col(190, 190, 215), fit_text(g.official, tw).c_str()); y += ImGui::GetTextLineHeight() + 6; }
        ImVec2 at(x, y + 4);
        chip(dl, at, g.gbc ? "GB Color" : "GB", g.gbc ? ui::accent(1) : lib_col(150, 160, 150), x + tw);
        chip(dl, at, g.genre, ui::accent(2), x + tw);
        chip(dl, at, g.year, ui::accent(3), x + tw);
        chip(dl, at, players_text(g.players), ui::accent(4), x + tw);
        y = at.y + ImGui::GetTextLineHeight() + 16;
        std::string by;
        if (!g.publisher.empty()) by = g.publisher;
        if (!g.developer.empty() && g.developer != g.publisher) by += (by.empty() ? "" : "  ·  desenvolvido por ") + g.developer;
        if (!by.empty()) { dl->AddText(ImVec2(x, y), lib_col(205, 205, 225), fit_text(by, tw).c_str()); y += ImGui::GetTextLineHeight() + 4; }
        if (!g.series.empty()) { dl->AddText(ImVec2(x, y), lib_col(170, 170, 200), fit_text("Série: " + g.series, tw).c_str()); y += ImGui::GetTextLineHeight() + 4; }
        std::string stats = play_time_text(g.play_seconds);
        if (g.ach_total) stats += "   ·   " + std::to_string(g.ach_done) + " de " + std::to_string(g.ach_total) + " conquistas";
        dl->AddText(ImVec2(x, y), lib_col(170, 230, 170), stats.c_str());
        // buttons
        ImGui::SetCursorScreenPos(ImVec2(x, q.y - 54));
        ImGui::BeginDisabled(!g.runnable);
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(lib_col(70, 170, 110)));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(lib_col(90, 200, 130)));
        ui::push_size(1.25f);
        if (ImGui::Button(g.runnable ? "  JOGAR  " : (g.platform == "windows" ? " versão Windows " : " versão Linux "), ImVec2(0, 38))) play(g);
        ui::pop_size();
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Atalhos", ImVec2(0, 38))) {
            std::string err;
            if (make_game_shortcuts(g.dir, true, true, err)) msg.notify("Atalhos criados na Área de Trabalho e no menu");
            else msg.error("Atalhos", err);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Pasta", ImVec2(0, 38))) plat::open_in_explorer(g.dir);
        ImGui::SameLine();
        if (ImGui::Button("Trocar capa", ImVec2(0, 38))) {
            std::string f = plat::open_file_dialog("Escolha a imagem da capa", "Imagens", "*.png;*.jpg;*.jpeg;*.bmp");
            std::vector<uint32_t> px; int w = 0, h = 0;
            if (!f.empty()) {
                if (!load_png(f, px, w, h)) msg.error("Capa", "Não foi possível ler a imagem.");
                else if (save_png(plat::join(g.dir, "assets/capa.png"), px.data(), w, h)) { forget_cover(g.cover); last_scan = 0; msg.notify("Capa trocada"); }
                else msg.error("Capa", "Não foi possível gravar assets/capa.png.");
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Escolha uma imagem (PNG ou JPG) para ser a capa deste jogo.\nFica em assets/capa.png e vai junto se você copiar a pasta.");
        if (!g.custom_cover.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Capa original", ImVec2(0, 38))) { forget_cover(g.custom_cover); plat::remove_file(g.custom_cover); last_scan = 0; }
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, q.y + 16));
    }
    // ---------------------------------------------------------------- filters
    std::vector<std::string> genres{"Todos os gêneros"};
    for (auto& g : games) if (!g.genre.empty() && std::find(genres.begin(), genres.end(), g.genre) == genres.end()) genres.push_back(g.genre);
    std::sort(genres.begin() + 1, genres.end());
    if (genre_sel >= (int)genres.size()) genre_sel = 0;
    ui::push_size(1.35f); ImGui::TextUnformatted("Todos os jogos"); ui::pop_size();
    ImGui::SameLine(); ImGui::TextDisabled("  %zu jogo(s)", games.size());
    ImGui::SetNextItemWidth(250);
    ImGui::InputTextWithHint("##busca", "Procurar nome, editora...", filter, sizeof filter);
    ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::CalcTextSize("Esporte com animais").x + 50);
    if (ImGui::BeginCombo("##genero", genres[genre_sel].c_str())) {
        for (int i = 0; i < (int)genres.size(); i++) if (ImGui::Selectable(genres[i].c_str(), i == genre_sel)) genre_sel = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::CalcTextSize("GB e GB Color").x + 50);
    const char* systems[] = {"GB e GB Color", "Só GB", "Só GB Color"};
    ImGui::Combo("##sistema", &sys_sel, systems, 3);
    ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::CalcTextSize("Mais jogados").x + 50);
    const char* sorts[] = {"Recentes", "Nome", "Ano", "Mais jogados"};
    ImGui::Combo("##ordem", &sort_sel, sorts, 4);
    ImGui::SameLine(); ImGui::TextDisabled("Tamanho"); ImGui::SameLine(); ImGui::SetNextItemWidth(110);
    ImGui::SliderFloat("##tam", &cover_w, 130, 260, "");
    ImGui::SameLine();
    if (ImGui::Button("Atualizar")) last_scan = 0;
    ImGui::SameLine();
    if (ImGui::Button("Meus Jogos")) { plat::make_dirs(root); plat::open_in_explorer(root); }
    ImGui::Spacing();

    std::string f = filter; for (auto& c : f) c = (char)tolower((unsigned char)c);
    std::vector<const GameEntry*> shown;
    for (auto& g : games) {
        std::string hay = g.name + " " + g.official + " " + g.publisher + " " + g.developer + " " + g.series;
        for (auto& c : hay) c = (char)tolower((unsigned char)c);
        if (!f.empty() && hay.find(f) == std::string::npos) continue;
        if (genre_sel > 0 && g.genre != genres[genre_sel]) continue;
        if (sys_sel == 1 && g.gbc) continue;
        if (sys_sel == 2 && !g.gbc) continue;
        shown.push_back(&g);
    }
    if (sort_sel == 1) std::stable_sort(shown.begin(), shown.end(), [](auto a, auto b) { return a->name < b->name; });
    if (sort_sel == 2) std::stable_sort(shown.begin(), shown.end(), [](auto a, auto b) { return (a->year.empty() ? "9999" : a->year) < (b->year.empty() ? "9999" : b->year); });
    if (sort_sel == 3) std::stable_sort(shown.begin(), shown.end(), [](auto a, auto b) { return a->play_seconds > b->play_seconds; });
    if (shown.empty()) ImGui::TextDisabled("Nenhum jogo com esses filtros.");

    // ---------------------------------------------------------------- cover grid (box-art cards)
    const float cw = cover_w, art_h = cw * 1.12f, ch = art_h + 64, gap = 18;
    int per_row = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (cw + gap)));
    for (size_t i = 0; i < shown.size(); i++) {
        const GameEntry& g = *shown[i];
        ImGui::PushID(g.dir.c_str());
        if (i % per_row) ImGui::SameLine(0, gap);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("capa", ImVec2(cw, ch));
        bool hov = ImGui::IsItemHovered(), sel = &g == hero;
        if (ImGui::IsItemClicked()) featured = g.dir;
        if (hov && ImGui::IsMouseDoubleClicked(0) && g.runnable) play(g);
        if (hov) {
            std::string tip = g.official.empty() ? g.name : g.official;
            if (!g.publisher.empty()) tip += "\n" + g.publisher + (g.year.empty() ? "" : ", " + g.year);
            tip += "\nClique para destacar, dois cliques para jogar.";
            ImGui::SetTooltip("%s", tip.c_str());
        }
        CoverTex c = cover_of(g);
        ImU32 tint = c.t ? c.tint : lib_col(90, 90, 170);
        float lift = hov ? -4.f : 0.f;
        ImVec2 a(p.x, p.y + lift), b(p.x + cw, p.y + lift + ch);
        if (hov || sel) for (int k = 3; k >= 1; k--) dl->AddRect(ImVec2(a.x - k * 2, a.y - k * 2), ImVec2(b.x + k * 2, b.y + k * 2), (tint & 0x00FFFFFF) | ((ImU32)(28 * (4 - k) + (sel ? 40 : 0)) << 24), 12.f + k * 2, 0, 2.f);
        dl->AddRectFilled(ImVec2(a.x + 4, a.y + 6), ImVec2(b.x + 4, b.y + 6), IM_COL32(0, 0, 0, 70), 10);   // shadow
        dl->AddRectFilled(a, b, lib_panel(), 10);
        // system band at the top of the box
        const float band = 20;
        if (g.gbc) {
            float bw = cw / 5;
            for (int k = 0; k < 5; k++) dl->AddRectFilled(ImVec2(a.x + k * bw, a.y), ImVec2(a.x + (k + 1) * bw + (k == 4 ? 0 : 1), a.y + band), ui::accent(k), k == 0 ? 10 : k == 4 ? 10 : 0, k == 0 ? ImDrawFlags_RoundCornersTopLeft : k == 4 ? ImDrawFlags_RoundCornersTopRight : 0);
        } else dl->AddRectFilled(a, ImVec2(b.x, a.y + band), lib_col(150, 160, 150), 10, ImDrawFlags_RoundCornersTop);
        const char* sys = g.gbc ? "GB COLOR" : "GB";
        ImVec2 sz = ImGui::CalcTextSize(sys);
        dl->AddRectFilled(ImVec2(a.x + 5, a.y + 2), ImVec2(a.x + 15 + sz.x, a.y + band - 2), IM_COL32(15, 15, 30, 200), 8);
        dl->AddText(ImVec2(a.x + 10, a.y + (band - sz.y) / 2), IM_COL32(240, 240, 250, 255), sys);
        if (!g.runnable) { const char* w = g.platform == "windows" ? "Windows" : "Linux"; dl->AddText(ImVec2(b.x - ImGui::CalcTextSize(w).x - 8, a.y + 2), IM_COL32(60, 20, 20, 255), w); }
        // art
        ImVec2 ia(a.x + 6, a.y + band + 4), ib(b.x - 6, a.y + band + 4 + art_h - 10);
        if (c.t) draw_cover_image(dl, c, ia, ib, 6, ImDrawFlags_RoundCornersAll);
        else {
            dl->AddRectFilledMultiColor(ia, ib, mix(tint, lib_dark(), 0.3f), mix(tint, lib_dark(), 0.3f), lib_dark(), lib_dark());
            ui::push_size(2.2f);
            std::string ini = g.name.substr(0, 1);
            ImVec2 ts = ImGui::CalcTextSize(ini.c_str());
            dl->AddText(ImVec2((ia.x + ib.x - ts.x) / 2, (ia.y + ib.y - ts.y) / 2), IM_COL32(230, 230, 250, 200), ini.c_str());
            ui::pop_size();
        }
        dl->AddRect(ia, ib, IM_COL32(255, 255, 255, 40), 6);
        // glass sheen
        dl->AddRectFilledMultiColor(ia, ImVec2(ib.x, ia.y + (ib.y - ia.y) * 0.35f), IM_COL32(255, 255, 255, 26), IM_COL32(255, 255, 255, 26), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
        // title and details
        float ty = ib.y + 6;
        dl->AddText(ImVec2(a.x + 9, ty), IM_COL32(245, 245, 250, 255), fit_text(g.name, cw - 18).c_str());
        std::string sub = g.genre;
        if (!g.year.empty()) sub += (sub.empty() ? "" : " · ") + g.year;
        if (sub.empty()) sub = play_time_text(g.play_seconds);
        dl->AddText(ImVec2(a.x + 9, ty + ImGui::GetTextLineHeight() + 2), lib_col(165, 170, 200), fit_text(sub, cw - 18).c_str());
        if (g.play_seconds >= 60) {     // little bar: played time (up to 10 h)
            float t = (float)std::min(1.0, g.play_seconds / 36000.0);
            dl->AddRectFilled(ImVec2(a.x + 9, b.y - 9), ImVec2(b.x - 9, b.y - 6), IM_COL32(255, 255, 255, 30), 2);
            dl->AddRectFilled(ImVec2(a.x + 9, b.y - 9), ImVec2(a.x + 9 + (cw - 18) * t, b.y - 6), lib_col(110, 210, 140), 2);
        }
        if (hov && g.runnable) {     // play button on the art
            ImVec2 m((ia.x + ib.x) / 2, (ia.y + ib.y) / 2);
            dl->AddCircleFilled(m, 26, IM_COL32(20, 20, 40, 190));
            dl->AddCircle(m, 26, IM_COL32(255, 255, 255, 200), 0, 2);
            dl->AddTriangleFilled(ImVec2(m.x - 8, m.y - 12), ImVec2(m.x - 8, m.y + 12), ImVec2(m.x + 13, m.y), IM_COL32_WHITE);
        }
        ImGui::PopID();
        if (i % per_row == (size_t)per_row - 1 || i + 1 == shown.size()) ImGui::Dummy(ImVec2(0, 4));
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Dados dos jogos: libretro-database (CC BY-SA 4.0). Capas: imagem de abertura do jogo, ícone ou a imagem que você escolher.");
    ImGui::EndChild();
}

// ================================================================== sprite editor
namespace {
struct SprPiece { int x, y; int tile; bool fx, fy; int pal; bool obj; bool obp1; };   // 8x8 piece on the canvas (tile = bank*384+n)
struct SprEdit {
    std::unique_ptr<gb::Machine> m;
    std::string proj_dir;
    SDL_Texture* tex = nullptr;
    bool paused = false;
    std::vector<SprPiece> pieces; int cw = 0, ch = 0;     // selected character, canvas size in pixels
    std::map<int, std::array<u8, 16>> orig;                // tile -> graphics before editing (as the game drew them)
    std::map<int, uint64_t> hdkey;                         // tile -> HD pack key of the original look
    std::map<int, PixelInfo> hdpi;
    std::vector<std::map<int, std::array<u8, 16>>> undo, redo;
    int color = 3, tool = 0; float zoom = 0;
    bool stroke = false;
    std::string status;
    // edits stay on the character while the game runs: any tile the game (re)loads with the original
    // graphics of an edited tile gets the edited graphics again
    bool keep_live = true;
    bool edited = false;                                    // something was drawn this frame (pins are updated then only)
    std::map<std::array<u8, 16>, std::array<u8, 16>> pinned;
    SDL_Texture* sheet = nullptr;                           // every sprite tile loaded in video memory
    int sheet_bank = 0;
    int layout = 0;                                         // tile sheet click: 0 one piece, 1 8x16, 2 2x2 columns, 3 2x2 rows
};
SprEdit SE;
void se_mark_edited() { SE.edited = true; }

u8* se_tile(gb::Machine& m, int t) { return &m.vram[t >= 384 ? 1 : 0][(t % 384) * 16]; }
int se_get(gb::Machine& m, const SprPiece& p, int x, int y) {
    int tx = p.fx ? 7 - x : x, ty = p.fy ? 7 - y : y;
    const u8* d = se_tile(m, p.tile) + ty * 2;
    return (((d[1] >> (7 - tx)) & 1) << 1) | ((d[0] >> (7 - tx)) & 1);
}
void se_mark_edited();
void se_set(gb::Machine& m, const SprPiece& p, int x, int y, int ci) {
    se_mark_edited();
    int tx = p.fx ? 7 - x : x, ty = p.fy ? 7 - y : y;
    u8* d = se_tile(m, p.tile) + ty * 2; u8 bit = (u8)(1 << (7 - tx));
    d[0] = (ci & 1) ? (d[0] | bit) : (d[0] & ~bit); d[1] = (ci & 2) ? (d[1] | bit) : (d[1] & ~bit);
}
u32 se_color(gb::Machine& m, const SprPiece& p, int ci) {
    if (m.cgb) return p.obj ? m.objcol[p.pal & 7][ci] : m.bgcol[p.pal & 7][ci];
    if (p.obj) { u8 obp = p.obp1 ? m.io[0x49] : m.io[0x48]; return (p.obp1 ? m.dmg_obj1 : m.dmg_obj0)[(obp >> (ci * 2)) & 3]; }
    return m.dmg_palette[(m.io[0x47] >> (ci * 2)) & 3];
}
void se_remember(gb::Machine& m, const SprPiece& p) {
    if (SE.orig.count(p.tile)) return;
    std::array<u8, 16> a; memcpy(a.data(), se_tile(m, p.tile), 16);
    SE.orig[p.tile] = a;
    PixelInfo pi;
    pi.tile = m.cgb ? (u16)p.tile : (u16)(p.tile | (p.obj ? (p.obp1 ? 0xC000 : 0x8000) : 0));
    pi.pal = m.cgb ? (u8)((p.obj ? 8 : 0) + (p.pal & 7)) : (p.obj ? (p.obp1 ? m.io[0x49] : m.io[0x48]) : m.io[0x47]);
    pi.sub = 0;
    SE.hdpi[p.tile] = pi;
    SE.hdkey[p.tile] = hd_tile_key(m, pi);
}
void se_select(gb::Machine& m, int px, int py) {
    SE.pieces.clear();
    const int hgt = (m.io[0x40] & 0x04) ? 16 : 8;
    struct Ob { int x, y, i; };
    std::vector<Ob> all, grp;
    for (int i = 0; i < 40; i++) {
        int sy = m.oam[i * 4] - 16, sx = m.oam[i * 4 + 1] - 8;
        if (sx <= -8 || sx >= 160 || sy <= -hgt || sy >= 144) continue;
        all.push_back({sx, sy, i});
    }
    int hit = -1;
    if (m.io[0x40] & 0x02)
        for (size_t k = 0; k < all.size() && hit < 0; k++)
            if (px >= all[k].x && px < all[k].x + 8 && py >= all[k].y && py < all[k].y + hgt) hit = (int)k;
    if (hit >= 0) {
        // the whole character: every object touching the clicked one, directly or through others
        std::vector<bool> in(all.size(), false);
        in[hit] = true; grp.push_back(all[hit]);
        for (size_t q = 0; q < grp.size() && grp.size() < 24; q++)
            for (size_t k = 0; k < all.size(); k++) {
                if (in[k]) continue;
                if (all[k].x <= grp[q].x + 8 && all[k].x + 8 >= grp[q].x && all[k].y <= grp[q].y + hgt && all[k].y + hgt >= grp[q].y) { in[k] = true; grp.push_back(all[k]); }
            }
        int x0 = 999, y0 = 999, x1 = -999, y1 = -999;
        for (auto& o : grp) { x0 = std::min(x0, o.x); y0 = std::min(y0, o.y); x1 = std::max(x1, o.x + 8); y1 = std::max(y1, o.y + hgt); }
        SE.cw = x1 - x0; SE.ch = y1 - y0;
        for (auto it = grp.rbegin(); it != grp.rend(); ++it) {         // lower object numbers are drawn on top: add them last
            u8 tile = m.oam[it->i * 4 + 2], at = m.oam[it->i * 4 + 3];
            if (hgt == 16) tile &= 0xFE;
            int bank = (m.cgb && (at & 0x08)) ? 384 : 0;
            for (int h = 0; h < hgt / 8; h++) {
                int part = (at & 0x40) ? (hgt / 8 - 1 - h) : h;
                SE.pieces.push_back({it->x - x0, it->y - y0 + h * 8, bank + tile + part, (at & 0x20) != 0, (at & 0x40) != 0, at & 7, true, (at & 0x10) != 0});
            }
        }
        SE.status = "Personagem com " + std::to_string(grp.size()) + " objeto(s) selecionado.";
    } else {
        const PixelInfo& pi = m.pinfo[py * gb::Machine::WIDE_W + gb::Machine::WIDE_X0 + px];
        if (pi.tile == 0xFFFF) { SE.status = "Nada para editar neste ponto."; SE.cw = SE.ch = 0; return; }
        int t = m.cgb ? pi.tile : (pi.tile & 0x1FF);
        SE.cw = SE.ch = 8;
        SE.pieces.push_back({0, 0, t, false, false, m.cgb ? (pi.pal & 7) : 0, false, false});
        SE.status = "Peça do cenário selecionada (tile " + std::to_string(t % 384) + ").";
    }
    for (auto& p : SE.pieces) se_remember(m, p);
    SE.undo.clear();
}
const SprPiece* se_piece_at(int x, int y, int& lx, int& ly) {
    for (auto it = SE.pieces.rbegin(); it != SE.pieces.rend(); ++it)
        if (x >= it->x && x < it->x + 8 && y >= it->y && y < it->y + 8) { lx = x - it->x; ly = y - it->y; return &*it; }
    return nullptr;
}
std::map<int, std::array<u8, 16>> se_state(gb::Machine& m) {
    std::map<int, std::array<u8, 16>> s;
    for (auto& p : SE.pieces) { std::array<u8, 16> a; memcpy(a.data(), se_tile(m, p.tile), 16); s[p.tile] = a; }
    return s;
}
void se_snapshot(gb::Machine& m) {
    SE.undo.push_back(se_state(m));
    if (SE.undo.size() > 100) SE.undo.erase(SE.undo.begin());
    SE.redo.clear();
}
void se_restore(gb::Machine& m, const std::map<int, std::array<u8, 16>>& st) { for (auto& kv : st) memcpy(se_tile(m, kv.first), kv.second.data(), 16); SE.edited = true; }
// the whole canvas as colour numbers (-1 = no piece there)
std::vector<int> se_read(gb::Machine& m) {
    std::vector<int> b((size_t)SE.cw * SE.ch, -1);
    for (int y = 0; y < SE.ch; y++) for (int x = 0; x < SE.cw; x++) { int lx, ly; if (const SprPiece* pc = se_piece_at(x, y, lx, ly)) b[(size_t)y * SE.cw + x] = se_get(m, *pc, lx, ly); }
    return b;
}
void se_write(gb::Machine& m, const std::vector<int>& b) {
    for (int y = 0; y < SE.ch; y++) for (int x = 0; x < SE.cw; x++) {
        int v = b[(size_t)y * SE.cw + x], lx, ly;
        if (v < 0) continue;
        if (const SprPiece* pc = se_piece_at(x, y, lx, ly)) se_set(m, *pc, lx, ly, v);
    }
}
bool se_flat(const std::array<u8, 16>& a) { for (int i = 1; i < 16; i++) if (a[i] != a[0]) return false; return true; }
// remembers original -> edited graphics of the tiles being drawn, for "keep the edit while the game runs"
// (only right after an edit, and only for the tiles on the canvas: slots the game reused later are left alone)
void se_update_pins(gb::Machine& m) {
    if (!SE.edited) return;
    SE.edited = false;
    for (auto& p : SE.pieces) {
        auto it = SE.orig.find(p.tile);
        if (it == SE.orig.end() || se_flat(it->second)) continue;   // blank tiles are shared by everything: never pinned
        std::array<u8, 16> now; memcpy(now.data(), se_tile(m, p.tile), 16);
        if (now != it->second) SE.pinned[it->second] = now; else SE.pinned.erase(it->second);
    }
}
void se_apply_pins(gb::Machine& m) {
    if (SE.pinned.empty()) return;
    const int tiles = m.cgb ? 768 : 384;
    std::array<u8, 16> a;
    for (int t = 0; t < tiles; t++) {
        u8* d = se_tile(m, t);
        memcpy(a.data(), d, 16);
        auto it = SE.pinned.find(a);
        if (it != SE.pinned.end()) memcpy(d, it->second.data(), 16);
    }
}
// a piece (or a few neighbouring tiles) picked from the sheet of loaded sprite tiles
void se_select_tiles(gb::Machine& m, int bank, int t) {
    SE.pieces.clear();
    const int b = bank * 384;
    auto add = [&](int x, int y, int tile) { SE.pieces.push_back({x, y, b + (tile & 0xFF), false, false, 0, true, false}); };
    switch (SE.layout) {
    case 1: add(0, 0, t & 0xFE); add(0, 8, (t & 0xFE) + 1); SE.cw = 8; SE.ch = 16; break;
    case 2: add(0, 0, t); add(0, 8, t + 1); add(8, 0, t + 2); add(8, 8, t + 3); SE.cw = SE.ch = 16; break;
    case 3: add(0, 0, t); add(8, 0, t + 1); add(0, 8, t + 2); add(8, 8, t + 3); SE.cw = SE.ch = 16; break;
    default: add(0, 0, t); SE.cw = SE.ch = 8; break;
    }
    for (auto& p : SE.pieces) se_remember(m, p);
    SE.undo.clear(); SE.redo.clear();
    SE.status = "Peça " + std::to_string(t) + (SE.layout ? " e vizinhas" : "") + " escolhida na folha de peças carregadas.";
}
} // namespace

void draw_sprite_editor(Project& p, const StudioMsg& msg) {
    using gb::Machine;
    if (!SE.m || SE.proj_dir != p.dir) {
        SE = SprEdit();
        SE.proj_dir = p.dir;
        SE.m = std::make_unique<Machine>();
        std::string err;
        if (!SE.m->load_rom(p.rom, err)) { SE.m.reset(); ImGui::TextWrapped("Não foi possível iniciar o jogo: %s", err.c_str()); return; }
        SE.m->track_pixels = true;
    }
    Machine& m = *SE.m;
    if (!SE.tex && msg.ren) { SE.tex = SDL_CreateTexture(msg.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 160, 144); SDL_SetTextureScaleMode(SE.tex, SDL_ScaleModeNearest); }
    ImGui::TextWrapped("1) Jogue aqui até o personagem aparecer (setas, Z = A, X = B, Enter = Start, Shift = Select).  2) Clique nele: o jogo pausa e ele abre ao lado.  3) Pinte e clique em \"Salvar no jogo\".");
    // ---- the game
    ImGuiIO& io = ImGui::GetIO();
    u8 b = 0;
    if (!SE.paused && !io.WantTextInput) {
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        if (ks[SDL_SCANCODE_RIGHT]) b |= gb::BTN_RIGHT; if (ks[SDL_SCANCODE_LEFT]) b |= gb::BTN_LEFT;
        if (ks[SDL_SCANCODE_UP]) b |= gb::BTN_UP; if (ks[SDL_SCANCODE_DOWN]) b |= gb::BTN_DOWN;
        if (ks[SDL_SCANCODE_Z]) b |= gb::BTN_A; if (ks[SDL_SCANCODE_X]) b |= gb::BTN_B;
        if (ks[SDL_SCANCODE_RETURN]) b |= gb::BTN_START; if (ks[SDL_SCANCODE_RSHIFT] || ks[SDL_SCANCODE_LSHIFT]) b |= gb::BTN_SELECT;
    }
    if (!SE.paused) { m.buttons = b; m.run_frame(nullptr); m.apu->out.clear(); if (SE.keep_live) se_apply_pins(m); }
    if (SE.tex) SDL_UpdateTexture(SE.tex, nullptr, m.fb, 160 * 4);
    // two scrolling columns: the game (and the tile sheet) on the left, the drawing on the right
    const float gs = std::max(2.f, std::min(3.f, std::floor((ImGui::GetContentRegionAvail().y - 120) / 144 * 2) / 2));
    ImGui::BeginChild("se_jogo", ImVec2(std::max(160 * gs, 128 * 3.f) + 24, 0));
    if (ImGui::Button(SE.paused ? "Continuar o jogo" : "Pausar")) SE.paused = !SE.paused;
    ImGui::SameLine();
    if (ImGui::Button("Avançar 1 quadro")) { SE.paused = true; m.buttons = 0; m.run_frame(nullptr); m.apu->out.clear(); if (SE.keep_live) se_apply_pins(m); }
    ImGui::SameLine();
    if (ImGui::Button("Reiniciar")) { std::string e; m.load_rom(p.rom, e); m.track_pixels = true; SE.pieces.clear(); SE.cw = SE.ch = 0; SE.orig.clear(); SE.hdkey.clear(); SE.hdpi.clear(); SE.undo.clear(); SE.redo.clear(); SE.pinned.clear(); SE.paused = false; }
    ImVec2 g0 = ImGui::GetCursorScreenPos();
    ImGui::Image(ui::tex_ref(SE.tex), ImVec2(160 * gs, 144 * gs));
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
        ImVec2 mp = ImGui::GetMousePos();
        int px = (int)((mp.x - g0.x) / gs), py = (int)((mp.y - g0.y) / gs);
        if (px >= 0 && px < 160 && py >= 0 && py < 144) { SE.paused = true; se_select(m, px, py); }
    }
    ImGui::TextDisabled(SE.paused ? "Pausado. Clique em outro personagem ou continue o jogo." : "Clique num personagem para editar.");
    ImGui::Checkbox("Manter minhas mudanças enquanto o jogo roda", &SE.keep_live);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("O jogo recarrega os gráficos dos personagens o tempo todo (ao andar, pular...).\nCom isto ligado, a sua versão volta sempre que o jogo carrega a peça original,\npara você ver o personagem novo se mexendo antes de salvar.");
    // ---- every sprite tile in video memory: edit poses that are not on screen right now
    if (ImGui::CollapsingHeader("Todas as peças de personagem carregadas")) {
        const int banks = m.cgb ? 2 : 1;
        if (banks > 1) { ImGui::RadioButton("Banco 0", &SE.sheet_bank, 0); ImGui::SameLine(); ImGui::RadioButton("Banco 1", &SE.sheet_bank, 1); }
        static const char* lay[] = {"1 peça (8x8)", "2 em pé (8x16)", "4 peças, em colunas (16x16)", "4 peças, em linhas (16x16)"};
        ImGui::SetNextItemWidth(260); ImGui::Combo("Ao clicar, pegar", &SE.layout, lay, 4);
        if (!SE.sheet && msg.ren) { SE.sheet = SDL_CreateTexture(msg.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 128); SDL_SetTextureScaleMode(SE.sheet, SDL_ScaleModeNearest); }
        if (SE.sheet) {
            static uint32_t px[128 * 128];
            SprPiece pc{0, 0, 0, false, false, 0, true, false};
            for (int t = 0; t < 256; t++) {
                pc.tile = SE.sheet_bank * 384 + t;
                for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
                    int ci = se_get(m, pc, x, y);
                    px[((t / 16) * 8 + y) * 128 + (t % 16) * 8 + x] = ci ? (se_color(m, pc, ci) | 0xFF000000u) : (((x ^ y) & 4) ? 0xFF3A3050u : 0xFF463C60u);
                }
            }
            SDL_UpdateTexture(SE.sheet, nullptr, px, 128 * 4);
            ImVec2 s0 = ImGui::GetCursorScreenPos();
            const float ss = 3.0f;
            ImGui::Image(ui::tex_ref(SE.sheet), ImVec2(128 * ss, 128 * ss));
            if (ImGui::IsItemHovered()) {
                ImVec2 mp = ImGui::GetMousePos();
                int t = (int)((mp.y - s0.y) / (8 * ss)) * 16 + (int)((mp.x - s0.x) / (8 * ss));
                if (t >= 0 && t < 256) {
                    ImGui::SetTooltip("Peça %d", t);
                    if (ImGui::IsMouseClicked(0)) { SE.paused = true; se_select_tiles(m, SE.sheet_bank, t); }
                }
            }
            ImGui::TextWrapped("Estas são as peças que o jogo carregou agora. Outras poses aparecem aqui quando o jogo as carrega.");
        }
    }
    ImGui::EndChild();
    ImGui::SameLine(0, 16);
    // ---- the canvas
    ImGui::BeginChild("se_desenho", ImVec2(0, 0));
    if (SE.cw <= 0) {
        ImGui::TextDisabled("Nenhum personagem selecionado.");
        ImGui::TextWrapped("Clique num personagem no jogo, ou abra \"Todas as peças de personagem carregadas\" e escolha uma peça.");
        ImGui::EndChild(); return;
    }
    ImGui::TextUnformatted(SE.status.c_str());
    const float z = SE.zoom > 0 ? SE.zoom : std::max(6.f, std::min(28.f, std::floor(400.f / std::max(SE.cw, SE.ch))));
    ImVec2 c0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("canvas", ImVec2(SE.cw * z, SE.ch * z));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int y = 0; y < SE.ch; y++) for (int x = 0; x < SE.cw; x++) {     // checkerboard = transparent
        bool d = ((x >> 2) + (y >> 2)) & 1;
        dl->AddRectFilled(ImVec2(c0.x + x * z, c0.y + y * z), ImVec2(c0.x + (x + 1) * z, c0.y + (y + 1) * z), d ? IM_COL32(70, 60, 100, 255) : IM_COL32(88, 76, 122, 255));
    }
    for (auto& pc : SE.pieces)
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
            int ci = se_get(m, pc, x, y);
            if (pc.obj && ci == 0) continue;
            u32 c = se_color(m, pc, ci);
            dl->AddRectFilled(ImVec2(c0.x + (pc.x + x) * z, c0.y + (pc.y + y) * z), ImVec2(c0.x + (pc.x + x + 1) * z, c0.y + (pc.y + y + 1) * z), IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, 255));
        }
    if (z >= 10) for (int k = 0; k <= SE.cw; k += 8) dl->AddLine(ImVec2(c0.x + k * z, c0.y), ImVec2(c0.x + k * z, c0.y + SE.ch * z), IM_COL32(255, 255, 255, 40));
    if (z >= 10) for (int k = 0; k <= SE.ch; k += 8) dl->AddLine(ImVec2(c0.x, c0.y + k * z), ImVec2(c0.x + SE.cw * z, c0.y + k * z), IM_COL32(255, 255, 255, 40));
    const bool l = ImGui::IsMouseDown(0), r = ImGui::IsMouseDown(1);
    if (hov && (l || r)) {
        ImVec2 mp = ImGui::GetMousePos();
        int x = (int)((mp.x - c0.x) / z), y = (int)((mp.y - c0.y) / z), lx, ly;
        if (const SprPiece* pc = se_piece_at(x, y, lx, ly)) {
            int want = r ? 0 : SE.color;
            if (SE.tool == 2 && l) SE.color = se_get(m, *pc, lx, ly);                  // dropper
            else if (SE.tool == 1 && l) {                                              // bucket (inside the character)
                if (ImGui::IsMouseClicked(0)) {
                    se_snapshot(m);
                    int from = se_get(m, *pc, lx, ly);
                    if (from != want) {
                        std::vector<std::pair<int, int>> st{{x, y}};
                        while (!st.empty()) {
                            auto q = st.back(); st.pop_back();
                            int qx, qy; const SprPiece* qp = se_piece_at(q.first, q.second, qx, qy);
                            if (!qp || se_get(m, *qp, qx, qy) != from) continue;
                            se_set(m, *qp, qx, qy, want);
                            st.push_back({q.first + 1, q.second}); st.push_back({q.first - 1, q.second}); st.push_back({q.first, q.second + 1}); st.push_back({q.first, q.second - 1});
                        }
                    }
                }
            } else {
                if (!SE.stroke) { se_snapshot(m); SE.stroke = true; }
                se_set(m, *pc, lx, ly, want);
            }
        }
    }
    if (!l && !r) SE.stroke = false;
    // keyboard shortcuts (with the game paused, or Ctrl combinations)
    if (!io.WantTextInput) {
        const bool ctrl = io.KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !SE.undo.empty()) { SE.redo.push_back(se_state(m)); se_restore(m, SE.undo.back()); SE.undo.pop_back(); }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false) && !SE.redo.empty()) { SE.undo.push_back(se_state(m)); se_restore(m, SE.redo.back()); SE.redo.pop_back(); }
        if (SE.paused && !ctrl) {
            for (int i = 0; i < 4; i++) if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i), false)) SE.color = i;
            if (ImGui::IsKeyPressed(ImGuiKey_B, false)) SE.tool = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_G, false)) SE.tool = 1;
            if (ImGui::IsKeyPressed(ImGuiKey_I, false)) SE.tool = 2;
        }
    }
    // tools
    ImGui::Text("Cor:");
    const SprPiece& p0 = SE.pieces.back();
    for (int i = 0; i < 4; i++) {
        ImGui::SameLine();
        u32 c = se_color(m, p0, i);
        ImGui::PushID(i);
        if (p0.obj && i == 0) { if (ImGui::Button(SE.color == 0 ? "[vazio]" : "vazio")) SE.color = 0; }
        else if (ImGui::ColorButton("c", ImVec4(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(SE.color == i ? 36 : 28, SE.color == i ? 36 : 28))) SE.color = i;
        ImGui::PopID();
    }
    ImGui::RadioButton("Lápis", &SE.tool, 0); ImGui::SameLine(); ImGui::RadioButton("Balde", &SE.tool, 1); ImGui::SameLine(); ImGui::RadioButton("Conta-gotas", &SE.tool, 2);
    ImGui::SameLine(); ImGui::TextDisabled("(botão direito apaga)");
    ImGui::BeginDisabled(SE.undo.empty());
    if (ImGui::Button("Desfazer")) { SE.redo.push_back(se_state(m)); se_restore(m, SE.undo.back()); SE.undo.pop_back(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(SE.redo.empty());
    if (ImGui::Button("Refazer")) { SE.undo.push_back(se_state(m)); se_restore(m, SE.redo.back()); SE.redo.pop_back(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Voltar ao original")) { se_snapshot(m); for (auto& pc : SE.pieces) if (SE.orig.count(pc.tile)) memcpy(se_tile(m, pc.tile), SE.orig[pc.tile].data(), 16); SE.edited = true; }
    ImGui::SetNextItemWidth(180);
    float zz = z; if (ImGui::SliderFloat("Zoom", &zz, 4, 32, "%.0fx")) SE.zoom = std::floor(zz);
    // whole-character transformations
    {
        auto transform = [&](auto fn) { se_snapshot(m); std::vector<int> a = se_read(m), b = a; fn(a, b); se_write(m, b); };
        const int W = SE.cw, H = SE.ch;
        if (ImGui::Button("Espelhar ↔")) transform([&](std::vector<int>& a, std::vector<int>& b) { for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (a[y * W + x] >= 0 && a[y * W + W - 1 - x] >= 0) b[y * W + x] = a[y * W + W - 1 - x]; });
        ImGui::SameLine();
        if (ImGui::Button("Espelhar ↕")) transform([&](std::vector<int>& a, std::vector<int>& b) { for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (a[y * W + x] >= 0 && a[(H - 1 - y) * W + x] >= 0) b[y * W + x] = a[(H - 1 - y) * W + x]; });
        ImGui::SameLine(0, 16); ImGui::TextDisabled("Mover:");
        const int dirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        const char* dn[4] = {"←", "→", "↑", "↓"};
        for (int d = 0; d < 4; d++) {
            ImGui::SameLine(); ImGui::PushID(100 + d);
            if (ImGui::Button(dn[d])) transform([&](std::vector<int>& a, std::vector<int>& b) {
                for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
                    int sx = ((x - dirs[d][0]) % W + W) % W, sy = ((y - dirs[d][1]) % H + H) % H;
                    if (b[y * W + x] >= 0 && a[sy * W + sx] >= 0) b[y * W + x] = a[sy * W + sx];
                }
            });
            ImGui::PopID();
        }
        static int from = 1, to = 2;
        ImGui::TextUnformatted("Cor"); ImGui::SameLine(); ImGui::SetNextItemWidth(70); ImGui::InputInt("##de", &from, 0); ImGui::SameLine(); ImGui::TextUnformatted("→"); ImGui::SameLine();
        ImGui::SetNextItemWidth(70); ImGui::InputInt("##para", &to, 0); ImGui::SameLine();
        from = std::max(0, std::min(3, from)); to = std::max(0, std::min(3, to));
        if (ImGui::Button("Trocar cor")) transform([&](std::vector<int>& a, std::vector<int>& b) { for (size_t i = 0; i < a.size(); i++) if (a[i] == from) b[i] = to; });
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Troca todos os pixels da cor %d pela cor %d (0 = vazio/fundo, 3 = a mais escura).", from, to);
        // PNG: draw in another program (Aseprite, Paint, Photoshop...) and bring it back
        if (ImGui::Button("Exportar PNG")) {
            std::vector<int> a = se_read(m);
            std::vector<uint32_t> px(a.size(), 0);
            const SprPiece& pp = SE.pieces.back();
            for (size_t i = 0; i < a.size(); i++) if (a[i] > 0 || (a[i] == 0 && !pp.obj)) px[i] = se_color(m, pp, a[i]) | 0xFF000000u;
            std::string dir = p.path("sprites");
            plat::make_dirs(dir);
            std::string f = plat::join(dir, "personagem.png");
            if (save_png(f, px.data(), SE.cw, SE.ch)) { msg.notify("Salvo em " + f + ". Edite e use \"Importar PNG\"."); plat::open_in_explorer(dir); }
            else msg.error("Exportar PNG", "Não foi possível gravar " + f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Importar PNG")) {
            std::string f = plat::join(p.path("sprites"), "personagem.png");
            std::vector<uint32_t> px; int w = 0, h = 0;
            if (!load_png(f, px, w, h)) msg.error("Importar PNG", "Coloque a imagem em " + f + " (use \"Exportar PNG\" primeiro para ter o tamanho certo).");
            else if (w % SE.cw || h % SE.ch || w / SE.cw != h / SE.ch) msg.error("Importar PNG", "A imagem tem " + std::to_string(w) + "x" + std::to_string(h) + " pixels; o personagem tem " + std::to_string(SE.cw) + "x" + std::to_string(SE.ch) + " (ou um múltiplo exato disso).");
            else {
                const int k = w / SE.cw;
                const SprPiece& pp = SE.pieces.back();
                transform([&](std::vector<int>&, std::vector<int>& b) {
                    for (int y = 0; y < SE.ch; y++) for (int x = 0; x < SE.cw; x++) {
                        if (b[y * SE.cw + x] < 0) continue;
                        uint32_t c = px[(size_t)(y * k) * w + x * k];
                        if ((c >> 24) < 128) { b[y * SE.cw + x] = 0; continue; }   // see-through: background
                        int best = pp.obj ? 1 : 0; long bd = -1;                  // nearest of the four colours
                        for (int i = pp.obj ? 1 : 0; i < 4; i++) {
                            u32 q = se_color(m, pp, i);
                            long dr = (long)((c >> 16) & 255) - ((q >> 16) & 255), dg = (long)((c >> 8) & 255) - ((q >> 8) & 255), db = (long)(c & 255) - (q & 255);
                            long d2 = dr * dr * 3 + dg * dg * 4 + db * db * 2;
                            if (bd < 0 || d2 < bd) { bd = d2; best = i; }
                        }
                        b[y * SE.cw + x] = best;
                    }
                });
                msg.notify("Imagem importada. As cores foram ajustadas às 4 do Game Boy.");
            }
        }
        ImGui::SameLine(); ImGui::TextDisabled("(para desenhar em outro programa)");
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("Atalhos (com o jogo pausado): 1-4 cor · B lápis · G balde · I conta-gotas · Ctrl+Z desfazer · Ctrl+Y refazer");
        ImGui::PopStyleColor();
    }
    // which tiles changed, and where each one can be saved
    int changed = 0;
    for (auto& kv : SE.orig) if (memcmp(se_tile(m, kv.first), kv.second.data(), 16)) changed++;
    ImGui::BeginDisabled(changed == 0);
    if (ImGui::Button("Salvar no jogo", ImVec2(200, 0))) {
        std::vector<u8> rom = p.rom;
        int in_rom = 0, in_hd = 0;
        std::string hd_dir = p.path("hd/editor");
        for (auto& kv : SE.orig) {
            const u8* now = se_tile(m, kv.first);
            if (!memcmp(now, kv.second.data(), 16)) continue;
            // the same 16 bytes in the ROM: the graphics are stored uncompressed and can be changed in place
            bool found = false, blank = true;
            for (int i = 1; i < 16; i++) if (kv.second[i] != kv.second[0]) blank = false;
            if (!blank)
                for (size_t off = 0; off + 16 <= rom.size(); off++)
                    if (rom[off] == kv.second[0] && !memcmp(&rom[off], kv.second.data(), 16)) { memcpy(&rom[off], now, 16); found = true; off += 15; }
            if (found) { in_rom++; continue; }
            // compressed or generated graphics: the new look goes into an HD pack that replaces the tile on screen
            plat::make_dirs(hd_dir);
            uint32_t px[64];
            PixelInfo pi = SE.hdpi[kv.first];
            tile_rgba(m, pi, px);
            if (pi.pal >= 8 || (pi.tile & 0x8000)) for (int i = 0; i < 64; i++) {            // objects: colour 0 stays see-through
                const u8* d = now + (i / 8) * 2; int bit = 7 - (i % 8);
                if (!((((d[1] >> bit) & 1) << 1) | ((d[0] >> bit) & 1))) px[i] = 0;
            }
            char name[32]; snprintf(name, sizeof name, "%016llx.png", (unsigned long long)SE.hdkey[kv.first]);
            if (save_png(plat::join(hd_dir, name), px, 8, 8)) in_hd++;
        }
        bool ok = true;
        if (in_rom) ok = p.write_working_rom(rom, "editor de sprites");
        if (in_hd) { plat::write_text(plat::join(hd_dir, "pack.ini"), "# Pacote criado pelo editor de sprites do NativeBoy Studio\nscale=1\n"); p.ini.set("hd_padrao", "1"); p.save(); }
        if (!ok) msg.error("Editor de sprites", "Não foi possível gravar a ROM de trabalho.");
        else {
            std::string t = "Salvo: " + std::to_string(in_rom) + " peça(s) direto na ROM";
            if (in_hd) t += ", " + std::to_string(in_hd) + " pelo pacote HD \"editor\" (gráficos comprimidos)";
            msg.notify(t + ". Converta de novo para gerar o jogo.");
            for (auto& kv : SE.orig) memcpy(kv.second.data(), se_tile(m, kv.first), 16);
            for (auto& pc : SE.pieces) { SE.orig.erase(pc.tile); }
            for (auto& pc : SE.pieces) se_remember(m, pc);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%d peça(s) alterada(s)", changed);
    se_update_pins(m);
    ImGui::TextWrapped("Continue o jogo para ver o personagem novo em movimento. Gráficos guardados sem compressão são trocados direto na ROM; os comprimidos são trocados na tela por um pacote HD (nesse caso só valem para as cores com que apareceram aqui, e pixels novos fora do contorno original não aparecem).");
    ImGui::EndChild();
}

// ================================================================== mod assistant
void draw_mod_assistant(Project& p, const StudioMsg& msg, std::string& goto_tab) {
    ImGui::TextWrapped("Escolha o que você quer mudar no jogo. Cada receita faz a mudança aqui mesmo ou abre a ferramenta certa, já explicando o passo a passo. Nada disso exige programar.");
    ImGui::Spacing();
    auto recipe = [&](const char* title, const char* text) { ImGui::SeparatorText(title); ImGui::TextWrapped("%s", text); };

    recipe("Redesenhar um personagem ou o cenário", "Jogue dentro do Studio, clique no personagem e pinte por cima.");
    if (ImGui::Button("Abrir o editor de sprites")) goto_tab = "Editor de sprites";

    recipe("Vidas, energia ou tempo infinitos", "1) Clique em \"Jogar para procurar\".  2) No jogo, abra o menu (Esc) > Trapaças > Localizador.  3) Digite o valor atual (por exemplo 3 vidas) e procure; perca uma vida, digite o novo valor e procure de novo, até sobrar um endereço.  4) O localizador mostra o código (formato 01VVAAAA): cole abaixo com um nome.");
    if (ImGui::Button("Jogar para procurar") && msg.test_play) msg.test_play();
    static char cname[64] = "", ccode[16] = "";
    ImGui::SetNextItemWidth(220); ImGui::InputTextWithHint("##cn", "Nome (ex.: vidas infinitas)", cname, sizeof cname);
    ImGui::SameLine(); ImGui::SetNextItemWidth(130); ImGui::InputTextWithHint("##cc", "01VVAAAA", ccode, sizeof ccode, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();
    if (ImGui::Button("Adicionar ao jogo")) {
        std::string c = ccode;
        if (c.size() != 8 || c.compare(0, 2, "01") != 0) msg.error("Código inválido", "O código precisa ter 8 dígitos e começar com 01 (formato GameShark 01VVAAAA).");
        else {
            std::string cur; plat::read_text(p.path("mods/cheats.txt"), cur);
            if (!cur.empty() && cur.back() != '\n') cur += "\n";
            cur += std::string("# ") + (cname[0] ? cname : "trapaça") + "\n" + c + "\n";
            plat::make_dirs(p.path("mods")); plat::write_text(p.path("mods/cheats.txt"), cur);
            msg.notify("Trapaça adicionada. Ela vale no jogo exportado (fora do modo fidelidade).");
            cname[0] = ccode[0] = 0;
        }
    }
    {
        std::string cur; int n = 0;
        if (plat::read_text(p.path("mods/cheats.txt"), cur)) for (auto& l : split_lines(cur)) { std::string t = trim(l); if (t.size() >= 8 && t[0] != '#') n++; }
        ImGui::SameLine(); ImGui::TextDisabled("%d trapaça(s) no projeto", n);
    }

    recipe("Jogo mais rápido ou mais lento", "Muda a velocidade do jogo exportado. 100% é a velocidade original.");
    int sp = std::max(50, std::min(200, p.ini.geti("padrao_velocidade", 100)));
    ImGui::SetNextItemWidth(300);
    if (ImGui::SliderInt("Velocidade", &sp, 50, 200, "%d%%")) p.ini.seti("padrao_velocidade", sp);
    if (ImGui::IsItemDeactivatedAfterEdit()) p.save();
    ImGui::SameLine(); if (ImGui::SmallButton("100%")) { p.ini.seti("padrao_velocidade", 100); p.save(); }

    recipe("Sem piscadas e sem lentidão de sprites", "O Game Boy só desenha 10 sprites por linha. Ligando esta opção o jogo exportado desenha todos.");
    bool nsl = p.ini.getb("padrao_sem_limite_sprites", false);
    if (ImGui::Checkbox("Desenhar todos os sprites", &nsl)) { p.ini.set("padrao_sem_limite_sprites", nsl ? "1" : "0"); p.save(); }

    recipe("Trocar textos, músicas, cores, ícone", "Estas mudanças têm ferramentas próprias:");
    if (ImGui::Button("Textos (tradução)")) goto_tab = "Tradução";
    ImGui::SameLine(); if (ImGui::Button("Músicas")) goto_tab = "Trilha sonora";
    ImGui::SameLine(); if (ImGui::Button("Cores e tela")) goto_tab = "Cores e melhorias";
    ImGui::SameLine(); if (ImGui::Button("Ícone e abertura")) goto_tab = "Ícone e abertura";
    ImGui::SameLine(); if (ImGui::Button("Conquistas")) goto_tab = "Conquistas";

    recipe("Compartilhar o que você mudou", "Em \"Patches e mods\" você cria um mod com todas as alterações da ROM, para aplicar em outro computador.");
    if (ImGui::Button("Abrir Patches e mods")) goto_tab = "Patches e mods";
}
