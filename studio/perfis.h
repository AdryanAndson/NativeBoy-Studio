// Game profiles: the Studio recognises a ROM by the title in its header and starts the project with
// the settings that suit that kind of game (widescreen, extra rows, frame, palette...).
#pragma once
#include "../common/settings.h"
#include <string>
#include <vector>
#include <cstdint>

struct GameProfile {
    std::string match;     // header title; a trailing * matches any title that starts with it
    std::string name;      // shown to the user
    std::string kind;      // id of the kind of game
    std::string extra;     // "key=value;key=value" on top of the kind's settings
    bool tested = false;   // checked by the authors with the real game
};
const char* profile_kind_name(const std::string& kind);
// profiles: built-in list + optional "perfis_jogos.txt" next to the program (TITLE|name|kind|key=value;...)
// then the list of every known Game Boy / Color game (assets/jogos_gb.txt), found by the ROM's CRC32
const GameProfile* find_profile(const std::string& header_title, const std::string& app_dir, const std::vector<uint8_t>* rom = nullptr);
// official name of a ROM from that list ("" if unknown) and how many games the list has
std::string known_game_name(const std::vector<uint8_t>& rom, const std::string& app_dir);
size_t known_game_count(const std::string& app_dir);
// everything the list knows about a ROM (empty fields = not in the source)
struct KnownGameInfo { std::string name, genre, year, publisher, developer, players, series; };
bool known_game_info(const std::vector<uint8_t>& rom, const std::string& app_dir, KnownGameInfo& out);
// writes nome_oficial, genero, ano, editora, desenvolvedora, jogadores, serie (only the known ones)
void store_game_info(const KnownGameInfo& g, Settings& ini);
// writes the profile's defaults into the project settings; returns a one-line description
std::string apply_profile(const GameProfile& p, Settings& ini);
