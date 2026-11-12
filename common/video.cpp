#include "video.h"
#include "platform.h"
#include "settings.h"
#include "../core/sgb.h"
#include <algorithm>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace gb;

#define SAME4(a, b, c, d) {a, b, c, d}, {a, b, c, d}, {a, b, c, d}
const DmgPalette DMG_PALETTES[] = {
    {"classico", "Clássico (verde DMG)", SAME4(0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820)},
    {"original", "Original (LCD verde-oliva)", SAME4(0xFF9BBC0F, 0xFF8BAC0F, 0xFF306230, 0xFF0F380F)},
    {"cinza", "Game Boy Pocket (cinza)", SAME4(0xFFE8E8E8, 0xFFA0A0A0, 0xFF585858, 0xFF101010)},
    {"light", "Game Boy Light (azulado)", SAME4(0xFF00B0C8, 0xFF0090A8, 0xFF006878, 0xFF004050)},
    {"sepia", "Sépia", SAME4(0xFFF8E8C8, 0xFFD8B078, 0xFF906838, 0xFF302010)},
    {"contraste", "Alto contraste", SAME4(0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000)},
    {"acessivel", "Acessível p/ daltonismo (amarelo e azul)", SAME4(0xFFFFFFFF, 0xFFFFD800, 0xFF0050C8, 0xFF000000)},
    {"noite", "Noturno (tons frios)", SAME4(0xFFC4F0C2, 0xFF5AB9A8, 0xFF1E606E, 0xFF2D1B00)},
    // colourised: separate colours for scenery and characters (inspired by the Game Boy Color's
    // built-in palettes for original Game Boy games)
    {"cor_verde_vermelho", "Colorido: cenário verde, personagens vermelhos",
     {0xFFFFFFFF, 0xFF7BFF31, 0xFF0063C5, 0xFF000000}, {0xFFFFFFFF, 0xFFFF8484, 0xFF943A3A, 0xFF000000}, {0xFFFFFFFF, 0xFFFF8484, 0xFF943A3A, 0xFF000000}},
    {"cor_azul", "Colorido: cenário azul",
     {0xFFFFFFFF, 0xFF63A5FF, 0xFF0000FF, 0xFF000000}, {0xFFFFFFFF, 0xFFFF8484, 0xFF943A3A, 0xFF000000}, {0xFFFFFFFF, 0xFF7BFF31, 0xFF008400, 0xFF000000}},
    {"cor_azul_escuro", "Colorido: azul-escuro",
     {0xFFFFFFFF, 0xFF8C8CDE, 0xFF52528C, 0xFF000000}, {0xFFFFFFFF, 0xFFFF8484, 0xFF943A3A, 0xFF000000}, {0xFFFFFFFF, 0xFFFFAD63, 0xFF843100, 0xFF000000}},
    {"cor_amarelo", "Colorido: amarelo",
     {0xFFFFFFFF, 0xFFFFFF00, 0xFF7B4A00, 0xFF000000}, {0xFFFFFFFF, 0xFF63A5FF, 0xFF0000FF, 0xFF000000}, {0xFFFFFFFF, 0xFF7BFF31, 0xFF008400, 0xFF000000}},
    {"cor_marrom", "Colorido: marrom", SAME4(0xFFFFFFFF, 0xFFFFAD63, 0xFF843100, 0xFF000000)},
    {"cor_vermelho", "Colorido: vermelho", SAME4(0xFFFFFFFF, 0xFFFF8484, 0xFF943A3A, 0xFF000000)},
    {"cor_pastel", "Colorido: pastel", SAME4(0xFFFFFFA5, 0xFFFF9494, 0xFF9494FF, 0xFF000000)},
    {"cor_fogo", "Colorido: vermelho e amarelo", SAME4(0xFFFFFFFF, 0xFFFFFF00, 0xFFFF0000, 0xFF000000)},
    {"cor_invertido", "Colorido: invertido", SAME4(0xFF000000, 0xFF008484, 0xFFFFDE00, 0xFFFFFFFF)},
};
const int DMG_PALETTE_COUNT = (int)(sizeof DMG_PALETTES / sizeof DMG_PALETTES[0]);

std::string palette_to_string(const uint32_t c[12]) {
    std::string s;
    for (int i = 0; i < 12; i++) { char b[8]; snprintf(b, sizeof b, "%06X", (unsigned)(c[i] & 0xFFFFFF)); s += (i ? "," : "") + std::string(b); }
    return s;
}
bool palette_from_string(const std::string& s, uint32_t c[12]) {
    int n = 0;
    size_t p = 0;
    while (n < 12 && p < s.size()) {
        size_t q = s.find(',', p);
        if (q == std::string::npos) q = s.size();
        c[n++] = 0xFF000000u | (uint32_t)strtoul(s.substr(p, q - p).c_str(), nullptr, 16);
        p = q + 1;
    }
    return n == 12;
}

void apply_dmg_palette(Machine& m, const std::string& id, const std::string& custom) {
    uint32_t c[12];
    if (id == "personalizada" && palette_from_string(custom, c)) {
        memcpy(m.dmg_palette, c, 16); memcpy(m.dmg_obj0, c + 4, 16); memcpy(m.dmg_obj1, c + 8, 16);
        return;
    }
    const DmgPalette* p = &DMG_PALETTES[0];
    for (int i = 0; i < DMG_PALETTE_COUNT; i++) if (id == DMG_PALETTES[i].id) p = &DMG_PALETTES[i];
    memcpy(m.dmg_palette, p->bg, sizeof p->bg);
    memcpy(m.dmg_obj0, p->obj0, sizeof p->obj0);
    memcpy(m.dmg_obj1, p->obj1, sizeof p->obj1);
}

const char* FILTER_IDS[FILTER_COUNT] = {"nitido", "suave", "scale2x", "scale3x", "lcd", "crt", "xbr2", "xbr4", "xbr8"};
const char* FILTER_NAMES[FILTER_COUNT] = {"Nítido (pixels perfeitos)", "Suave (bilinear)", "Scale2x (bordas suavizadas)", "Scale3x (bordas suavizadas)", "Grade de LCD", "TV CRT (linhas de varredura)",
                                          "xBR 2x (curvas arredondadas)", "xBR 4x (alta definição suave)", "xBR 8x (para telas 4K)"};
int filter_from_id(const std::string& s) { for (int i = 0; i < FILTER_COUNT; i++) if (s == FILTER_IDS[i]) return i; return 0; }

static inline uint64_t fnv(uint64_t h, uint8_t b) { h ^= b; h *= 1099511628211ULL; return h; }

static const u8* tile_data(const Machine& m, u16 t) {
    int id = t & 0x3FFF;
    int bank = id / 384, idx = id % 384;
    return &m.vram[bank & 1][idx * 16];
}

uint64_t hd_tile_key(const Machine& m, const PixelInfo& pi) {
    if (pi.tile == 0xFFFF) return 0;
    const u8* td = tile_data(m, pi.tile);
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < 16; i++) h = fnv(h, td[i]);
    if (!m.cgb) {
        u8 kind = (pi.tile & 0x8000) ? ((pi.tile & 0x4000) ? 2 : 1) : 0;
        h = fnv(h, kind);
        for (int i = 0; i < 4; i++) h = fnv(h, (pi.pal >> (2 * i)) & 3);
    } else {
        const u8* pal = pi.pal < 8 ? m.bgpal : m.objpal;
        int p = pi.pal & 7;
        h = fnv(h, pi.pal >= 8 ? 0x55 : 0xAA);
        for (int i = 0; i < 8; i++) h = fnv(h, pal[p * 8 + i]);
    }
    return h | 1;   // never 0
}

static u32 cgb_rgb(const Machine& m, u16 c) {
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31, R, G, B;
    if (m.color_correct) {
        R = std::min(255, (r * 26 + g * 4 + b * 2) >> 2);
        G = std::min(255, (g * 24 + b * 8) >> 2);
        B = std::min(255, (r * 6 + g * 4 + b * 22) >> 2);
    } else { R = (r << 3) | (r >> 2); G = (g << 3) | (g >> 2); B = (b << 3) | (b >> 2); }
    return 0xFF000000u | (R << 16) | (G << 8) | B;
}

void tile_rgba(const Machine& m, const PixelInfo& pi, uint32_t out[64]) {
    const u8* td = tile_data(m, pi.tile);
    u32 col[4];
    bool obj;
    if (!m.cgb) {
        obj = (pi.tile & 0x8000) != 0;
        const u32* base = obj ? ((pi.tile & 0x4000) ? m.dmg_obj1 : m.dmg_obj0) : m.dmg_palette;
        for (int i = 0; i < 4; i++) col[i] = base[(pi.pal >> (2 * i)) & 3];
    } else {
        obj = pi.pal >= 8;
        const u8* pal = obj ? m.objpal : m.bgpal;
        int p = pi.pal & 7;
        for (int i = 0; i < 4; i++) col[i] = cgb_rgb(m, (u16)(pal[p * 8 + i * 2] | (pal[p * 8 + i * 2 + 1] << 8)));
    }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int ci = (((td[y * 2 + 1] >> (7 - x)) & 1) << 1) | ((td[y * 2] >> (7 - x)) & 1);
            out[y * 8 + x] = (obj && ci == 0) ? 0x00000000u : col[ci];
        }
}

bool load_png(const std::string& path, std::vector<uint32_t>& argb, int& w, int& h) {
    std::vector<uint8_t> file;
    if (!plat::read_file(path, file)) return false;
    int n;
    unsigned char* px = stbi_load_from_memory(file.data(), (int)file.size(), &w, &h, &n, 4);
    if (!px) return false;
    argb.resize((size_t)w * h);
    for (int i = 0; i < w * h; i++) argb[i] = ((u32)px[i * 4 + 3] << 24) | ((u32)px[i * 4] << 16) | ((u32)px[i * 4 + 1] << 8) | px[i * 4 + 2];
    stbi_image_free(px);
    return true;
}

static void png_writer(void* ctx, void* data, int size) {
    auto* v = (std::vector<uint8_t>*)ctx;
    v->insert(v->end(), (uint8_t*)data, (uint8_t*)data + size);
}
bool save_png(const std::string& path, const uint32_t* argb, int w, int h) {
    std::vector<uint8_t> rgba((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        u32 c = argb[i];
        rgba[i * 4] = (c >> 16) & 255; rgba[i * 4 + 1] = (c >> 8) & 255; rgba[i * 4 + 2] = c & 255; rgba[i * 4 + 3] = c >> 24;
    }
    std::vector<uint8_t> out;
    if (!stbi_write_png_to_func(png_writer, &out, w, h, 4, rgba.data(), w * 4)) return false;
    return plat::write_file(path, out.data(), out.size());
}

bool HdPack::load(const std::string& d, std::string& err) {
    dir = d; name = plat::file_name(d);
    index.clear(); images.clear();
    Settings ini;
    ini.load(plat::join(d, "pack.ini"));
    scale = std::max(1, std::min(16, ini.geti("scale", 4)));
    int bad = 0;
    for (auto& f : plat::list_dir(d, false)) {
        if (f.size() != 20 || f.substr(16) != ".png") continue;
        uint64_t key = strtoull(f.substr(0, 16).c_str(), nullptr, 16);
        std::vector<uint32_t> img; int w, h;
        if (!load_png(plat::join(d, f), img, w, h)) { bad++; continue; }
        if (w != 8 * scale || h != 8 * scale) {     // resample anything else to the pack size
            std::vector<uint32_t> r((size_t)64 * scale * scale);
            for (int y = 0; y < 8 * scale; y++)
                for (int x = 0; x < 8 * scale; x++) r[y * 8 * scale + x] = img[(y * h / (8 * scale)) * w + x * w / (8 * scale)];
            img.swap(r);
        }
        index[key] = (int)images.size();
        images.push_back(std::move(img));
    }
    if (images.empty()) { err = "O pacote HD \"" + name + "\" não contém imagens de tiles válidas."; return false; }
    if (bad) err = std::to_string(bad) + " imagem(ns) do pacote HD não puderam ser lidas.";
    return true;
}

static void scale2x(const u32* s, int w, int h, u32* d) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            u32 P = s[y * w + x];
            u32 A = y > 0 ? s[(y - 1) * w + x] : P, B = x < w - 1 ? s[y * w + x + 1] : P;
            u32 C = x > 0 ? s[y * w + x - 1] : P, D = y < h - 1 ? s[(y + 1) * w + x] : P;
            u32 o1 = P, o2 = P, o3 = P, o4 = P;
            if (C == A && C != D && A != B) o1 = A;
            if (A == B && A != C && B != D) o2 = B;
            if (D == C && D != B && C != A) o3 = C;
            if (B == D && B != A && D != C) o4 = D;
            int W = w * 2;
            d[(y * 2) * W + x * 2] = o1; d[(y * 2) * W + x * 2 + 1] = o2;
            d[(y * 2 + 1) * W + x * 2] = o3; d[(y * 2 + 1) * W + x * 2 + 1] = o4;
        }
}
static void scale3x(const u32* s, int w, int h, u32* d) {
    auto px = [&](int x, int y) { x = std::max(0, std::min(w - 1, x)); y = std::max(0, std::min(h - 1, y)); return s[y * w + x]; };
    int W = w * 3;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            u32 A = px(x - 1, y - 1), B = px(x, y - 1), C = px(x + 1, y - 1), D = px(x - 1, y), E = px(x, y), F = px(x + 1, y), G = px(x - 1, y + 1), H = px(x, y + 1), I = px(x + 1, y + 1);
            u32 o[9] = {E, E, E, E, E, E, E, E, E};
            if (B != H && D != F) {
                o[0] = D == B ? D : E;
                o[1] = (D == B && E != C) || (B == F && E != A) ? B : E;
                o[2] = B == F ? F : E;
                o[3] = (D == B && E != G) || (D == H && E != A) ? D : E;
                o[5] = (B == F && E != I) || (H == F && E != C) ? F : E;
                o[6] = D == H ? D : E;
                o[7] = (D == H && E != I) || (H == F && E != G) ? H : E;
                o[8] = H == F ? F : E;
            }
            for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) d[(y * 3 + j) * W + x * 3 + i] = o[j * 3 + i];
        }
}
static inline u32 darken(u32 c, int num) {
    u32 r = ((c >> 16) & 255) * num / 16, g = ((c >> 8) & 255) * num / 16, b = (c & 255) * num / 16;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}


// ---------------------------------------------------------------- xBR (level 1), 2x
// Edge-directed pixel-art scaler: for each corner of each pixel it compares the "weight" of the two
// possible diagonal edges (colour distances in YUV around a 5x5 window) and, when the edge crosses
// the corner, blends in the neighbouring colour. Curves and diagonals come out rounded instead of
// stair-stepped. Applying it twice gives 4x.
static inline void yuv(u32 c, int& y, int& u, int& v) {
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    y = (r * 299 + g * 587 + b * 114) / 1000;
    u = (-r * 169 - g * 331 + b * 500) / 1000;
    v = (r * 500 - g * 419 - b * 81) / 1000;
}
static void xbr2x(const u32* s, int w, int h, u32* d) {
    std::vector<int> Y((size_t)w * h), U((size_t)w * h), V((size_t)w * h);
    for (int i = 0; i < w * h; i++) yuv(s[i], Y[i], U[i], V[i]);
    auto at = [&](int x, int y) { x = x < 0 ? 0 : x >= w ? w - 1 : x; y = y < 0 ? 0 : y >= h ? h - 1 : y; return y * w + x; };
    auto dist = [&](int a, int b) { return 48 * std::abs(Y[a] - Y[b]) + 7 * std::abs(U[a] - U[b]) + 6 * std::abs(V[a] - V[b]); };
    auto mix = [](u32 a, u32 b) { return 0xFF000000u | ((((a & 0xFEFEFE) >> 1) + ((b & 0xFEFEFE) >> 1)) & 0xFFFFFF); };
    const int W2 = w * 2;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int E = at(x, y);
            u32 out[4] = {s[E], s[E], s[E], s[E]};     // TL TR BL BR
            // corner directions (sx, sy): BR, BL, TL, TR; offsets are mirrored per corner
            static const int dirs[4][2] = {{1, 1}, {-1, 1}, {-1, -1}, {1, -1}};
            static const int slot[4] = {3, 2, 0, 1};
            for (int k = 0; k < 4; k++) {
                int sx = dirs[k][0], sy = dirs[k][1];
                auto P = [&](int dx, int dy) { return at(x + dx * sx, y + dy * sy); };
                int B = P(0, -1), C = P(1, -1), D = P(-1, 0), F = P(1, 0), G = P(-1, 1), H = P(0, 1), I = P(1, 1);
                int F4 = P(2, 0), H5 = P(0, 2), I4 = P(2, 1), I5 = P(1, 2);
                if (s[E] == s[F] || s[E] == s[H]) continue;        // no edge through this corner
                int e = dist(E, C) + dist(E, G) + dist(I, F4) + dist(I, H5) + 4 * dist(H, F);
                int i = dist(H, D) + dist(H, I5) + dist(F, I4) + dist(F, B) + 4 * dist(E, I);
                if (e < i) {
                    u32 nc = dist(E, F) <= dist(E, H) ? s[F] : s[H];
                    out[slot[k]] = mix(s[E], nc);
                }
            }
            u32* o = d + (size_t)(y * 2) * W2 + x * 2;
            o[0] = out[0]; o[1] = out[1]; o[W2] = out[2]; o[W2 + 1] = out[3];
        }
}

// ---------------------------------------------------------------- effects
static inline u32 vivid_px(u32 c, int amt) {
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    int l = (r * 77 + g * 150 + b * 29) >> 8;
    int k = 256 + amt * 3;                       // up to 1.0 + 1.17 saturation
    auto f = [&](int ch) {
        int v = l + (((ch - l) * k) >> 8);
        v = 128 + (((v - 128) * (256 + amt)) >> 8);   // a little contrast
        return v < 0 ? 0 : v > 255 ? 255 : v;
    };
    return (c & 0xFF000000u) | ((u32)f(r) << 16) | ((u32)f(g) << 8) | (u32)f(b);
}

const uint32_t* VideoStage::process(const Machine& m, int filter, const HdPack* hd, bool lcd_ghosting, bool sgb_border) {
    linear = false;
    hd_hits = 0;
    const bool border = sgb_border && m.sgb && m.sgb->border_loaded && !(hd && !hd->empty());
    const bool wide = m.widescreen && !border;
    int bw = wide ? Machine::WIDE_W : 160, bh = 144;
    const int px0 = wide ? 0 : Machine::WIDE_X0;            // column of x=0 inside pinfo
    const u32* src = wide ? m.fbw : m.fb;
    if (border) {      // Super Game Boy frame: 256x224 with the game in the middle
        sgbbuf.resize(256 * 224);
        m.sgb->compose(m, sgbbuf.data());
        src = sgbbuf.data(); bw = 256; bh = 224;
    }
    // extended view (ultrawide / taller picture): tile identities do not exist outside the wide picture,
    // so HD packs and the sprite shadow are not used in this mode
    extended = wide && m.wide_extend && m.fbx.size() == (size_t)Machine::XW * Machine::XH;
    if (extended) { src = m.fbx.data(); bw = Machine::XW; bh = Machine::XH; hd = nullptr; }
    base_w = bw; base_h = bh;
    // LCD ghosting: the original screen kept part of the previous image
    if (lcd_ghosting) {
        if (ghost.size() != (size_t)bw * bh) ghost.assign(src, src + (size_t)bw * bh);
        for (int i = 0; i < bw * bh; i++) {
            u32 a = src[i], b = ghost[i];
            u32 r = (((a >> 16) & 255) * 5 + ((b >> 16) & 255) * 3) >> 3;
            u32 g = (((a >> 8) & 255) * 5 + ((b >> 8) & 255) * 3) >> 3;
            u32 bl = ((a & 255) * 5 + (b & 255) * 3) >> 3;
            ghost[i] = 0xFF000000u | (r << 16) | (g << 8) | bl;
        }
        src = ghost.data();
    } else if (!ghost.empty()) ghost.clear();

    // modern effects on the base picture
    if (fx.shadow || fx.vivid > 0) {
        fxbuf.assign(src, src + (size_t)bw * bh);
        if (fx.shadow && m.track_pixels && !border && !extended) {
            // a sprite pixel darkens the background pixel one step down-right of it
            for (int y = bh - 2; y >= 0; y--)
                for (int x = bw - 2; x >= 0; x--) {
                    const PixelInfo& a = m.pinfo[y * Machine::WIDE_W + x + px0];
                    const PixelInfo& b = m.pinfo[(y + 1) * Machine::WIDE_W + x + 1 + px0];
                    if (a.tile != 0xFFFF && a.pal >= 8 && !(b.tile != 0xFFFF && b.pal >= 8)) {
                        u32& c = fxbuf[(size_t)(y + 1) * bw + x + 1];
                        c = (c & 0xFF000000u) | ((((c >> 16) & 255) * 150 >> 8) << 16) | ((((c >> 8) & 255) * 150 >> 8) << 8) | ((c & 255) * 150 >> 8);
                    }
                }
        }
        if (fx.vivid > 0) for (auto& c : fxbuf) c = vivid_px(c, fx.vivid);
        src = fxbuf.data();
    }
    base_px = src;
    const uint32_t* out = run_filters(m, filter, hd, src, bw, bh, px0);
    if (fx.vignette) {
        if (vig_w != w || vig_h != h) {
            vig.resize((size_t)w * h);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    float dx = (x + 0.5f) / w - 0.5f, dy = (y + 0.5f) / h - 0.5f;
                    float d2 = (dx * dx + dy * dy) * 2.0f;          // 0 centre .. 1 corner
                    vig[(size_t)y * w + x] = 1.0f - 0.45f * d2 * d2;
                }
            vig_w = w; vig_h = h;
        }
        if (out != buf.data()) { buf.assign(out, out + (size_t)w * h); out = buf.data(); }
        for (size_t i = 0; i < (size_t)w * h; i++) {
            u32 c = buf[i]; float k = vig[i];
            buf[i] = (c & 0xFF000000u) | ((u32)(((c >> 16) & 255) * k) << 16) | ((u32)(((c >> 8) & 255) * k) << 8) | (u32)((c & 255) * k);
        }
    }
    return out;
}

const uint32_t* VideoStage::run_filters(const Machine& m, int filter, const HdPack* hd, const u32* src, int bw, int bh, int px0) {
    if (hd && !hd->empty()) {
        int S = hd->scale;
        w = bw * S; h = bh * S;
        buf.resize((size_t)w * h);
        std::unordered_map<uint64_t, int> cache;      // (tile,pal) -> image or -1 for this frame
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++) {
                const PixelInfo& pi = m.pinfo[y * Machine::WIDE_W + x + px0];
                u32 base = src[y * bw + x];
                int img = -1;
                if (pi.tile != 0xFFFF) {
                    uint64_t ck = ((uint64_t)pi.tile << 8) | pi.pal;
                    auto it = cache.find(ck);
                    if (it == cache.end()) {
                        auto f = hd->index.find(hd_tile_key(m, pi));
                        img = f == hd->index.end() ? -1 : f->second;
                        cache[ck] = img;
                    } else img = it->second;
                }
                u32* o = &buf[(size_t)(y * S) * w + x * S];
                if (img < 0) {
                    for (int j = 0; j < S; j++) for (int i = 0; i < S; i++) o[j * w + i] = base;
                } else {
                    hd_hits++;
                    const auto& im = hd->images[img];
                    int tx = pi.sub & 7, ty = pi.sub >> 3;
                    for (int j = 0; j < S; j++)
                        for (int i = 0; i < S; i++) {
                            u32 c = im[(size_t)(ty * S + j) * 8 * S + tx * S + i];
                            o[j * w + i] = (c >> 24) >= 128 ? (c | 0xFF000000u) : base;
                        }
                }
            }
        linear = filter == FILTER_SMOOTH;
        return buf.data();
    }
    switch (filter) {
    case FILTER_SCALE2X: w = bw * 2; h = bh * 2; buf.resize((size_t)w * h); scale2x(src, bw, bh, buf.data()); return buf.data();
    case FILTER_SCALE3X: w = bw * 3; h = bh * 3; buf.resize((size_t)w * h); scale3x(src, bw, bh, buf.data()); return buf.data();
    case FILTER_LCD:
        w = bw * 3; h = bh * 3; buf.resize((size_t)w * h);
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++) {
                u32 c = src[y * bw + x];
                for (int j = 0; j < 3; j++)
                    for (int i = 0; i < 3; i++) buf[(size_t)(y * 3 + j) * w + x * 3 + i] = (i == 2 || j == 2) ? darken(c, 12) : c;
            }
        linear = true;
        return buf.data();
    case FILTER_CRT:
        // 3x: dark scanline every third row + a soft RGB aperture mask
        w = bw * 3; h = bh * 3; buf.resize((size_t)w * h);
        for (int y = 0; y < bh; y++)
            for (int x = 0; x < bw; x++) {
                u32 c = src[y * bw + x];
                u32 r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
                for (int j = 0; j < 3; j++) {
                    int k = j == 2 ? 7 : 16;     // scanline
                    for (int i = 0; i < 3; i++) {
                        u32 rr = r * k / 16, gg = g * k / 16, bb = b * k / 16;
                        if (i == 0) { gg = gg * 13 / 16; bb = bb * 13 / 16; }
                        if (i == 1) { rr = rr * 13 / 16; bb = bb * 13 / 16; }
                        if (i == 2) { rr = rr * 13 / 16; gg = gg * 13 / 16; }
                        buf[(size_t)(y * 3 + j) * w + x * 3 + i] = 0xFF000000u | (rr << 16) | (gg << 8) | bb;
                    }
                }
            }
        linear = true;
        return buf.data();
    case FILTER_XBR2: w = bw * 2; h = bh * 2; buf.resize((size_t)w * h); xbr2x(src, bw, bh, buf.data()); linear = true; return buf.data();
    case FILTER_XBR4:
        tmp2x.resize((size_t)bw * bh * 4);
        xbr2x(src, bw, bh, tmp2x.data());
        w = bw * 4; h = bh * 4; buf.resize((size_t)w * h);
        xbr2x(tmp2x.data(), bw * 2, bh * 2, buf.data());
        linear = true;
        return buf.data();
    case FILTER_XBR8:
        tmp2x.resize((size_t)bw * bh * 4); tmp4x.resize((size_t)bw * bh * 16);
        xbr2x(src, bw, bh, tmp2x.data());
        xbr2x(tmp2x.data(), bw * 2, bh * 2, tmp4x.data());
        w = bw * 8; h = bh * 8; buf.resize((size_t)w * h);
        xbr2x(tmp4x.data(), bw * 4, bh * 4, buf.data());
        linear = true;
        return buf.data();
    case FILTER_SMOOTH: w = bw; h = bh; linear = true; return src;
    default: w = bw; h = bh; return src;
    }
}

void TileDumper::collect(const Machine& m) {
    int x0 = m.widescreen ? 0 : Machine::WIDE_X0, x1 = m.widescreen ? Machine::WIDE_W : Machine::WIDE_X0 + 160;
    for (int i = 0; i < Machine::WIDE_W * 144; i++) {
        int x = i % Machine::WIDE_W;
        if (x < x0 || x >= x1) continue;
        const PixelInfo& pi = m.pinfo[i];
        if (pi.tile == 0xFFFF) continue;
        uint64_t k = hd_tile_key(m, pi);
        if (tiles.count(k)) continue;
        std::vector<uint32_t> px(64);
        tile_rgba(m, pi, px.data());
        tiles[k] = px;
        Meta mt;
        mt.obj = m.cgb ? pi.pal >= 8 : (pi.tile & 0x8000) != 0;
        mt.tile = (uint16_t)(pi.tile & 0x3FFF);
        mt.order = (uint32_t)meta.size();
        meta[k] = mt;
    }
}

std::vector<uint32_t> hd_smooth_tile(const uint32_t* px64, int scale) {
    // xBR on the tile alone (its edges repeat outwards), applied until it reaches the pack size
    const int S = 8 * scale;
    std::vector<u32> cur(px64, px64 + 64), nxt;
    for (auto& c : cur) c |= 0xFF000000u;
    int w = 8;
    while (w < S) {
        nxt.resize((size_t)w * 2 * w * 2);
        xbr2x(cur.data(), w, w, nxt.data());
        cur.swap(nxt); w *= 2;
    }
    std::vector<uint32_t> out((size_t)S * S);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            u32 c = cur[(size_t)(y * w / S) * w + x * w / S] & 0x00FFFFFFu;
            u32 a = px64[(y * 8 / S) * 8 + x * 8 / S] & 0xFF000000u;       // transparency stays pixel-sharp
            out[(size_t)y * S + x] = c | a;
        }
    return out;
}

static const int SHEET_COLS = 16;
int TileDumper::write(const std::string& dir, int scale, std::string& err, bool smooth) const {
    if (!plat::make_dirs(dir) || !plat::make_dirs(plat::join(dir, "_original"))) { err = "Não foi possível criar a pasta " + dir; return 0; }
    int n = 0;
    const int S = 8 * scale;
    std::vector<uint32_t> big((size_t)S * S);
    // order: characters (by tile number, so the pieces of one character stay together), then scenery
    std::vector<uint64_t> keys;
    for (auto& t : tiles) keys.push_back(t.first);
    std::sort(keys.begin(), keys.end(), [&](uint64_t a, uint64_t b) {
        auto ia = meta.find(a), ib = meta.find(b);
        Meta ma = ia == meta.end() ? Meta() : ia->second, mb = ib == meta.end() ? Meta() : ib->second;
        if (ma.obj != mb.obj) return ma.obj;
        if (ma.tile != mb.tile) return ma.tile < mb.tile;
        return ma.order < mb.order;
    });
    const int rows = ((int)keys.size() + SHEET_COLS - 1) / SHEET_COLS;
    std::vector<uint32_t> sheet((size_t)SHEET_COLS * S * std::max(1, rows) * S, 0);
    std::string index = "# Folha HD do NativeBoy: cada célula da folha_hd.png é um tile. Não mude o tamanho da imagem.\nescala=" + std::to_string(scale) +
                        "\ncolunas=" + std::to_string(SHEET_COLS) + "\n";
    for (size_t i = 0; i < keys.size(); i++) {
        const auto& px = tiles.at(keys[i]);
        if (smooth) big = hd_smooth_tile(px.data(), scale);
        else for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) big[(size_t)y * S + x] = px[(y / scale) * 8 + x / scale];
        char name[32];
        snprintf(name, sizeof name, "%016llx.png", (unsigned long long)keys[i]);
        if (save_png(plat::join(dir, name), big.data(), S, S)) n++;
        save_png(plat::join(plat::join(dir, "_original"), name), big.data(), S, S);
        const int cx = (int)(i % SHEET_COLS) * S, cy = (int)(i / SHEET_COLS) * S;
        for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) sheet[(size_t)(cy + y) * SHEET_COLS * S + cx + x] = big[(size_t)y * S + x];
        index += std::to_string(i) + " " + std::string(name, 16) + "\n";
    }
    if (!keys.empty()) save_png(plat::join(dir, "folha_hd.png"), sheet.data(), SHEET_COLS * S, rows * S);
    plat::write_text(plat::join(dir, "folha_hd.txt"), index);
    plat::write_text(plat::join(dir, "pack.ini"), "# Pacote HD do NativeBoy\n# Redesenhe os PNGs mantendo o nome do arquivo. Tamanho de cada tile = 8 x escala.\nscale=" + std::to_string(scale) + "\n");
    plat::write_text(plat::join(dir, "LEIA-ME.txt"),
                     "Pacote HD: cada tile (8x8 pixels do Game Boy) ganha uma imagem ampliada.\r\n\r\n"
                     "Jeito mais fácil: abra folha_hd.png no seu programa de desenho (Aseprite, Krita, Photoshop, Paint...).\r\n"
                     "Ela tem todos os tiles lado a lado: primeiro os personagens, depois o cenário. Redesenhe por cima,\r\n"
                     "sem mudar o tamanho da imagem, e use \"Importar a folha redesenhada\" no NativeBoy Studio.\r\n\r\n"
                     "Também dá para editar cada PNG separado, mantendo o tamanho e o nome.\r\n"
                     "Pixels transparentes deixam aparecer a cor original. Tiles que você apagar continuam com o visual original.\r\n"
                     "A pasta _original guarda os modelos como foram gerados (não apague).\r\n");
    return n;
}

int hd_import_sheet(const std::string& dir, std::string& err) {
    std::string idx;
    if (!plat::read_text(plat::join(dir, "folha_hd.txt"), idx)) { err = "A pasta não tem folha_hd.txt (gere os modelos de novo)."; return -1; }
    int scale = 0, cols = 0;
    std::vector<std::pair<int, std::string>> cells;
    for (auto& l : split_lines(idx)) {
        if (l.empty() || l[0] == '#') continue;
        if (l.compare(0, 7, "escala=") == 0) { scale = atoi(l.c_str() + 7); continue; }
        if (l.compare(0, 8, "colunas=") == 0) { cols = atoi(l.c_str() + 8); continue; }
        size_t sp = l.find(' ');
        if (sp != std::string::npos) cells.push_back({atoi(l.c_str()), l.substr(sp + 1)});
    }
    std::vector<uint32_t> sheet; int w = 0, h = 0;
    if (scale <= 0 || cols <= 0) { err = "folha_hd.txt inválido."; return -1; }
    if (!load_png(plat::join(dir, "folha_hd.png"), sheet, w, h)) { err = "Não foi possível ler folha_hd.png."; return -1; }
    const int S = 8 * scale, rows = ((int)cells.size() + cols - 1) / cols;
    if (w != cols * S || h != rows * S) {
        err = "A folha mudou de tamanho (" + std::to_string(w) + "x" + std::to_string(h) + "; o certo é " + std::to_string(cols * S) + "x" + std::to_string(rows * S) + "). Redesenhe sem redimensionar.";
        return -1;
    }
    int changed = 0;
    std::vector<uint32_t> cell((size_t)S * S), old;
    for (auto& c : cells) {
        const int cx = (c.first % cols) * S, cy = (c.first / cols) * S;
        for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) cell[(size_t)y * S + x] = sheet[(size_t)(cy + y) * w + cx + x];
        const std::string f = plat::join(dir, c.second + ".png");
        int ow = 0, oh = 0;
        if (load_png(f, old, ow, oh) && ow == S && oh == S && old == cell) continue;
        if (save_png(f, cell.data(), S, S)) changed++;
    }
    return changed;
}

void hd_pack_stats(const std::string& dir, int& total, int& redrawn) {
    total = redrawn = 0;
    for (auto& f : plat::list_dir(dir, false)) {
        if (f.size() != 20 || f.substr(16) != ".png") continue;
        total++;
        std::string a, b;
        if (!plat::read_text(plat::join(dir, f), a)) continue;
        if (!plat::read_text(plat::join(plat::join(dir, "_original"), f), b) || a != b) redrawn++;
    }
}
