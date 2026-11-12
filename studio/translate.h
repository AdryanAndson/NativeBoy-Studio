// Translation tools: character table (.tbl), relative search, text dump and reinsertion.
#pragma once
#include "../core/gb.h"
#include <map>
#include <string>
#include <vector>

struct TextTable {
    std::map<gb::u8, std::string> chars;     // byte -> text
    std::vector<gb::u8> ends;                // string terminators
    bool load_text(const std::string& tbl);  // "80=A", "/50=<FIM>" or "/50"
    std::string to_text() const;
    bool empty() const { return chars.empty(); }
    bool is_end(gb::u8 b) const;
    std::string decode(const gb::u8* p, size_t n) const;           // unknown bytes as {XX}
    bool encode(const std::string& s, std::vector<gb::u8>& out, std::string& err) const;
};

struct RelHit { size_t off; int base; };    // base = byte value of the first letter of the alphabet ('A' or 'a')
// Finds where a word is stored with letters in alphabetical order, whatever their byte values are
std::vector<RelHit> relative_search(const std::vector<gb::u8>& rom, const std::string& word, size_t max_hits = 200);
// Builds a table from the positions found: upper/lower-case letters, digits and space (-1 = unknown)
TextTable table_from_bases(int upper, int lower, int digit0, int space, int end);
// Guesses the terminator: the byte that most often follows runs of table characters
int guess_terminator(const std::vector<gb::u8>& rom, const TextTable& t);
// Guesses the space: the byte most often found between two letters
int guess_space(const std::vector<gb::u8>& rom, const TextTable& t);

struct TextEntry { size_t off; size_t len; std::string text; };
std::vector<TextEntry> dump_texts(const std::vector<gb::u8>& rom, const TextTable& t, int min_len = 4);
std::string entries_to_file(const std::vector<TextEntry>& e);
// Applies an edited text file to a ROM copy; errors lists lines that did not fit
// relocate: a text longer than its space is written to free space of the same bank and the pointers
// to it are updated (when 1 to 4 pointers are found); notes tells where each one went
int insert_texts(std::vector<gb::u8>& rom, const TextTable& t, const std::string& file, std::vector<std::string>& errors,
                 bool relocate = false, std::vector<std::string>* notes = nullptr);
// Finds the character table without a word from the player: common game words (START, PRESS, GAME,
// THE, YOU...) are searched in every possible alphabet position. Best guesses first.
struct TableGuess { int upper = -1, lower = -1, space = -1, end = -1; int score = 0, words = 0; std::string sample; };
std::vector<TableGuess> auto_detect_tables(const std::vector<gb::u8>& rom, size_t max = 3);
TextTable table_from_guess(const TableGuess& g);
// pointers (little-endian GB addresses) to a text at ROM offset off: in its bank and in bank 0
std::vector<size_t> find_pointers(const std::vector<gb::u8>& rom, size_t off);
std::vector<gb::u8> make_ips(const std::vector<gb::u8>& orig, const std::vector<gb::u8>& mod);
