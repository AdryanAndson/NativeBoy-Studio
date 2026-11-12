// UI helpers shared by the Studio and exported games (SDL2 + Dear ImGui).
#pragma once
#include <SDL.h>
#include <string>
#include "imgui.h"

namespace ui {
// Loads a font with Latin-1 (Portuguese accents). Looks in assets/fonts then system fonts.
void load_fonts(const std::string& base_dir, float size);
// theme: "gbc" (translucent purple with colourful accents) or "classico" (dark with green)
void apply_style(bool light = false, const char* theme = "gbc");
extern bool theme_gbc;      // purple family (translucent purple, and the retro one)
extern bool theme_retro;    // "Portátil retrô": indigo / lavender with bevelled buttons, like the old handhelds
// five accent colours of the theme (red, purple, green, yellow, blue), used for stripes and dots
unsigned accent(int i);
extern float base_size;
inline void push_size(float mult) { ImGui::PushFont(nullptr, base_size * mult); }
inline void pop_size() { ImGui::PopFont(); }
inline ImTextureRef tex_ref(SDL_Texture* t) { return ImTextureRef((ImTextureID)(intptr_t)t); }
// key-name helpers for remapping
std::string key_name(SDL_Scancode sc);
SDL_Scancode key_from_name(const std::string& s);
// draw a badge "EMULAÇÃO" / "NATIVO"
void badge(const char* text, ImU32 color);
}
