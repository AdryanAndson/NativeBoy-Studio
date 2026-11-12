#include "ui_common.h"
#include "platform.h"

namespace ui {
float base_size = 18.0f;

void load_fonts(const std::string& base, float size) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x21FF, 0x25A0, 0x25FF, 0x2600, 0x26FF, 0};
    std::string cands[] = {
        plat::join(base, "assets/fonts/DejaVuSans.ttf"),
        plat::join(base, "fonts/DejaVuSans.ttf"),
#ifdef _WIN32
        "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
#endif
    };
    std::string chosen;
    for (auto& c : cands) if (plat::file_exists(c)) { chosen = c; break; }
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    if (!chosen.empty()) {
        std::vector<uint8_t> data;
        plat::read_file(chosen, data);
        // ImGui keeps the pointer: give it its own copy
        auto add = [&](float sz) {
            void* mem = IM_ALLOC(data.size());
            memcpy(mem, data.data(), data.size());
            return io.Fonts->AddFontFromMemoryTTF(mem, (int)data.size(), sz, &cfg, ranges);
        };
        add(size);
    } else {
        io.Fonts->AddFontDefault();
    }
    base_size = size;
    ImGui::GetStyle().FontSizeBase = size;
}

bool theme_gbc = true;
bool theme_retro = false;

unsigned accent(int i) {
    static const unsigned c[5] = {IM_COL32(232, 62, 98, 255), IM_COL32(150, 96, 232, 255), IM_COL32(88, 200, 96, 255), IM_COL32(246, 200, 54, 255), IM_COL32(56, 168, 232, 255)};
    return c[((i % 5) + 5) % 5];
}

void apply_style(bool light, const char* theme) {
    ImGuiStyle& s = ImGui::GetStyle();
    theme_retro = !light && theme && std::string(theme) == "retro";
    theme_gbc = !light && theme && (std::string(theme) == "gbc" || theme_retro);
    if (light) ImGui::StyleColorsLight(); else ImGui::StyleColorsDark();
    s.WindowRounding = 8; s.FrameRounding = 6; s.GrabRounding = 6; s.PopupRounding = 6; s.TabRounding = 6; s.ChildRounding = 6;
    s.FramePadding = ImVec2(10, 6); s.ItemSpacing = ImVec2(10, 8); s.WindowPadding = ImVec2(14, 12);
    ImVec4* c = s.Colors;
    if (theme_retro) {
        // indigo body, lavender buttons with a bevel (light edge, dark shadow), LCD-green marks, yellow handles
        s.WindowRounding = 6; s.FrameRounding = 4; s.GrabRounding = 3; s.PopupRounding = 4; s.TabRounding = 4; s.ChildRounding = 6;
        s.ScrollbarRounding = 3; s.FramePadding = ImVec2(12, 6);
        s.WindowBorderSize = 1; s.ChildBorderSize = 1; s.FrameBorderSize = 1; s.TabBorderSize = 1; s.PopupBorderSize = 1;
        c[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 1.00f, 1.0f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.68f, 0.68f, 0.86f, 1.0f);
        c[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.37f, 0.93f);
        c[ImGuiCol_ChildBg] = ImVec4(0.19f, 0.19f, 0.45f, 0.88f);
        c[ImGuiCol_PopupBg] = ImVec4(0.17f, 0.17f, 0.41f, 0.98f);
        c[ImGuiCol_Border] = ImVec4(0.06f, 0.06f, 0.20f, 0.95f);
        c[ImGuiCol_BorderShadow] = ImVec4(0.62f, 0.62f, 0.95f, 0.22f);
        c[ImGuiCol_Button] = ImVec4(0.44f, 0.42f, 0.78f, 1.0f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.55f, 0.53f, 0.89f, 1.0f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.31f, 0.29f, 0.60f, 1.0f);
        c[ImGuiCol_Header] = ImVec4(0.44f, 0.42f, 0.78f, 0.60f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.55f, 0.53f, 0.89f, 0.85f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.55f, 0.53f, 0.89f, 1.0f);
        c[ImGuiCol_FrameBg] = ImVec4(0.09f, 0.09f, 0.25f, 1.0f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.13f, 0.13f, 0.33f, 1.0f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.17f, 0.17f, 0.41f, 1.0f);
        c[ImGuiCol_CheckMark] = ImVec4(0.62f, 0.80f, 0.10f, 1.0f);
        c[ImGuiCol_SliderGrab] = ImVec4(0.96f, 0.80f, 0.25f, 1.0f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.88f, 0.40f, 1.0f);
        c[ImGuiCol_Tab] = ImVec4(0.22f, 0.22f, 0.50f, 1.0f);
        c[ImGuiCol_TabHovered] = ImVec4(0.55f, 0.53f, 0.89f, 1.0f);
        c[ImGuiCol_TabSelected] = ImVec4(0.44f, 0.42f, 0.78f, 1.0f);
        c[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.12f, 0.30f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.30f, 0.29f, 0.62f, 1.0f);
        c[ImGuiCol_Separator] = ImVec4(0.40f, 0.40f, 0.72f, 0.65f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.09f, 0.09f, 0.25f, 0.7f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.44f, 0.42f, 0.78f, 0.9f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.55f, 0.53f, 0.89f, 1.0f);
        c[ImGuiCol_PlotHistogram] = ImVec4(0.62f, 0.80f, 0.10f, 1.0f);
        c[ImGuiCol_NavCursor] = ImVec4(0.96f, 0.80f, 0.25f, 1.0f);
        c[ImGuiCol_TableHeaderBg] = ImVec4(0.30f, 0.29f, 0.62f, 1.0f);
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.04f, 0.04f, 0.12f, 0.65f);
    } else if (theme_gbc) {
        // translucent purple shell, rounded "pill" controls, teal marks and yellow highlights
        s.WindowRounding = 14; s.FrameRounding = 12; s.GrabRounding = 12; s.PopupRounding = 12; s.TabRounding = 10; s.ChildRounding = 12;
        s.ScrollbarRounding = 12; s.FramePadding = ImVec2(13, 7); s.WindowBorderSize = 0; s.ChildBorderSize = 0; s.FrameBorderSize = 0;
        c[ImGuiCol_Text] = ImVec4(0.96f, 0.94f, 1.00f, 1.0f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.66f, 0.60f, 0.80f, 1.0f);
        c[ImGuiCol_WindowBg] = ImVec4(0.115f, 0.085f, 0.215f, 0.90f);
        c[ImGuiCol_ChildBg] = ImVec4(0.17f, 0.125f, 0.31f, 0.78f);
        c[ImGuiCol_PopupBg] = ImVec4(0.15f, 0.11f, 0.28f, 0.98f);
        c[ImGuiCol_Border] = ImVec4(0.55f, 0.42f, 0.90f, 0.35f);
        c[ImGuiCol_Button] = ImVec4(0.43f, 0.30f, 0.80f, 1.0f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.54f, 0.40f, 0.93f, 1.0f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.33f, 0.22f, 0.66f, 1.0f);
        c[ImGuiCol_Header] = ImVec4(0.43f, 0.30f, 0.80f, 0.55f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.54f, 0.40f, 0.93f, 0.80f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.54f, 0.40f, 0.93f, 1.0f);
        c[ImGuiCol_FrameBg] = ImVec4(0.235f, 0.175f, 0.42f, 1.0f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.31f, 0.235f, 0.54f, 1.0f);
        c[ImGuiCol_FrameBgActive] = ImVec4(0.36f, 0.27f, 0.62f, 1.0f);
        c[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.88f, 0.78f, 1.0f);
        c[ImGuiCol_SliderGrab] = ImVec4(0.96f, 0.78f, 0.21f, 1.0f);
        c[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.86f, 0.36f, 1.0f);
        c[ImGuiCol_Tab] = ImVec4(0.21f, 0.155f, 0.38f, 1.0f);
        c[ImGuiCol_TabHovered] = ImVec4(0.54f, 0.40f, 0.93f, 1.0f);
        c[ImGuiCol_TabSelected] = ImVec4(0.43f, 0.30f, 0.80f, 1.0f);
        c[ImGuiCol_TitleBg] = ImVec4(0.13f, 0.095f, 0.24f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.26f, 0.18f, 0.50f, 1.0f);
        c[ImGuiCol_Separator] = ImVec4(0.55f, 0.42f, 0.90f, 0.40f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.075f, 0.19f, 0.6f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(0.43f, 0.30f, 0.80f, 0.8f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.54f, 0.40f, 0.93f, 1.0f);
        c[ImGuiCol_PlotHistogram] = ImVec4(0.30f, 0.88f, 0.78f, 1.0f);
        c[ImGuiCol_NavCursor] = ImVec4(0.96f, 0.78f, 0.21f, 1.0f);
        c[ImGuiCol_TableHeaderBg] = ImVec4(0.26f, 0.18f, 0.50f, 1.0f);
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.05f, 0.03f, 0.12f, 0.65f);
    } else if (!light) {
        c[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.10f, 0.12f, 0.97f);
        c[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.16f, 1.0f);
        c[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.12f, 0.15f, 0.98f);
        c[ImGuiCol_Button] = ImVec4(0.20f, 0.42f, 0.36f, 1.0f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.55f, 0.46f, 1.0f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.17f, 0.35f, 0.30f, 1.0f);
        c[ImGuiCol_Header] = ImVec4(0.20f, 0.42f, 0.36f, 0.6f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.55f, 0.46f, 0.8f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.55f, 0.46f, 1.0f);
        c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.18f, 0.22f, 1.0f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.25f, 0.30f, 1.0f);
        c[ImGuiCol_CheckMark] = ImVec4(0.45f, 0.85f, 0.70f, 1.0f);
        c[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.85f, 0.70f, 1.0f);
        c[ImGuiCol_Tab] = ImVec4(0.14f, 0.16f, 0.20f, 1.0f);
        c[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.55f, 0.46f, 1.0f);
        c[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.25f, 0.22f, 1.0f);
        c[ImGuiCol_PlotHistogram] = ImVec4(0.45f, 0.85f, 0.70f, 1.0f);
    }
}

std::string key_name(SDL_Scancode sc) { const char* n = SDL_GetScancodeName(sc); return n && *n ? n : "?"; }
SDL_Scancode key_from_name(const std::string& s) { return SDL_GetScancodeFromName(s.c_str()); }

void badge(const char* text, ImU32 color) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + ts.x + 16, p.y + ts.y + 8), color, 6);
    dl->AddText(ImVec2(p.x + 8, p.y + 4), IM_COL32(255, 255, 255, 255), text);
    ImGui::Dummy(ImVec2(ts.x + 16, ts.y + 8));
}
}
