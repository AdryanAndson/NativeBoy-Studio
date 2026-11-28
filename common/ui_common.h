// UI helpers shared by the Studio and exported games (SDL2 + Dear ImGui).
// Two classic looks:
//   "gb"  - the grey handheld: light grey shell, the four greens of its screen, navy print, magenta keys
//   "gbc" - the colour handheld: a dark body tinted by the shell colour (grape, berry, teal, kiwi,
//           dandelion or the see-through "atomic" one) with the five bright accents
#pragma once
#include <SDL.h>
#include <string>
#include "imgui.h"

namespace ui {
// Loads the text font (Latin-1: Portuguese accents) plus the pixel fonts used for titles.
void load_fonts(const std::string& base_dir, float size);
extern ImFont* font_text;    // body text (DejaVu Sans / system font)
extern ImFont* font_pixel;   // "Press Start 2P": short titles, brand, labels like START / SELECT
extern ImFont* font_head;    // "Pixelify Sans": larger headings that need to stay readable
extern float base_size;

// ------------------------------------------------------------------ themes
// theme: "gb" or "gbc" (old names: "retro" and "classico" become "gbc"); shell: shell colour id (gbc)
void apply_theme(const std::string& theme, const std::string& shell = "uva");
// legacy entry point (light ignored)
void apply_style(bool light = false, const char* theme = "gbc");
extern bool theme_gb;       // grey handheld (light)
extern bool theme_gbc;      // colour handheld (dark)
extern bool theme_retro;    // (old theme, always false now)

// shell colours of the colour theme
struct Shell { const char* id; const char* name; ImU32 color; };
const Shell* shells(int* count);
// theme names for combo boxes
extern const char* theme_ids[2];
extern const char* theme_names[2];

// colours of the current theme (for custom drawing)
struct Pal {
    bool light = false;
    ImVec4 ok, warn, err, info, dim, text;      // text colours that read well on the window background
    ImU32 shell, shell_dark, shell_light;         // body of the handheld
    ImU32 lcd[4];                                  // the screen, darkest to lightest
    ImU32 card, card_hover, card_edge, card_text, card_dim;   // big panels / cards
    ImU32 key_a, key_b;                           // round keys (A/B)
    ImU32 print;                                  // "printed" text on the shell
    ImU32 led;                                    // power light
};
const Pal& pal();
// five accent colours of the theme, used for stripes and dots
unsigned accent(int i);

// ------------------------------------------------------------------ drawing helpers
inline void push_size(float mult) { ImGui::PushFont(nullptr, base_size * mult); }
inline void pop_size() { ImGui::PopFont(); }
// pixel font at mult x the text size
void push_pixel(float mult = 1.0f);
void push_head(float mult = 1.0f);
inline void pop_font() { ImGui::PopFont(); }
// big pixel title (with a small shadow) at the cursor
void title(const char* text, float mult = 1.6f);
// section heading: pixel text and the theme stripe under it
void section(const char* text);
// the theme stripe (GB: magenta + navy lines; GBC: five accent blocks) across [x0,x1] at y
void stripe(ImDrawList* dl, float x0, float x1, float y, float h = 3.f);
// power light
void led(ImDrawList* dl, ImVec2 center, float r, bool on = true);
// a small "screen" (inset bezel + LCD) rectangle; returns the inner rectangle
void lcd_frame(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding = 8.f);
// button that looks like the round A/B keys (label under it)
bool key_button(const char* id, const char* label, float radius, bool b_key = false);
// pill button like START / SELECT
bool pill_button(const char* label, ImVec2 size = ImVec2(0, 0));

// Windows 10/11: title bar in the theme colours (dark or light, and the shell colour on Windows 11)
void title_bar(SDL_Window* w);
inline ImTextureRef tex_ref(SDL_Texture* t) { return ImTextureRef((ImTextureID)(intptr_t)t); }
// key-name helpers for remapping
std::string key_name(SDL_Scancode sc);
SDL_Scancode key_from_name(const std::string& s);
// draw a badge "EMULAÇÃO" / "NATIVO"
void badge(const char* text, ImU32 color);
}
