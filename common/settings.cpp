#include "settings.h"
#include "platform.h"

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> r;
    size_t p = 0;
    while (p <= s.size()) {
        size_t q = s.find('\n', p);
        if (q == std::string::npos) q = s.size();
        std::string line = s.substr(p, q - p);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        r.push_back(line);
        p = q + 1;
    }
    return r;
}

bool Settings::load(const std::string& p) {
    path = p;
    std::string txt;
    if (!plat::read_text(p, txt)) return false;
    for (auto& line : split_lines(txt)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == '#' || l[0] == ';' || l[0] == '[') continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        kv[trim(l.substr(0, eq))] = trim(l.substr(eq + 1));
    }
    return true;
}

bool Settings::save() const {
    std::string s = "# NativeBoy - configurações (pode ser editado com o Bloco de Notas)\n";
    for (auto& e : kv) s += e.first + "=" + e.second + "\n";
    return plat::write_text(path, s);
}
