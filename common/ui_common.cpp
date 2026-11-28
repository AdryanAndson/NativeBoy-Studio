#include "ui_common.h"
#include "platform.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <SDL_syswm.h>
#include <windows.h>
#endif

namespace ui {
float base_size = 18.0f;
ImFont* font_text = nullptr;
ImFont* font_pixel = nullptr;
ImFont* font_head = nullptr;

static ImFont* load_one(const std::vector<std::string>& cands, float size, const ImWchar* ranges, bool pixel) {
    ImGuiIO& io = ImGui::GetIO();
    for (auto& c : cands) {
        if (!plat::file_exists(c)) continue;
        std::vector<uint8_t> data;
        if (!plat::read_file(c, data) || data.empty()) continue;
        ImFontConfig cfg;
        cfg.OversampleH = pixel ? 1 : 2;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = pixel;
        // ImGui keeps the pointer: give it its own copy
        void* mem = IM_ALLOC(data.size());
        memcpy(mem, data.data(), data.size());
        if (ImFont* f = io.Fonts->AddFontFromMemoryTTF(mem, (int)data.size(), size, &cfg, ranges)) return f;
    }
    return nullptr;
}

void load_fonts(const std::string& base, float size) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x21FF, 0x25A0, 0x25FF, 0x2600, 0x26FF, 0};
    font_text = load_one({plat::join(base, "assets/fonts/DejaVuSans.ttf"), plat::join(base, "fonts/DejaVuSans.ttf"),
#ifdef _WIN32
                          "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
#else
                          "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
#endif
                         }, size, ranges, false);
    if (!font_text) font_text = io.Fonts->AddFontDefault();
    font_pixel = load_one({plat::join(base, "assets/fonts/PressStart2P-Regular.ttf"), plat::join(base, "fonts/PressStart2P-Regular.ttf")}, size, ranges, true);
    font_head = load_one({plat::join(base, "assets/fonts/PixelifySans.ttf"), plat::join(base, "fonts/PixelifySans.ttf")}, size, ranges, false);
    if (!font_pixel) font_pixel = font_text;       // (an old game folder without the new fonts)
    if (!font_head) font_head = font_pixel;
    io.FontDefault = font_text;
    base_size = size;
    ImGui::GetStyle().FontSizeBase = size;
}

// the pixel font is drawn on an 8-pixel grid: sizes snap to multiples of 8 so every dot stays square
void push_pixel(float mult) {
    const float want = base_size * mult * 0.72f * ImGui::GetStyle().FontScaleMain;
    const float snapped = std::max(8.f, std::round(want / 8.f) * 8.f);
    ImGui::PushFont(font_pixel, snapped / std::max(0.01f, ImGui::GetStyle().FontScaleMain));
}
void push_head(float mult) { ImGui::PushFont(font_head, base_size * mult * 1.08f); }

// ------------------------------------------------------------------ themes
bool theme_gb = false, theme_gbc = true, theme_retro = false;
const char* theme_ids[2] = {"gb", "gbc"};
const char* theme_names[2] = {"GB clássico (cinza e tela verde)", "GB Color (carcaça colorida)"};

static const Shell kShells[] = {
    {"uva", "Uva (roxo)", IM_COL32(104, 64, 178, 255)},
    {"morango", "Morango (vermelho)", IM_COL32(206, 52, 88, 255)},
    {"petroleo", "Azul-petróleo", IM_COL32(24, 142, 168, 255)},
    {"kiwi", "Kiwi (verde-limão)", IM_COL32(120, 168, 28, 255)},
    {"amarelo", "Dente-de-leão (amarelo)", IM_COL32(226, 178, 26, 255)},
    {"atomico", "Atômico (transparente)", IM_COL32(126, 104, 176, 255)},
};
const Shell* shells(int* count) { if (count) *count = (int)(sizeof kShells / sizeof kShells[0]); return kShells; }

static Pal P;
const Pal& pal() { return P; }
static unsigned acc[5];
unsigned accent(int i) { return acc[((i % 5) + 5) % 5]; }

static ImVec4 v4(ImU32 c, float a = -1) { ImVec4 v = ImGui::ColorConvertU32ToFloat4(c); if (a >= 0) v.w = a; return v; }
static ImVec4 rgb(int r, int g, int b, float a = 1.f) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); }
static ImVec4 mix(ImVec4 a, ImVec4 b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }
static ImU32 u32(ImVec4 v) { return ImGui::ColorConvertFloat4ToU32(v); }
static float luma(ImVec4 v) { return 0.299f * v.x + 0.587f * v.y + 0.114f * v.z; }

void apply_theme(const std::string& theme_in, const std::string& shell_id) {
    const std::string theme = theme_in == "gb" ? "gb" : "gbc";
    theme_gb = theme == "gb";
    theme_gbc = !theme_gb;
    theme_retro = false;
    ImGuiStyle& s = ImGui::GetStyle();
    const float fsb = s.FontSizeBase;
    s = ImGuiStyle();
    s.FontSizeBase = fsb;
    ImVec4* c = s.Colors;
    // the screen of the grey handheld (also the LCD drawn by both themes)
    P.lcd[0] = IM_COL32(8, 24, 32, 255); P.lcd[1] = IM_COL32(52, 104, 86, 255); P.lcd[2] = IM_COL32(136, 192, 112, 255); P.lcd[3] = IM_COL32(224, 248, 208, 255);
    P.led = IM_COL32(232, 40, 52, 255);
    if (theme_gb) {
        // ---------------------------------------------------------------- GB: light grey shell
        P.light = true;
        const ImVec4 shell = rgb(200, 196, 190), shell_d = rgb(172, 167, 162), shell_l = rgb(222, 219, 214);
        const ImVec4 navy = rgb(34, 38, 92), green_d = rgb(8, 24, 32), magenta = rgb(156, 38, 89);
        P.shell = u32(shell); P.shell_dark = u32(shell_d); P.shell_light = u32(shell_l);
        P.print = u32(navy);
        P.key_a = u32(magenta); P.key_b = u32(rgb(140, 32, 80));
        P.card = u32(rgb(198, 228, 176)); P.card_hover = u32(rgb(214, 240, 196)); P.card_edge = u32(rgb(52, 104, 86));
        P.card_text = u32(green_d); P.card_dim = u32(rgb(40, 84, 70));
        P.ok = rgb(24, 112, 70); P.warn = rgb(150, 92, 0); P.err = rgb(178, 30, 40); P.info = rgb(40, 60, 150); P.dim = rgb(92, 90, 104); P.text = navy;
        acc[0] = u32(magenta); acc[1] = u32(navy); acc[2] = P.lcd[1]; acc[3] = P.lcd[2]; acc[4] = u32(rgb(110, 104, 140));
        s.WindowRounding = 6; s.ChildRounding = 6; s.FrameRounding = 4; s.PopupRounding = 4; s.GrabRounding = 3; s.TabRounding = 4; s.ScrollbarRounding = 4;
        s.WindowBorderSize = 1; s.ChildBorderSize = 1; s.FrameBorderSize = 1; s.PopupBorderSize = 1; s.TabBorderSize = 0;
        s.FramePadding = ImVec2(10, 6); s.ItemSpacing = ImVec2(10, 8); s.WindowPadding = ImVec2(14, 12);
        c[ImGuiCol_Text] = navy;
        c[ImGuiCol_TextDisabled] = rgb(110, 108, 124);
        c[ImGuiCol_WindowBg] = shell;
        c[ImGuiCol_ChildBg] = rgb(190, 186, 180, 0.6f);
        c[ImGuiCol_PopupBg] = shell_l;
        c[ImGuiCol_MenuBarBg] = shell_d;
        c[ImGuiCol_Border] = rgb(128, 124, 136, 0.8f);
        c[ImGuiCol_BorderShadow] = rgb(255, 255, 255, 0.35f);
        c[ImGuiCol_FrameBg] = rgb(214, 236, 200);          // pale screen
        c[ImGuiCol_FrameBgHovered] = rgb(196, 226, 178);
        c[ImGuiCol_FrameBgActive] = rgb(136, 192, 112);
        c[ImGuiCol_Button] = rgb(176, 172, 186);
        c[ImGuiCol_ButtonHovered] = rgb(192, 188, 204);
        c[ImGuiCol_ButtonActive] = rgb(150, 146, 162);
        c[ImGuiCol_Header] = rgb(136, 192, 112, 0.55f);
        c[ImGuiCol_HeaderHovered] = rgb(136, 192, 112, 0.85f);
        c[ImGuiCol_HeaderActive] = rgb(120, 180, 98);
        c[ImGuiCol_CheckMark] = green_d;
        c[ImGuiCol_SliderGrab] = magenta;
        c[ImGuiCol_SliderGrabActive] = rgb(186, 50, 110);
        c[ImGuiCol_Separator] = rgb(128, 124, 136, 0.7f);
        c[ImGuiCol_SeparatorHovered] = magenta;
        c[ImGuiCol_SeparatorActive] = magenta;
        c[ImGuiCol_ResizeGrip] = rgb(128, 124, 136, 0.4f);
        c[ImGuiCol_ResizeGripHovered] = magenta;
        c[ImGuiCol_ResizeGripActive] = magenta;
        c[ImGuiCol_Tab] = shell_d;
        c[ImGuiCol_TabHovered] = rgb(160, 208, 136);
        c[ImGuiCol_TabSelected] = rgb(136, 192, 112);
        c[ImGuiCol_TabSelectedOverline] = magenta;
        c[ImGuiCol_TabDimmed] = shell_d;
        c[ImGuiCol_TabDimmedSelected] = rgb(186, 214, 168);
        c[ImGuiCol_TabDimmedSelectedOverline] = rgb(128, 124, 136);
        c[ImGuiCol_TitleBg] = shell_d;
        c[ImGuiCol_TitleBgActive] = rgb(136, 192, 112);
        c[ImGuiCol_TitleBgCollapsed] = shell_d;
        c[ImGuiCol_ScrollbarBg] = rgb(180, 176, 170, 0.6f);
        c[ImGuiCol_ScrollbarGrab] = rgb(140, 136, 150);
        c[ImGuiCol_ScrollbarGrabHovered] = rgb(120, 116, 132);
        c[ImGuiCol_ScrollbarGrabActive] = magenta;
        c[ImGuiCol_PlotLines] = green_d;
        c[ImGuiCol_PlotHistogram] = rgb(52, 104, 86);
        c[ImGuiCol_PlotHistogramHovered] = magenta;
        c[ImGuiCol_TableHeaderBg] = rgb(176, 172, 186);
        c[ImGuiCol_TableBorderStrong] = rgb(128, 124, 136);
        c[ImGuiCol_TableBorderLight] = rgb(160, 156, 168);
        c[ImGuiCol_TableRowBg] = rgb(0, 0, 0, 0);
        c[ImGuiCol_TableRowBgAlt] = rgb(255, 255, 255, 0.18f);
        c[ImGuiCol_TextSelectedBg] = rgb(136, 192, 112, 0.7f);
        c[ImGuiCol_DragDropTarget] = magenta;
        c[ImGuiCol_NavCursor] = magenta;
        c[ImGuiCol_ModalWindowDimBg] = rgb(40, 40, 50, 0.45f);
        c[ImGuiCol_DockingPreview] = rgb(156, 38, 89, 0.5f);
        c[ImGuiCol_DockingEmptyBg] = shell_d;
    } else {
        // ---------------------------------------------------------------- GBC: dark body tinted by the shell
        P.light = false;
        const Shell* sh = &kShells[0];
        for (auto& k : kShells) if (shell_id == k.id) sh = &k;
        const bool atomic = !strcmp(sh->id, "atomico");
        ImVec4 base = v4(sh->color);
        // keys with white text: very bright shells are darkened a little
        ImVec4 key = luma(base) > 0.55f ? mix(base, rgb(0, 0, 0), 0.30f) : base;
        const ImVec4 night = rgb(14, 12, 26);
        const ImVec4 body = mix(night, base, 0.16f), body2 = mix(night, base, 0.24f), body3 = mix(night, base, 0.34f);
        P.shell = u32(base); P.shell_dark = u32(mix(base, rgb(0, 0, 0), 0.35f)); P.shell_light = u32(mix(base, rgb(255, 255, 255), 0.30f));
        P.print = IM_COL32(240, 236, 255, 255);
        P.key_a = IM_COL32(40, 36, 60, 255); P.key_b = IM_COL32(40, 36, 60, 255);
        P.card = u32(ImVec4(body3.x, body3.y, body3.z, atomic ? 0.82f : 0.95f)); P.card_hover = u32(mix(body3, key, 0.35f));
        P.card_edge = u32(key); P.card_text = IM_COL32(255, 255, 255, 255); P.card_dim = IM_COL32(200, 194, 220, 255);
        P.ok = rgb(110, 224, 140); P.warn = rgb(255, 204, 80); P.err = rgb(255, 110, 110); P.info = rgb(150, 190, 255); P.dim = rgb(170, 164, 190); P.text = rgb(244, 240, 255);
        // berry, grape, kiwi, dandelion, teal
        acc[0] = IM_COL32(214, 44, 86, 255); acc[1] = IM_COL32(124, 78, 206, 255); acc[2] = IM_COL32(140, 196, 30, 255); acc[3] = IM_COL32(244, 196, 26, 255); acc[4] = IM_COL32(28, 164, 190, 255);
        const float wa = atomic ? 0.88f : 0.97f;
        s.WindowRounding = 12; s.ChildRounding = 10; s.FrameRounding = 10; s.PopupRounding = 10; s.GrabRounding = 10; s.TabRounding = 8; s.ScrollbarRounding = 10;
        s.WindowBorderSize = 1; s.ChildBorderSize = 0; s.FrameBorderSize = 0; s.PopupBorderSize = 1; s.TabBorderSize = 0;
        s.FramePadding = ImVec2(12, 6); s.ItemSpacing = ImVec2(10, 8); s.WindowPadding = ImVec2(14, 12);
        c[ImGuiCol_Text] = P.text;
        c[ImGuiCol_TextDisabled] = rgb(160, 152, 186);
        c[ImGuiCol_WindowBg] = ImVec4(body.x, body.y, body.z, wa);
        c[ImGuiCol_ChildBg] = ImVec4(body2.x, body2.y, body2.z, 0.75f);
        c[ImGuiCol_PopupBg] = ImVec4(body2.x, body2.y, body2.z, 0.99f);
        c[ImGuiCol_MenuBarBg] = mix(night, base, 0.30f);
        c[ImGuiCol_Border] = ImVec4(key.x, key.y, key.z, 0.40f);
        c[ImGuiCol_BorderShadow] = rgb(0, 0, 0, 0);
        c[ImGuiCol_FrameBg] = mix(night, base, 0.28f);
        c[ImGuiCol_FrameBgHovered] = mix(night, base, 0.40f);
        c[ImGuiCol_FrameBgActive] = mix(night, base, 0.50f);
        c[ImGuiCol_Button] = key;
        c[ImGuiCol_ButtonHovered] = mix(key, rgb(255, 255, 255), 0.16f);
        c[ImGuiCol_ButtonActive] = mix(key, rgb(0, 0, 0), 0.25f);
        c[ImGuiCol_Header] = ImVec4(key.x, key.y, key.z, 0.50f);
        c[ImGuiCol_HeaderHovered] = ImVec4(key.x, key.y, key.z, 0.78f);
        c[ImGuiCol_HeaderActive] = key;
        c[ImGuiCol_CheckMark] = v4(acc[2]);
        c[ImGuiCol_SliderGrab] = v4(acc[3]);
        c[ImGuiCol_SliderGrabActive] = mix(v4(acc[3]), rgb(255, 255, 255), 0.25f);
        c[ImGuiCol_Separator] = ImVec4(key.x, key.y, key.z, 0.45f);
        c[ImGuiCol_SeparatorHovered] = v4(acc[3]);
        c[ImGuiCol_SeparatorActive] = v4(acc[3]);
        c[ImGuiCol_ResizeGrip] = ImVec4(key.x, key.y, key.z, 0.35f);
        c[ImGuiCol_ResizeGripHovered] = v4(acc[3]);
        c[ImGuiCol_ResizeGripActive] = v4(acc[3]);
        c[ImGuiCol_Tab] = mix(night, base, 0.26f);
        c[ImGuiCol_TabHovered] = mix(key, rgb(255, 255, 255), 0.10f);
        c[ImGuiCol_TabSelected] = key;
        c[ImGuiCol_TabSelectedOverline] = v4(acc[3]);
        c[ImGuiCol_TabDimmed] = mix(night, base, 0.20f);
        c[ImGuiCol_TabDimmedSelected] = mix(night, base, 0.42f);
        c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(key.x, key.y, key.z, 0.6f);
        c[ImGuiCol_TitleBg] = mix(night, base, 0.22f);
        c[ImGuiCol_TitleBgActive] = mix(night, base, 0.45f);
        c[ImGuiCol_TitleBgCollapsed] = mix(night, base, 0.22f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(body.x, body.y, body.z, 0.5f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(key.x, key.y, key.z, 0.75f);
        c[ImGuiCol_ScrollbarGrabHovered] = key;
        c[ImGuiCol_ScrollbarGrabActive] = v4(acc[3]);
        c[ImGuiCol_PlotLines] = v4(acc[4]);
        c[ImGuiCol_PlotHistogram] = v4(acc[2]);
        c[ImGuiCol_PlotHistogramHovered] = v4(acc[3]);
        c[ImGuiCol_TableHeaderBg] = mix(night, base, 0.40f);
        c[ImGuiCol_TableBorderStrong] = ImVec4(key.x, key.y, key.z, 0.5f);
        c[ImGuiCol_TableBorderLight] = ImVec4(key.x, key.y, key.z, 0.25f);
        c[ImGuiCol_TableRowBg] = rgb(0, 0, 0, 0);
        c[ImGuiCol_TableRowBgAlt] = rgb(255, 255, 255, 0.04f);
        c[ImGuiCol_TextSelectedBg] = ImVec4(key.x, key.y, key.z, 0.6f);
        c[ImGuiCol_DragDropTarget] = v4(acc[3]);
        c[ImGuiCol_NavCursor] = v4(acc[3]);
        c[ImGuiCol_ModalWindowDimBg] = rgb(6, 4, 14, 0.65f);
        c[ImGuiCol_DockingPreview] = ImVec4(key.x, key.y, key.z, 0.55f);
        c[ImGuiCol_DockingEmptyBg] = mix(night, base, 0.12f);
    }
}

void apply_style(bool, const char* theme) { apply_theme(theme && !strcmp(theme, "gb") ? "gb" : "gbc", "uva"); }

// ------------------------------------------------------------------ drawing helpers
void title(const char* text, float mult) {
    push_pixel(mult);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImU32 sh = P.light ? IM_COL32(255, 255, 255, 140) : IM_COL32(0, 0, 0, 150);
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 2, p.y + 2), sh, text);
    ImGui::TextUnformatted(text);
    pop_font();
}

void stripe(ImDrawList* dl, float x0, float x1, float y, float h) {
    if (theme_gb) {
        // the two lines of the grey handheld's screen frame
        dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + h), accent(0));
        dl->AddRectFilled(ImVec2(x0, y + h + 2), ImVec2(x1, y + h * 2 + 2), accent(1));
    } else {
        const float w = (x1 - x0) / 5;
        for (int i = 0; i < 5; i++) dl->AddRectFilled(ImVec2(x0 + i * w, y), ImVec2(x0 + (i + 1) * w + (i < 4 ? 1 : 0), y + h), accent(i));
    }
}

void section(const char* text) {
    ImGui::Spacing();
    push_pixel(0.95f);
    ImGui::TextUnformatted(text);
    pop_font();
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    stripe(ImGui::GetWindowDrawList(), p.x, p.x + std::min(w, 260.f), p.y - 2, 2.f);
    ImGui::Dummy(ImVec2(1, theme_gb ? 6 : 3));
}

void led(ImDrawList* dl, ImVec2 c, float r, bool on) {
    if (on) dl->AddCircleFilled(c, r * 2.2f, (P.led & 0x00FFFFFF) | (40u << 24));
    dl->AddCircleFilled(c, r, on ? P.led : IM_COL32(90, 30, 34, 255));
    dl->AddCircleFilled(ImVec2(c.x - r * 0.35f, c.y - r * 0.35f), r * 0.35f, IM_COL32(255, 255, 255, on ? 150 : 40));
}

void lcd_frame(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding) {
    const ImU32 bezel = theme_gb ? IM_COL32(86, 88, 104, 255) : IM_COL32(32, 30, 44, 255);
    dl->AddRectFilled(a, b, bezel, rounding);
    dl->AddRectFilled(ImVec2(a.x + 6, a.y + 6), ImVec2(b.x - 6, b.y - 6), P.lcd[3], rounding * 0.4f);
}

bool key_button(const char* id, const char* label, float r, bool b_key) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float lh = ImGui::GetTextLineHeight();
    const bool pressed = ImGui::InvisibleButton("k", ImVec2(r * 2 + 4, r * 2 + lh + 6));
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + r + 2, p.y + r + 2);
    ImU32 base = theme_gb ? (b_key ? P.key_b : P.key_a) : (b_key ? accent(0) : accent(1));
    dl->AddCircleFilled(ImVec2(c.x + 1.5f, c.y + 2.5f), r, IM_COL32(0, 0, 0, 70));
    dl->AddCircleFilled(act ? ImVec2(c.x + 1, c.y + 1.5f) : c, r, hov ? u32(mix(v4(base), rgb(255, 255, 255), 0.15f)) : base);
    dl->AddCircle(c, r, IM_COL32(0, 0, 0, 60), 0, 1.5f);
    push_pixel(0.8f);
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(c.x - ts.x / 2, p.y + r * 2 + 6), u32(P.text), label);
    pop_font();
    ImGui::PopID();
    return pressed;
}

bool pill_button(const char* label, ImVec2 size) {
    push_pixel(0.75f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 100.f);
    ImGui::PushStyleColor(ImGuiCol_Button, theme_gb ? rgb(130, 128, 142) : v4(P.shell_dark));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme_gb ? rgb(150, 148, 162) : v4(P.shell));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme_gb ? rgb(110, 108, 122) : v4(P.shell_dark));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    pop_font();
    return r;
}

void title_bar(SDL_Window* w) {
#ifdef _WIN32
    SDL_SysWMinfo info; SDL_VERSION(&info.version);
    if (!w || !SDL_GetWindowWMInfo(w, &info)) return;
    static HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    typedef HRESULT(WINAPI * SetAttr)(HWND, DWORD, LPCVOID, DWORD);
    SetAttr set = (SetAttr)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!set) return;
    HWND h = info.info.win.window;
    BOOL dark = P.light ? FALSE : TRUE;
    if (set(h, 20, &dark, sizeof dark) != S_OK) set(h, 19, &dark, sizeof dark);   // 19 on older Windows 10
    // Windows 11: caption in the shell colour (COLORREF is 0x00BBGGRR)
    auto ref = [](ImU32 c) { return (DWORD)(c & 0x00FFFFFF); };   // (ImU32 is already 0xAABBGGRR)
    ImU32 cap = P.light ? P.shell : ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
    DWORD caption = ref(cap), text = ref(P.light ? P.print : IM_COL32(244, 240, 255, 255));
    set(h, 35, &caption, sizeof caption);
    set(h, 36, &text, sizeof text);
#else
    (void)w;
#endif
}

std::string key_name(SDL_Scancode sc) { const char* n = SDL_GetScancodeName(sc); return n && *n ? n : "?"; }
SDL_Scancode key_from_name(const std::string& s) { return SDL_GetScancodeFromName(s.c_str()); }

void badge(const char* text, ImU32 color) {
    push_pixel(0.7f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + ts.x + 16, p.y + ts.y + 10), color, theme_gb ? 3.f : 6.f);
    dl->AddText(ImVec2(p.x + 8, p.y + 5), IM_COL32(255, 255, 255, 255), text);
    ImGui::Dummy(ImVec2(ts.x + 16, ts.y + 10));
    pop_font();
}
}
