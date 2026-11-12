// A NativeBoy project: one ROM plus everything recovered/generated from it.
#pragma once
#include "../common/settings.h"
#include "../recomp/analysis.h"
#include "toolchain.h"
#include <string>
#include <vector>

enum ProjStep { ST_IMPORTED = 0, ST_DISCOVERED, ST_ANALYZED, ST_GENERATED, ST_BUILT, ST_VERIFIED, ST_EXPORTED };

struct VerifyResult {
    bool ran = false, ok = false, identical = false;
    int frames = 0, diverged_at = -1;
    unsigned long long fb_rom = 0, fb_ram = 0, native_entries = 0;
    int native_instr = 0, native_ram_instr = 0;
    std::vector<std::string> missing;
    std::string serial;
};

struct ReportItem { int level; std::string text; };   // 0 ok, 1 aviso, 2 problema, 3 info
struct Report {
    int status = 3;              // 0 = nativo completo verificado, 1 = nativo parcial, 2 = falha, 3 = ainda não verificado
    std::string title, summary;
    std::vector<ReportItem> items;
};

// Writes a .zip (stored, no compression) with the given (name inside the zip, file contents) entries.
bool write_zip(const std::string& path, const std::vector<std::pair<std::string, std::string>>& files);

struct Project {
    std::string dir, name, exe_name;
    Settings ini;
    std::vector<gb::u8> rom;          // working ROM (may contain the user's modifications)
    gb::RomInfo info;
    nb::Analysis an;
    bool an_valid = false;
    std::vector<gb::u8> trace;
    std::vector<gb::Machine::RamExec> ram;
    VerifyResult verify;
    std::string last_build_error;

    static std::string sanitize(const std::string& s, bool for_exe);
    static bool create_from_rom(const std::string& rom_path, const std::string& projects_root, Project& out, std::string& err);
    bool open(const std::string& dir, std::string& err);
    bool save();
    void backup(const std::string& reason);
    std::vector<std::string> list_backups() const;
    bool restore_backup(const std::string& name, std::string& err);

    std::string path(const std::string& rel) const;
    int step() const { return ini.geti("etapa", 0); }
    void set_step(int s) { ini.seti("etapa", s); save(); }
    std::string exe_path() const;
    bool rom_modified() const;
    bool write_working_rom(const std::vector<gb::u8>& r, const std::string& reason);

    // pipeline (worker thread)
    bool step_discover(int frames, BuildLog& log, std::string& err);
    bool step_analyze(BuildLog& log, std::string& err);
    bool step_generate(BuildLog& log, std::string& err);
    bool step_build(const Toolchain& tc, BuildLog& log, std::string& err);
    bool step_verify(int frames, BuildLog& log, std::string& err);
    bool step_export(const std::string& games_root, const Toolchain& tc, BuildLog& log, std::string& err, std::string& out_dir);
    // single-file version of an exported game folder: a small launcher with the folder packed inside
    // installers: Windows "Instalar <name>.exe" (own installer, per user, with uninstaller) and Linux .deb
    bool make_installer_windows(const std::string& game_dir, const std::string& games_root, const Toolchain& tc, BuildLog& log, std::string& err, std::string& out_file);
    bool make_deb(const std::string& game_dir, const std::string& games_root, BuildLog& log, std::string& err, std::string& out_file);
    bool make_single_exe(const std::string& game_dir, const std::string& games_root, const Toolchain& tc, BuildLog& log, std::string& err, std::string& out_file);

    void merge_trace(const std::vector<gb::u8>& t, const std::vector<gb::Machine::RamExec>& r);
    bool load_trace();
    bool save_trace();
    int add_entries(const std::vector<std::string>& lines);   // "BB:AAAA"
    int import_missing(const std::string& file, std::string& err);
    Report make_report() const;
    // Game icon (256x256 PNG): mods/icone.png or a screen of the game itself; written to build/icone.png
    bool make_icon(std::string& err);
    // Logs, reports and system information for support (never the ROM or the game code)
    bool write_diagnostics_zip(const std::string& out_zip, const std::string& system_info, std::string& err) const;
    bool load_verify();
};

std::string fmt_num(unsigned long long v);
