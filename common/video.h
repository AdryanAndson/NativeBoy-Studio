// Output stage shared by the Studio emulator and exported games:
// palettes, scaling filters (Scale2x/Scale3x, LCD grid) and HD tile packs.
#pragma once
#include "../core/gb.h"
#include <string>
#include <unordered_map>
#include <vector>

// Palettes for original Game Boy games. 'bg' colours the background/window and
// 'obj0'/'obj1' the two sprite palettes, so characters can get their own colours
// (like the Game Boy Color does for older games).
struct DmgPalette { const char* id; const char* name; uint32_t bg[4], obj0[4], obj1[4]; };
extern const DmgPalette DMG_PALETTES[];
extern const int DMG_PALETTE_COUNT;
// custom = 12 colours "RRGGBB,..." (bg x4, obj0 x4, obj1 x4) used when id == "personalizada"
void apply_dmg_palette(gb::Machine& m, const std::string& id, const std::string& custom = "");
std::string palette_to_string(const uint32_t c[12]);
bool palette_from_string(const std::string& s, uint32_t c[12]);

// Stable identity of a tile as drawn (pixels + palette identity, independent of the
// user's color choices). Used to name and look up HD replacement images.
uint64_t hd_tile_key(const gb::Machine& m, const gb::PixelInfo& pi);
void tile_rgba(const gb::Machine& m, const gb::PixelInfo& pi, uint32_t out[64]);  // 8x8 as currently colored

struct HdPack {
    std::string name, dir;
    int scale = 4;
    std::unordered_map<uint64_t, int> index;     // key -> image
    std::vector<std::vector<uint32_t>> images;   // (8*scale)^2 ARGB
    bool load(const std::string& dir, std::string& err);
    bool empty() const { return images.empty(); }
};

enum Filter { FILTER_NEAREST = 0, FILTER_SMOOTH, FILTER_SCALE2X, FILTER_SCALE3X, FILTER_LCD, FILTER_CRT, FILTER_XBR2, FILTER_XBR4, FILTER_XBR8, FILTER_COUNT };
extern const char* FILTER_IDS[FILTER_COUNT];
extern const char* FILTER_NAMES[FILTER_COUNT];
int filter_from_id(const std::string& s);

// Modern look, applied before scaling (shadow, colours) and after it (vignette)
struct VideoFx {
    bool shadow = false;      // drop shadow under sprites (needs Machine::track_pixels)
    int vivid = 0;            // 0-100: more saturated, slightly more contrasted colours
    bool vignette = false;    // edges gently darker, like a screen with depth
    bool any() const { return shadow || vivid > 0 || vignette; }
};

struct VideoStage {
    VideoFx fx;
    std::vector<uint32_t> fxbuf, tmp2x, tmp4x;
    bool extended = false;               // the 352x208 extended view is in use (Machine::fbx)
    const uint32_t* base_px = nullptr;   // picture before scaling (after effects), base_w x base_h
    std::vector<float> vig; int vig_w = 0, vig_h = 0;
    std::vector<uint32_t> buf;
    int w = 160, h = 144;
    bool linear = false;     // ask the GPU for bilinear sampling
    int hd_hits = 0;
    std::vector<uint32_t> ghost;   // previous frame for the LCD ghosting effect
    std::vector<uint32_t> sgbbuf;  // Super Game Boy border composition
    int base_w = 160, base_h = 144; // size before filters (aspect for scaling)
    // builds the image to upload; returns pointer to w*h ARGB pixels
    // (widescreen machines produce 256 columns instead of 160)
    const uint32_t* process(const gb::Machine& m, int filter, const HdPack* hd, bool lcd_ghosting = false, bool sgb_border = false);
private:
    const uint32_t* run_filters(const gb::Machine& m, int filter, const HdPack* hd, const uint32_t* src, int bw, int bh, int px0);
};

// tile dumping for HD pack authoring
struct TileDumper {
    std::unordered_map<uint64_t, std::vector<uint32_t>> tiles;
    struct Meta { uint16_t tile = 0; bool obj = false; uint32_t order = 0; };
    std::unordered_map<uint64_t, Meta> meta;     // tile number, character or scenery, order seen
    void collect(const gb::Machine& m);
    // one PNG per tile (named by its key) plus, in the same folder:
    //   folha_hd.png  every tile in one picture (characters first, then scenery), to redraw in a single file
    //   folha_hd.txt  which tile is in which cell of the sheet
    //   _original/    the models as generated, to count what was redrawn
    // smooth = the models start rounded (xBR) instead of big square pixels
    int write(const std::string& dir, int scale, std::string& err, bool smooth = false) const;
};
// HD sheet (folha_hd.png) redrawn by the player: cuts it back into the tile PNGs of the pack.
// Returns how many tiles changed; -1 on error
int hd_import_sheet(const std::string& dir, std::string& err);
// tiles in the pack and how many differ from the generated models
void hd_pack_stats(const std::string& dir, int& total, int& redrawn);
// enlarges an 8x8 tile to 8*scale with xBR (alpha kept sharp)
std::vector<uint32_t> hd_smooth_tile(const uint32_t* px64, int scale);

bool save_png(const std::string& path, const uint32_t* argb, int w, int h);
bool load_png(const std::string& path, std::vector<uint32_t>& argb, int& w, int& h);
