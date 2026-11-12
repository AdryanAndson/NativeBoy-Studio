// Locates and drives the bundled C++ toolchain (w64devkit on Windows) and SDK.
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

struct Toolchain {
    std::string app_dir;       // NativeBoy Studio install folder
    std::string gxx;           // g++ executable
    std::string bin_dir;       // folder added to PATH
    std::string sdk;           // sdk folder (core/, common/, runtime/, imgui/, stb/, SDL2/)
    std::string rgbds;         // folder with rgbasm/rgblink/rgbfix (optional)
    std::string cache_dir;     // compiled runtime objects
    std::string version;       // "g++ (GCC) 15.2.0"
    bool ok = false;
    std::string problem;       // human readable

    bool cross_win = false;    // Linux Studio building Windows .exe files with MinGW-w64

    bool detect(const std::string& app_dir, const std::string& user_dir);
    // Linux only: a second toolchain that produces Windows games (needs g++-mingw-w64 + sdk/SDL2-win)
    bool detect_cross_windows(const Toolchain& host, const std::string& user_dir);
    bool win_target() const;
    std::string windres() const;     // resource compiler (Windows icon), "" if unknown
    std::string exe_suffix() const;
    std::vector<std::string> runtime_sources() const;   // absolute paths
    std::string include_flags() const;
    std::string link_libs() const;
    std::string sdl_dll() const;
    std::string font_file() const;
    std::vector<std::pair<std::string, std::string>> env() const;   // environment for compiler processes
    std::string platform_name() const;
    bool bundled_sdl() const;      // Linux portable package ships SDL2 in sdk/SDL2
};

struct BuildLog {
    std::function<void(const std::string&)> line;
    std::function<void(float)> progress;
    std::atomic<bool>* cancel = nullptr;
};

// Compiles sources in parallel. Each job: (source, object). Returns false on first error,
// with the compiler output in 'errors'.
bool compile_many(const Toolchain& tc, const std::vector<std::pair<std::string, std::string>>& jobs,
                  const std::string& flags, const std::string& cwd, BuildLog& log, std::string& errors);
bool ensure_runtime_objects(const Toolchain& tc, std::vector<std::string>& objs, BuildLog& log, std::string& errors);
