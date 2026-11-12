#include "translate.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

using namespace gb;

bool TextTable::load_text(const std::string& tbl) {
    chars.clear(); ends.clear();
    std::istringstream in(tbl);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        bool end = line[0] == '/';
        if (end) line = line.substr(1);
        if (line.size() < 2 || !isxdigit((unsigned char)line[0]) || !isxdigit((unsigned char)line[1])) continue;
        u8 b = (u8)strtol(line.substr(0, 2).c_str(), nullptr, 16);
        std::string v = line.size() > 3 && line[2] == '=' ? line.substr(3) : "";
        if (end) { ends.push_back(b); if (!v.empty()) chars[b] = v; }
        else if (line.size() >= 3 && line[2] == '=') chars[b] = v.empty() ? " " : v;
    }
    return !chars.empty();
}

std::string TextTable::to_text() const {
    std::string s = "# Tabela de caracteres (formato .tbl): BYTE=texto. Linhas com / são o fim de um texto.\n";
    char b[16];
    for (u8 e : ends) { auto it = chars.find(e); snprintf(b, sizeof b, "/%02X", e); s += b; if (it != chars.end()) s += "=" + it->second; s += "\n"; }
    for (auto& kv : chars) {
        if (std::find(ends.begin(), ends.end(), kv.first) != ends.end()) continue;
        snprintf(b, sizeof b, "%02X=", kv.first); s += b + kv.second + "\n";
    }
    return s;
}

bool TextTable::is_end(u8 b) const { return std::find(ends.begin(), ends.end(), b) != ends.end(); }

std::string TextTable::decode(const u8* p, size_t n) const {
    std::string s;
    char b[8];
    for (size_t i = 0; i < n; i++) {
        auto it = chars.find(p[i]);
        if (it != chars.end() && !is_end(p[i])) s += it->second;
        else { snprintf(b, sizeof b, "{%02X}", p[i]); s += b; }
    }
    return s;
}

bool TextTable::encode(const std::string& s, std::vector<u8>& out, std::string& err) const {
    out.clear();
    // longest text first, so multi-letter entries ("é", "<PLAYER>") win over single letters
    std::vector<std::pair<std::string, u8>> rev;
    for (auto& kv : chars) if (!is_end(kv.first) && !kv.second.empty()) rev.push_back({kv.second, kv.first});
    std::stable_sort(rev.begin(), rev.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '{' && i + 3 < s.size() && s[i + 3] == '}' && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            out.push_back((u8)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)); i += 4; continue;
        }
        bool found = false;
        for (auto& r : rev)
            if (s.compare(i, r.first.size(), r.first) == 0) { out.push_back(r.second); i += r.first.size(); found = true; break; }
        if (!found) {
            size_t l = 1; unsigned char c = (unsigned char)s[i];
            if (c >= 0xC0) l = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;   // whole UTF-8 character in the message
            err = "o caractere \"" + s.substr(i, l) + "\" não existe na tabela do jogo";
            return false;
        }
    }
    return true;
}

std::vector<RelHit> relative_search(const std::vector<u8>& rom, const std::string& word, size_t max_hits) {
    std::vector<RelHit> hits;
    std::vector<int> rel;   // letter offsets, -1000 = ignore (not a letter)
    // the letters of the case that appears most are used ("Pokemon" searches "okemon"), the rest is ignored
    int up = 0, lo = 0;
    for (char c : word) { if (isupper((unsigned char)c)) up++; else if (islower((unsigned char)c)) lo++; }
    if (up + lo == 0) return hits;
    char first = up >= lo ? 'A' : 'a';
    for (char c : word) {
        if (isupper((unsigned char)c) && first == 'A') rel.push_back(c - 'A');
        else if (islower((unsigned char)c) && first == 'a') rel.push_back(c - 'a');
        else rel.push_back(-1000);
    }
    int k0 = -1;
    for (size_t k = 0; k < rel.size(); k++) if (rel[k] != -1000) { k0 = (int)k; break; }
    if (k0 < 0 || rel.size() < 3) return hits;
    for (size_t i = 0; i + rel.size() <= rom.size() && hits.size() < max_hits; i++) {
        int base = (int)rom[i + k0] - rel[k0];
        if (base < 0 || base + 25 > 255) continue;
        bool ok = true;
        for (size_t k = 0; k < rel.size() && ok; k++)
            if (rel[k] != -1000 && (int)rom[i + k] != base + rel[k]) ok = false;
        if (ok) hits.push_back({i, base});
    }
    return hits;
}

TextTable table_from_bases(int upper, int lower, int digit0, int space, int end) {
    TextTable t;
    if (upper >= 0) for (int i = 0; i < 26 && upper + i < 256; i++) t.chars[(u8)(upper + i)] = std::string(1, (char)('A' + i));
    if (lower >= 0) for (int i = 0; i < 26 && lower + i < 256; i++) t.chars[(u8)(lower + i)] = std::string(1, (char)('a' + i));
    if (digit0 >= 0) for (int i = 0; i < 10 && digit0 + i < 256; i++) t.chars[(u8)(digit0 + i)] = std::string(1, (char)('0' + i));
    if (space >= 0) t.chars[(u8)space] = " ";
    if (end >= 0) t.ends.push_back((u8)end);
    return t;
}

int guess_terminator(const std::vector<u8>& rom, const TextTable& t) {
    int count[256] = {0}, runs = 0;
    size_t run = 0;
    for (size_t i = 0; i < rom.size(); i++) {
        if (t.chars.count(rom[i])) { run++; continue; }
        if (run >= 5) { count[rom[i]]++; runs++; }
        run = 0;
    }
    int best = -1, bc = 0;
    for (int b = 0; b < 256; b++) if (count[b] > bc) { bc = count[b]; best = b; }
    // three texts agreeing, or a small game where every text found ends with the same byte
    return bc >= 3 || (bc >= 2 && bc == runs) ? best : -1;
}

int guess_space(const std::vector<u8>& rom, const TextTable& t) {
    int count[256] = {0};
    auto letter = [&](u8 b) { auto it = t.chars.find(b); return it != t.chars.end() && it->second.size() == 1 && isalpha((unsigned char)it->second[0]); };
    for (size_t i = 2; i + 2 < rom.size(); i++)
        if (!t.chars.count(rom[i]) && letter(rom[i - 1]) && letter(rom[i - 2]) && letter(rom[i + 1]) && letter(rom[i + 2])) count[rom[i]]++;
    int best = -1, bc = 0;
    for (int b = 0; b < 256; b++) if (count[b] > bc && !t.is_end((u8)b)) { bc = count[b]; best = b; }
    return bc >= 3 ? best : -1;
}

std::vector<TextEntry> dump_texts(const std::vector<u8>& rom, const TextTable& t, int min_len) {
    std::vector<TextEntry> out;
    size_t i = 0, n = rom.size();
    while (i < n) {
        size_t j = i;
        int letters = 0;
        while (j < n && t.chars.count(rom[j]) && !t.is_end(rom[j])) {
            const std::string& v = t.chars.at(rom[j]);
            if (v.size() == 1 && isalpha((unsigned char)v[0])) letters++;
            j++;
        }
        size_t len = j - i;
        bool terminated = t.ends.empty() || (j < n && t.is_end(rom[j]));
        if ((int)len >= min_len && terminated && letters * 2 >= (int)len && (j / 0x4000) == (i / 0x4000)) {
            out.push_back({i, len, t.decode(&rom[i], len)});
            i = j + 1;
        } else i = len ? j : i + 1;
    }
    return out;
}

std::string entries_to_file(const std::vector<TextEntry>& e) {
    std::string s = "# NativeBoy Studio - textos extraídos da ROM.\n"
                    "# Edite só o que vem depois de \"|\". O número entre [] é o espaço disponível em bytes:\n"
                    "# um texto maior não cabe (fica no original e aparece no relatório). {XX} = byte especial.\n";
    char b[48];
    for (auto& t : e) {
        snprintf(b, sizeof b, "@%02X:%04X[%zu]|", (unsigned)(t.off / 0x4000), (unsigned)(t.off < 0x4000 ? t.off : 0x4000 + t.off % 0x4000), t.len);
        s += b + t.text + "\n";
    }
    return s;
}

static size_t gb_addr(size_t off) { return off < 0x4000 ? off : 0x4000 + off % 0x4000; }

std::vector<size_t> find_pointers(const std::vector<u8>& rom, size_t off) {
    std::vector<size_t> out;
    const size_t a = gb_addr(off), bank = off / 0x4000;
    const u8 lo = (u8)(a & 0xFF), hi = (u8)(a >> 8);
    auto scan = [&](size_t from, size_t to) {
        for (size_t p = from; p + 1 < to && p + 1 < rom.size(); p++)
            if (rom[p] == lo && rom[p + 1] == hi && !(p + 1 >= off && p < off + 1)) out.push_back(p);
    };
    scan(0x150, 0x4000);                                    // bank 0 (after the header)
    if (bank > 0) scan(bank * 0x4000, bank * 0x4000 + 0x4000);
    return out;
}

// free space in a bank: the longest run of $FF or $00 that is at least `need` long, preferring the
// end of the bank (unused ROM); taken ranges are remembered so two texts never share space
static long take_free(const std::vector<u8>& rom, size_t bank, size_t need, std::vector<std::pair<size_t, size_t>>& taken) {
    const size_t b0 = bank == 0 ? 0x150 : bank * 0x4000, b1 = std::min(rom.size(), bank * 0x4000 + 0x4000);
    auto used = [&](size_t p) { for (auto& t : taken) if (p >= t.first && p < t.second) return true; return false; };
    long best = -1; size_t best_len = 0;
    for (size_t p = b0; p < b1;) {
        const u8 v = rom[p];
        if ((v != 0xFF && v != 0x00) || used(p)) { p++; continue; }
        size_t q = p;
        while (q < b1 && rom[q] == v && !used(q)) q++;
        const size_t len = q - p;
        // keep 2 bytes of the run before the text (the end of the data in front of it may be such a byte)
        if (len >= need + 2 && len >= 16 && (q == b1 || len > best_len)) { best = (long)(p + 2); best_len = len; if (q == b1) break; }
        p = q;
    }
    if (best >= 0) taken.push_back({(size_t)best, (size_t)best + need});
    return best;
}

int insert_texts(std::vector<u8>& rom, const TextTable& t, const std::string& file, std::vector<std::string>& errors, bool relocate, std::vector<std::string>* notes) {
    std::vector<std::pair<size_t, size_t>> taken;
    std::istringstream in(file);
    std::string line;
    int n = 0, ln = 0;
    while (std::getline(in, line)) {
        ln++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] != '@') continue;
        unsigned bk, ad; size_t cap;
        size_t bar = line.find('|');
        if (bar == std::string::npos || sscanf(line.c_str(), "@%x:%x[%zu]", &bk, &ad, &cap) != 3) { errors.push_back("linha " + std::to_string(ln) + ": formato inválido"); continue; }
        size_t off = ad < 0x4000 ? ad : (size_t)bk * 0x4000 + (ad - 0x4000);
        char lab[48]; snprintf(lab, sizeof lab, "texto em %02X:%04X", bk, ad);     // as shown in the list
        const std::string at = lab;
        if (off + cap > rom.size()) { errors.push_back(at + ": endereço fora da ROM"); continue; }
        std::string txt = line.substr(bar + 1), err;
        std::vector<u8> enc;
        if (!t.encode(txt, enc, err)) { errors.push_back(at + ": " + err); continue; }
        if (enc.size() > cap) {
            const std::string where = at + ": " + std::to_string(enc.size()) + " bytes, mas só cabem " + std::to_string(cap);
            if (!relocate) { errors.push_back(where); continue; }
            if (t.ends.empty()) { errors.push_back(where + " (para mover o texto, a tabela precisa marcar o fim de texto)"); continue; }
            auto ptrs = find_pointers(rom, off);
            if (ptrs.empty()) { errors.push_back(where + " e nenhum ponteiro para ele foi achado (o jogo o lê de outro jeito)"); continue; }
            if (ptrs.size() > 4) { errors.push_back(where + " e há " + std::to_string(ptrs.size()) + " possíveis ponteiros (muitos para ter certeza)"); continue; }
            long dst = take_free(rom, off / 0x4000, enc.size() + 1, taken);
            if (dst < 0) { errors.push_back(where + " e não há espaço livre no mesmo banco"); continue; }
            for (size_t k = 0; k < enc.size(); k++) rom[(size_t)dst + k] = enc[k];
            rom[(size_t)dst + enc.size()] = t.ends[0];
            const size_t na = gb_addr((size_t)dst);
            for (size_t p : ptrs) { rom[p] = (u8)(na & 0xFF); rom[p + 1] = (u8)(na >> 8); }
            if (notes) {
                char b[160];
                snprintf(b, sizeof b, "%s: maior que o original, movido para %02X:%04X (%zu ponteiro%s atualizado%s)", at.c_str(), (unsigned)(dst / 0x4000), (unsigned)na,
                         ptrs.size(), ptrs.size() > 1 ? "s" : "", ptrs.size() > 1 ? "s" : "");
                notes->push_back(b);
            }
            n++;
            continue;
        }
        if (std::equal(enc.begin(), enc.end(), rom.begin() + off) && enc.size() == cap) continue;
        // pad: an earlier terminator keeps the rest unused; without terminators, spaces
        int pad = -1;
        if (!t.ends.empty()) pad = t.ends[0];
        else for (auto& kv : t.chars) if (kv.second == " ") { pad = kv.first; break; }
        if (enc.size() < cap && pad < 0) { errors.push_back(at + ": texto menor e a tabela não tem espaço nem fim de texto"); continue; }
        for (size_t k = 0; k < cap; k++) rom[off + k] = k < enc.size() ? enc[k] : (u8)pad;
        n++;
    }
    return n;
}

std::vector<u8> make_ips(const std::vector<u8>& a, const std::vector<u8>& b) {
    std::vector<u8> o = {'P', 'A', 'T', 'C', 'H'};
    size_t n = std::min(a.size(), b.size()), i = 0;
    while (i < n) {
        if (a[i] == b[i]) { i++; continue; }
        size_t j = i;
        while (j < n && j - i < 0xFFFF && (a[j] != b[j] || (j + 1 < n && a[j + 1] != b[j + 1]))) j++;
        size_t start = i == 0x454F46 ? i - 1 : i;   // offset "EOF" would end the patch early
        o.push_back((u8)(start >> 16)); o.push_back((u8)(start >> 8)); o.push_back((u8)start);
        size_t len = j - start;
        o.push_back((u8)(len >> 8)); o.push_back((u8)len);
        for (size_t k = start; k < j; k++) o.push_back(b[k]);
        i = j;
    }
    o.push_back('E'); o.push_back('O'); o.push_back('F');
    return o;
}

// ------------------------------------------------------------------ automatic table
static const char* const WORDS[] = {
    "START", "PRESS", "PUSH", "GAME", "OVER", "PLAY", "PLAYER", "SCORE", "LEVEL", "STAGE", "WORLD", "TIME", "CONTINUE",
    "OPTION", "OPTIONS", "SOUND", "MUSIC", "SELECT", "PAUSE", "BONUS", "LIFE", "LIVES", "POWER", "ITEM", "ITEMS", "SAVE",
    "LOAD", "MENU", "EXIT", "HIGH", "THE", "AND", "YOU", "YOUR", "THIS", "THAT", "WITH", "HAVE", "WHAT", "ARE", "FOR",
    "NOT", "CAN", "HELLO", "ATTACK", "MAGIC", "GOLD", "SHOP", "ENEMY", "KEY", "DOOR", "ROOM", "NEW", "END", "TOWN",
    "HOUSE", "NAME", "CREDITS", "PASSWORD", "MONEY", "BUY", "SELL", "USE", "GET", "WILL", "FROM", "THERE", "HERE",
};

std::vector<TableGuess> auto_detect_tables(const std::vector<u8>& rom, size_t max) {
    // hits per alphabet position, upper and lower case, counting how many different words agree
    std::vector<int> up(256, 0), lo(256, 0), upw(256, 0), low(256, 0);
    for (const char* w : WORDS) {
        std::string W = w, L = W;
        for (auto& c : L) c = (char)tolower((unsigned char)c);
        for (int pass = 0; pass < 2; pass++) {
            auto hits = relative_search(rom, pass ? L : W, 4000);
            std::vector<int> per(256, 0);
            for (auto& h : hits) per[h.base]++;
            for (int b = 0; b < 256; b++) if (per[b]) {
                // short words also appear by chance: they count less
                const int wgt = (int)W.size() >= 5 ? 3 : (int)W.size() == 4 ? 2 : 1;
                (pass ? lo : up)[b] += per[b] * wgt;
                (pass ? low : upw)[b]++;
            }
        }
    }
    std::vector<TableGuess> out;
    for (int b = 0; b < 256; b++) {
        if (upw[b] < 3 && low[b] < 3) continue;         // at least three different words must agree
        TableGuess g;
        if (upw[b] >= 3) { g.upper = b; g.score += up[b]; g.words += upw[b]; }
        if (low[b] >= 3 && g.upper < 0) { g.lower = b; g.score += lo[b]; g.words += low[b]; }
        if (g.upper >= 0) {       // lower case of the same table: the best other position, if any
            int bl = -1;
            for (int k = 0; k < 256; k++) if (low[k] >= 3 && (k >= b + 26 || k + 26 <= b) && (bl < 0 || lo[k] > lo[bl])) bl = k;
            if (bl >= 0) { g.lower = bl; g.score += lo[bl]; g.words += low[bl]; }
        }
        out.push_back(g);
    }
    std::sort(out.begin(), out.end(), [](const TableGuess& a, const TableGuess& b) { return a.score > b.score; });
    // the same table found through its upper and its lower case appears twice: keep one
    std::vector<TableGuess> uniq;
    for (auto& g : out) {
        bool dup = false;
        for (auto& u : uniq) if ((g.upper >= 0 && g.upper == u.upper) || (g.lower >= 0 && g.lower == u.lower)) dup = true;
        if (!dup) uniq.push_back(g);
        if (uniq.size() >= max) break;
    }
    for (auto& g : uniq) {
        TextTable t = table_from_bases(g.upper, g.lower, -1, -1, -1);
        g.space = guess_space(rom, t);
        if (g.space >= 0) t.chars[(u8)g.space] = " ";
        g.end = guess_terminator(rom, t);
        if (g.end >= 0) t.ends.push_back((u8)g.end);
        auto e = dump_texts(rom, t, 6);
        std::sort(e.begin(), e.end(), [](const TextEntry& a, const TextEntry& b) { return a.len > b.len; });
        for (size_t i = 0; i < e.size() && i < 3; i++) g.sample += (i ? "  /  " : "") + e[i].text.substr(0, 40);
    }
    return uniq;
}

TextTable table_from_guess(const TableGuess& g) {
    TextTable t = table_from_bases(g.upper, g.lower, -1, g.space, g.end);
    return t;
}
