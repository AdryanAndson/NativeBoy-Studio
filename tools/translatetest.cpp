// Translation tools test: relative search -> table -> dump -> edit -> reinsert -> IPS.
// usage: translatetest in.gb out.gb [out_relocated.gb]
#include "../studio/translate.h"
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace gb;
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<u8> rom((std::istreambuf_iterator<char>(f)), {});
    auto hits = relative_search(rom, "WORLD");
    if (hits.empty()) { printf("no hits\n"); return 1; }
    TextTable t = table_from_bases(hits[0].base, -1, -1, -1, -1);
    int sp = guess_space(rom, t); if (sp >= 0) t.chars[(u8)sp] = " ";
    int te = guess_terminator(rom, t); if (te >= 0) t.ends.push_back((u8)te);
    TextTable t2; t2.load_text(t.to_text());     // round trip through the .tbl format
    auto e = dump_texts(rom, t2, 4);
    std::string file = entries_to_file(e);
    printf("base=%02X space=%02X end=%02X texts=%zu\n%s", hits[0].base, sp, te, e.size(), file.c_str());
    // edit: shorter text fits, longer one must be rejected
    std::string edited;
    for (auto& ent : e) {
        std::string line = entries_to_file({ent});
        line = line.substr(line.find('@'));
        if (ent.text == "HELLO WORLD") line = line.substr(0, line.find('|') + 1) + "OLA MUNDO\n";
        if (ent.text == "PRESS START TO PLAY") line = line.substr(0, line.find('|') + 1) + "APERTE START PARA JOGAR AGORA\n";
        edited += line;
    }
    std::vector<u8> mod = rom;
    std::vector<std::string> errs;
    int n = insert_texts(mod, t2, edited, errs);
    for (auto& s : errs) printf("erro: %s\n", s.c_str());
    auto ips = make_ips(rom, mod);
    printf("inserted=%d errors=%zu ips=%zu bytes\n", n, errs.size(), ips.size());
    std::ofstream o(argv[2], std::ios::binary); o.write((const char*)mod.data(), mod.size());
    bool fit_ok = n == 1 && errs.size() == 1;
    // automatic table: no word given
    auto guesses = auto_detect_tables(rom);
    bool auto_ok = !guesses.empty() && guesses[0].upper == hits[0].base && guesses[0].space == sp && guesses[0].end == te;
    printf("automatica: %s base=%02X espaco=%02X fim=%02X palavras=%d amostra=%s\n", auto_ok ? "ok" : "FALHOU", guesses.empty() ? -1 : guesses[0].upper,
           guesses.empty() ? -1 : guesses[0].space, guesses.empty() ? -1 : guesses[0].end, guesses.empty() ? 0 : guesses[0].words, guesses.empty() ? "" : guesses[0].sample.c_str());
    // longer text: moved to free space, pointer updated
    std::vector<u8> mod2 = rom;
    std::vector<std::string> errs2, notes;
    TextTable t3 = t2;                                // the player completes the table with the end of text ($50 in this ROM)
    if (t3.ends.empty()) t3.ends.push_back(0x50);
    int n2 = insert_texts(mod2, t3, edited, errs2, true, &notes);
    for (auto& s : notes) printf("nota: %s\n", s.c_str());
    for (auto& s : errs2) printf("erro2: %s\n", s.c_str());
    printf("com_realocacao: inseridos=%d erros=%zu\n", n2, errs2.size());
    if (argc > 3) { std::ofstream o2(argv[3], std::ios::binary); o2.write((const char*)mod2.data(), mod2.size()); }
    return (fit_ok && auto_ok && n2 == 2 && errs2.empty()) ? 0 : 1;
}
