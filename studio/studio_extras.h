// Studio panels: translation, soundtrack, icon/opening screen, debugger.
#pragma once
#include <functional>
#include <string>

struct Project;
struct GamePlayer;
struct SDL_Renderer;

struct StudioMsg {
    std::function<void(const std::string&)> notify;
    std::function<void(const std::string&, const std::string&)> error;
    std::function<void()> test_play;       // starts the emulated game
    SDL_Renderer* ren = nullptr;
};

void draw_translation_tab(Project& p, const StudioMsg& msg);
void draw_music_tab(Project& p, const StudioMsg& msg);
void draw_icon_tab(Project& p, const StudioMsg& msg);
void draw_achievements_editor(Project& p, const StudioMsg& msg);
void draw_debugger(GamePlayer& pl, bool* open);
// Game library (exported games in "Meus Jogos") and launchers
void draw_library(const std::string& games_root, const StudioMsg& msg);
bool make_game_shortcuts(const std::string& game_dir, bool desktop, bool start_menu, std::string& err);

// Sprite editor: runs the game inside the tab; click a character (or a piece of scenery) and redraw it
void draw_sprite_editor(Project& p, const StudioMsg& msg);
// Mod assistant: ready-made recipes (cheats, game speed) and shortcuts to the other tools.
// goto_tab receives the name of the tab to open when the user picks a recipe that lives elsewhere.
void draw_mod_assistant(Project& p, const StudioMsg& msg, std::string& goto_tab);
