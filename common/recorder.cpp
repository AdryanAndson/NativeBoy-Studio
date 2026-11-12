#include "recorder.h"
#include "platform.h"
#include "stb_image_write.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>

// ================================================================== GIF
static void w16(FILE* f, int v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }

bool GifRecorder::start(const std::string& p, int w, int h) {
    stop();
    f = plat::fopen_utf8(p, "wb");
    if (!f) return false;
    path = p; W = w; H = h; frames_seen = 0; delay_acc = 0;
    fwrite("GIF89a", 1, 6, f);
    w16(f, W); w16(f, H);
    fputc(0x70, f); fputc(0, f); fputc(0, f);     // no global colour table, 8-bit colour resolution
    // loop forever
    static const uint8_t loop[] = {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 0x03, 0x01, 0x00, 0x00, 0x00};
    fwrite(loop, 1, sizeof loop, f);
    return true;
}

// GIF LZW with a fixed 8-bit minimum code size
static void lzw_write(FILE* f, const std::vector<uint8_t>& px) {
    const int min_size = 8, clear = 256, eoi = 257;
    std::vector<uint8_t> out;
    uint32_t bitbuf = 0; int bits = 0, code_size = min_size + 1;
    auto emit = [&](int code) {
        bitbuf |= (uint32_t)code << bits; bits += code_size;
        while (bits >= 8) { out.push_back((uint8_t)bitbuf); bitbuf >>= 8; bits -= 8; }
    };
    std::unordered_map<uint32_t, int> dict;
    int next = 258;
    emit(clear);
    int cur = px.empty() ? 0 : px[0];
    for (size_t i = 1; i < px.size(); i++) {
        uint32_t key = ((uint32_t)cur << 8) | px[i];
        auto it = dict.find(key);
        if (it != dict.end()) { cur = it->second; continue; }
        emit(cur);
        if (next < 4096) {
            dict[key] = next++;
            if (next > (1 << code_size) && code_size < 12) code_size++;
        } else {
            emit(clear);
            dict.clear(); next = 258; code_size = min_size + 1;
        }
        cur = px[i];
    }
    emit(cur);
    emit(eoi);
    if (bits > 0) out.push_back((uint8_t)bitbuf);
    fputc(min_size, f);
    for (size_t p = 0; p < out.size(); p += 255) {
        size_t n = std::min<size_t>(255, out.size() - p);
        fputc((int)n, f); fwrite(&out[p], 1, n, f);
    }
    fputc(0, f);
}

void GifRecorder::add_frame(const uint32_t* argb, int w, int h, int scale) {
    if (!f) return;
    frames_seen++;
    if (frames_seen & 1) return;                  // ~30 frames per second
    if (w * scale != W || h * scale != H) return; // size changed (e.g. widescreen toggled): skip
    // palette: exact when the frame has up to 256 colours, otherwise a 6x7x6 colour cube
    std::unordered_map<uint32_t, int> pal;
    std::vector<uint32_t> colours;
    bool exact = true;
    for (int i = 0; i < w * h && exact; i++) {
        uint32_t c = argb[i] & 0xFFFFFF;
        if (!pal.count(c)) { if (colours.size() >= 256) exact = false; else { pal[c] = (int)colours.size(); colours.push_back(c); } }
    }
    idx.resize((size_t)W * H);
    auto cube = [](uint32_t c) { int r = ((c >> 16) & 255) * 5 / 255, g = ((c >> 8) & 255) * 6 / 255, b = (c & 255) * 5 / 255; return (uint8_t)(r * 42 + g * 6 + b); };
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t c = argb[(y / scale) * w + x / scale] & 0xFFFFFF;
            idx[(size_t)y * W + x] = exact ? (uint8_t)pal[c] : cube(c);
        }
    if (!exact) {
        colours.clear();
        for (int r = 0; r < 6; r++) for (int g = 0; g < 7; g++) for (int b = 0; b < 6; b++)
            colours.push_back((uint32_t)(r * 255 / 5) << 16 | (uint32_t)(g * 255 / 6) << 8 | (uint32_t)(b * 255 / 5));
    }
    while (colours.size() < 256) colours.push_back(0);
    // delay in 1/100 s, alternating 3 and 4 so that the average matches 2 game frames (3.35)
    delay_acc += 200.0 / 59.7275;
    int d = (int)delay_acc; delay_acc -= d;
    static uint8_t gce[8] = {0x21, 0xF9, 0x04, 0x00, 0, 0, 0, 0x00};
    gce[4] = (uint8_t)d; gce[5] = 0;
    fwrite(gce, 1, 8, f);
    fputc(0x2C, f); w16(f, 0); w16(f, 0); w16(f, W); w16(f, H);
    fputc(0x87, f);                                // local colour table, 256 entries
    for (uint32_t c : colours) { fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f); }
    lzw_write(f, idx);
    if (seconds() > 60) stop();                    // GIFs get huge: one minute at most
}

void GifRecorder::stop() {
    if (!f) return;
    fputc(0x3B, f);
    fclose(f); f = nullptr;
}

// ================================================================== AVI (Motion-JPEG + PCM)
static void p32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (i * 8))); }
static void p16(std::vector<uint8_t>& v, uint16_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
static void pid(std::vector<uint8_t>& v, const char* s) { v.insert(v.end(), s, s + 4); }

static std::vector<uint8_t> avi_header(int W, int H, int rate, uint32_t frames, uint32_t audio_frames, uint32_t max_chunk) {
    std::vector<uint8_t> strl_v, strl_a, hdrl;
    // video stream
    pid(strl_v, "strh"); p32(strl_v, 56);
    pid(strl_v, "vids"); pid(strl_v, "MJPG"); p32(strl_v, 0); p32(strl_v, 0); p32(strl_v, 0);
    p32(strl_v, 10000); p32(strl_v, 597275);      // 59.7275 frames per second
    p32(strl_v, 0); p32(strl_v, frames); p32(strl_v, max_chunk); p32(strl_v, 0xFFFFFFFF); p32(strl_v, 0);
    p16(strl_v, 0); p16(strl_v, 0); p16(strl_v, (uint16_t)W); p16(strl_v, (uint16_t)H);
    pid(strl_v, "strf"); p32(strl_v, 40);
    p32(strl_v, 40); p32(strl_v, (uint32_t)W); p32(strl_v, (uint32_t)H); p16(strl_v, 1); p16(strl_v, 24);
    pid(strl_v, "MJPG"); p32(strl_v, (uint32_t)(W * H * 3)); p32(strl_v, 0); p32(strl_v, 0); p32(strl_v, 0); p32(strl_v, 0);
    // audio stream
    pid(strl_a, "strh"); p32(strl_a, 56);
    pid(strl_a, "auds"); p32(strl_a, 0); p32(strl_a, 0); p32(strl_a, 0); p32(strl_a, 0);
    p32(strl_a, 1); p32(strl_a, (uint32_t)rate);
    p32(strl_a, 0); p32(strl_a, audio_frames); p32(strl_a, (uint32_t)rate); p32(strl_a, 0xFFFFFFFF); p32(strl_a, 4);
    p16(strl_a, 0); p16(strl_a, 0); p16(strl_a, 0); p16(strl_a, 0);
    pid(strl_a, "strf"); p32(strl_a, 16);
    p16(strl_a, 1); p16(strl_a, 2); p32(strl_a, (uint32_t)rate); p32(strl_a, (uint32_t)rate * 4); p16(strl_a, 4); p16(strl_a, 16);
    auto list = [](const char* type, const std::vector<uint8_t>& body) {
        std::vector<uint8_t> v; pid(v, "LIST"); p32(v, (uint32_t)body.size() + 4); pid(v, type); v.insert(v.end(), body.begin(), body.end()); return v;
    };
    std::vector<uint8_t> body;
    pid(body, "avih"); p32(body, 56);
    p32(body, (uint32_t)(1000000.0 / 59.7275)); p32(body, 0); p32(body, 0); p32(body, 0x10);   // has index
    p32(body, frames); p32(body, 0); p32(body, 2); p32(body, max_chunk); p32(body, (uint32_t)W); p32(body, (uint32_t)H);
    p32(body, 0); p32(body, 0); p32(body, 0); p32(body, 0);
    auto lv = list("strl", strl_v), la = list("strl", strl_a);
    body.insert(body.end(), lv.begin(), lv.end());
    body.insert(body.end(), la.begin(), la.end());
    return list("hdrl", body);
}

bool VideoRecorder::start(const std::string& p, int w, int h, int sample_rate) {
    stop();
    f = plat::fopen_utf8(p, "wb");
    if (!f) return false;
    path = p; W = w; H = h; rate = sample_rate; frames = audio_frames = movi_bytes = 0; index.clear();
    auto hdr = avi_header(W, H, rate, 0, 0, 0);
    fwrite("RIFF\0\0\0\0AVI ", 1, 12, f);
    fwrite(hdr.data(), 1, hdr.size(), f);
    fwrite("LIST\0\0\0\0movi", 1, 12, f);
    movi_start = ftell(f) - 4;      // offsets in idx1 are relative to the "movi" fourcc
    return true;
}

void VideoRecorder::chunk(const char* id, const void* data, uint32_t size, bool key) {
    long off = ftell(f);
    fwrite(id, 1, 4, f);
    fwrite(&size, 4, 1, f);    // little-endian hosts (x86-64)
    fwrite(data, 1, size, f);
    if (size & 1) fputc(0, f);
    uint32_t idv; memcpy(&idv, id, 4);
    index.push_back({idv, key ? 0x10u : 0u, (uint32_t)(off - movi_start), size});
    movi_bytes += 8 + size + (size & 1);
}

static void jpg_sink(void* ctx, void* data, int size) { auto* v = (std::vector<uint8_t>*)ctx; v->insert(v->end(), (uint8_t*)data, (uint8_t*)data + size); }

void VideoRecorder::add_frame(const uint32_t* argb, int w, int h, int scale) {
    if (!f) return;
    if (w * scale != W || h * scale != H) { stop(); return; }   // picture size changed: finish the file
    rgb.resize((size_t)W * H * 3);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t c = argb[(y / scale) * w + x / scale];
            uint8_t* o = &rgb[((size_t)y * W + x) * 3];
            o[0] = (c >> 16) & 255; o[1] = (c >> 8) & 255; o[2] = c & 255;
        }
    jpg.clear();
    stbi_write_jpg_to_func(jpg_sink, &jpg, W, H, 3, rgb.data(), 80);   // <= 90: 4:2:0 chroma, plays everywhere
    chunk("00dc", jpg.data(), (uint32_t)jpg.size(), true);
    frames++;
    if (movi_bytes > 900u * 1024 * 1024) stop();     // plain AVI files must stay under 1 GB
}

void VideoRecorder::add_audio(const float* s, size_t n) {
    if (!f || n == 0) return;
    std::vector<int16_t> b(n);
    for (size_t i = 0; i < n; i++) b[i] = (int16_t)std::max(-32768.f, std::min(32767.f, s[i] * 32767.f));
    chunk("01wb", b.data(), (uint32_t)(n * 2), true);
    audio_frames += n / 2;
}

void VideoRecorder::stop() {
    if (!f) return;
    long movi_end = ftell(f);
    // index
    uint32_t isz = (uint32_t)(index.size() * 16);
    fwrite("idx1", 1, 4, f); fwrite(&isz, 4, 1, f);
    for (auto& e : index) { fwrite(&e.id, 4, 1, f); fwrite(&e.flags, 4, 1, f); fwrite(&e.off, 4, 1, f); fwrite(&e.size, 4, 1, f); }
    long end = ftell(f);
    uint32_t max_chunk = 0; for (auto& e : index) max_chunk = std::max(max_chunk, e.size);
    // patch sizes and the header with the final counts
    uint32_t riff = (uint32_t)(end - 8), movi = (uint32_t)(movi_end - (movi_start - 4) - 4);
    fseek(f, 4, SEEK_SET); fwrite(&riff, 4, 1, f);
    auto hdr = avi_header(W, H, rate, (uint32_t)frames, (uint32_t)audio_frames, max_chunk);
    fseek(f, 12, SEEK_SET); fwrite(hdr.data(), 1, hdr.size(), f);
    fseek(f, movi_start - 4, SEEK_SET); fwrite(&movi, 4, 1, f);
    fclose(f); f = nullptr;
}
