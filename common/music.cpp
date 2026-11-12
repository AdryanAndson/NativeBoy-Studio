#include "music.h"
#include "platform.h"
#include "settings.h"
#include <algorithm>
#include <cstring>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
// stb_vorbis defines short macros that clash with C++ code
#undef L
#undef C
#undef R

bool MusicPack::load(const std::string& d, std::string& err) {
    dir = d; addr = -1; files.clear();
    Settings ini;
    if (!ini.load(plat::join(d, "musica.ini"))) return false;
    std::string a = ini.get("endereco", "");
    if (a.empty()) { err = "musica.ini sem \"endereco\""; return false; }
    addr = (int)strtol(a.c_str(), nullptr, 16) & 0xFFFF;
    std::string mu = ini.get("mudo", "1234");
    mute_mask = 0;
    for (char c : mu) if (c >= '1' && c <= '4') mute_mask |= (uint8_t)(1 << (c - '1'));
    volume = std::max(0, std::min(200, ini.geti("volume", 80))) / 100.0f;
    for (auto& kv : ini.kv) {
        if (kv.first.size() > 2 || kv.first.empty()) continue;
        char* end = nullptr;
        long id = strtol(kv.first.c_str(), &end, 16);
        if (end && !*end && !kv.second.empty()) files[(int)id] = kv.second;
    }
    return true;
}

bool MusicPack::save(const std::string& d) const {
    Settings ini;
    ini.path = plat::join(d, "musica.ini");
    char b[16];
    snprintf(b, sizeof b, "%04X", addr < 0 ? 0 : addr); ini.set("endereco", b);
    std::string mu;
    for (int i = 0; i < 4; i++) if (mute_mask & (1 << i)) mu += (char)('1' + i);
    ini.set("mudo", mu);
    ini.seti("volume", (int)(volume * 100 + 0.5f));
    for (auto& kv : files) { snprintf(b, sizeof b, "%02X", kv.first); ini.set(b, kv.second); }
    return ini.save();
}

bool decode_ogg(const std::string& path, std::vector<float>& out, int& rate, std::string& err) {
    std::vector<uint8_t> data;
    if (!plat::read_file(path, data)) { err = "não foi possível ler " + path; return false; }
    int ch = 0, sr = 0;
    short* pcm = nullptr;
    int n = stb_vorbis_decode_memory(data.data(), (int)data.size(), &ch, &sr, &pcm);
    if (n <= 0 || !pcm || ch < 1) { err = plat::file_name(path) + " não é um arquivo OGG Vorbis válido"; free(pcm); return false; }
    out.resize((size_t)n * 2);
    for (int i = 0; i < n; i++) {
        float l = pcm[(size_t)i * ch] / 32768.0f;
        float r = ch > 1 ? pcm[(size_t)i * ch + 1] / 32768.0f : l;
        out[(size_t)i * 2] = l; out[(size_t)i * 2 + 1] = r;
    }
    free(pcm);
    rate = sr;
    return true;
}

void MusicPlayer::play(const std::string& path, float volume) {
    vol = volume;
    auto it = cache.find(path);
    if (it == cache.end()) {
        auto t = std::make_shared<Track>();
        std::string err;
        if (!decode_ogg(path, t->pcm, t->rate, err)) { error = err; cur.reset(); return; }
        it = cache.emplace(path, t).first;
    }
    cur = it->second; pos = 0;
}

void MusicPlayer::mix(std::vector<float>& out) {
    if (!cur || cur->pcm.size() < 4) return;
    const auto& p = cur->pcm;
    size_t frames = p.size() / 2;
    double step = (double)cur->rate / rate;
    for (size_t i = 0; i + 1 < out.size(); i += 2) {
        size_t a = (size_t)pos; double f = pos - a;
        size_t b = a + 1 >= frames ? 0 : a + 1;
        out[i] += (float)((p[a * 2] * (1 - f) + p[b * 2] * f) * vol);
        out[i + 1] += (float)((p[a * 2 + 1] * (1 - f) + p[b * 2 + 1] * f) * vol);
        pos += step;
        if (pos >= frames) pos -= frames;       // loop
    }
}

static void put32(FILE* f, uint32_t v) { uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)}; fwrite(b, 1, 4, f); }
static void put16(FILE* f, uint16_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; fwrite(b, 1, 2, f); }

bool WavRecorder::start(const std::string& p, int r) {
    stop();
    f = plat::fopen_utf8(p, "wb");
    if (!f) return false;
    path = p; rate = r; frames = 0;
    fwrite("RIFF", 1, 4, f); put32(f, 0); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, (uint32_t)r); put32(f, (uint32_t)r * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, 0);
    return true;
}

void WavRecorder::add(const float* s, size_t n) {
    if (!f) return;
    std::vector<int16_t> b(n);
    for (size_t i = 0; i < n; i++) b[i] = (int16_t)std::max(-32768.f, std::min(32767.f, s[i] * 32767.f));
    fwrite(b.data(), 2, n, f);
    frames += n / 2;
}

void WavRecorder::stop() {
    if (!f) return;
    uint32_t bytes = (uint32_t)(frames * 4);
    fseek(f, 4, SEEK_SET); put32(f, 36 + bytes);
    fseek(f, 40, SEEK_SET); put32(f, bytes);
    fclose(f); f = nullptr;
}
