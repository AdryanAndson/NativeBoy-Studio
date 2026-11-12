// key=value settings files (UTF-8), used by the Studio and by exported games.
#pragma once
#include <map>
#include <string>
#include <vector>

struct Settings {
    std::map<std::string, std::string> kv;
    std::string path;

    bool load(const std::string& p);
    bool save() const;
    std::string get(const std::string& k, const std::string& def = "") const {
        auto it = kv.find(k);
        return it == kv.end() ? def : it->second;
    }
    int geti(const std::string& k, int def) const {
        auto it = kv.find(k);
        if (it == kv.end() || it->second.empty()) return def;
        return atoi(it->second.c_str());
    }
    bool getb(const std::string& k, bool def) const { return geti(k, def ? 1 : 0) != 0; }
    void set(const std::string& k, const std::string& v) { kv[k] = v; }
    void seti(const std::string& k, int v) { kv[k] = std::to_string(v); }
};

std::vector<std::string> split_lines(const std::string& s);
std::string trim(const std::string& s);
