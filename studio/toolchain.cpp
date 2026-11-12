#include "toolchain.h"
#include "../common/platform.h"
#include "../common/settings.h"
#include <mutex>
#include <thread>

bool Toolchain::detect(const std::string& app, const std::string& user_dir) {
    app_dir = app;
    ok = false; problem.clear();
#ifdef _WIN32
    bin_dir = plat::join(app, "toolchain/w64devkit/bin");
    gxx = plat::join(bin_dir, "g++.exe");
    if (!plat::is_ascii(app)) { bin_dir = plat::tool_path(bin_dir); gxx = plat::join(bin_dir, "g++.exe"); }
    rgbds = plat::join(app, "toolchain/rgbds");
#else
    bin_dir = "";
    gxx = "g++";
    rgbds = plat::join(app, "toolchain/rgbds");
#endif
    sdk = plat::join(app, "sdk");
#ifdef _WIN32
    if (!plat::file_exists(gxx)) {
        problem = "O compilador C++ integrado não foi encontrado em\n" + gxx +
                  "\nReinstale o NativeBoy Studio (o instalador inclui o compilador) ou use Ferramentas > Reparar.";
        return false;
    }
#else
    if (plat::run("command -v g++", "").code != 0) {
        problem = "O compilador C++ (g++) não está instalado. Ele é necessário para converter jogos.\nAbra o terminal e execute:\n  sudo apt install g++";
        return false;
    }
    if (!bundled_sdl() && !plat::file_exists("/usr/include/SDL2/SDL.h")) {
        problem = "A biblioteca SDL2 para desenvolvimento não está instalada.\nAbra o terminal e execute:\n  sudo apt install libsdl2-dev";
        return false;
    }
#endif
    if (!plat::file_exists(plat::join(sdk, "core/gb.h"))) {
        problem = "Os arquivos do SDK do NativeBoy não foram encontrados em " + sdk + ". Reinstale o programa.";
        return false;
    }
    plat::ProcResult r = plat::run(plat::quote(gxx) + " --version", app, env());
    if (r.code != 0) { problem = "O compilador não pôde ser executado:\n" + r.output; return false; }
    version = r.output.substr(0, r.output.find('\n'));
    std::string sdkver;
    plat::read_text(plat::join(sdk, "VERSION"), sdkver);
    uint32_t h = 2166136261u;
    for (char c : version + trim(sdkver)) { h ^= (uint8_t)c; h *= 16777619u; }
    char b[32]; snprintf(b, sizeof b, "runtime-%08x", h);
    cache_dir = plat::join(plat::join(user_dir, "cache"), b);
    plat::make_dirs(cache_dir);
    ok = true;
    return true;
}

std::string Toolchain::exe_suffix() const { return win_target() ? ".exe" : ""; }

std::vector<std::string> Toolchain::runtime_sources() const {
    std::vector<std::string> v;
    for (auto f : {"core/machine.cpp", "core/apu.cpp", "core/sgb.cpp", "core/interp.cpp", "common/platform.cpp", "common/settings.cpp",
                   "common/video.cpp", "common/ui_common.cpp", "common/player.cpp", "common/player_extras.cpp", "common/console.cpp", "common/music.cpp", "common/recorder.cpp", "common/link.cpp", "runtime/main.cpp", "runtime/selftest.cpp",
                   "imgui/imgui.cpp", "imgui/imgui_draw.cpp", "imgui/imgui_tables.cpp", "imgui/imgui_widgets.cpp",
                   "imgui/backends/imgui_impl_sdl2.cpp", "imgui/backends/imgui_impl_sdlrenderer2.cpp"})
        v.push_back(plat::join(sdk, f));
    return v;
}

bool Toolchain::win_target() const {
#ifdef _WIN32
    return true;
#else
    return cross_win;
#endif
}

bool Toolchain::detect_cross_windows(const Toolchain& host, const std::string& user_dir) {
    *this = host;
    cross_win = true; ok = false; problem.clear();
#ifdef _WIN32
    (void)user_dir;
    problem = "Não disponível no Windows (o Studio do Windows já gera .exe).";
    return false;
#else
    gxx.clear();
    for (auto c : {"x86_64-w64-mingw32-g++-posix", "x86_64-w64-mingw32-g++"})
        if (plat::run(std::string("command -v ") + c, "").code == 0) { gxx = c; break; }
    if (gxx.empty()) {
        problem = "Para gerar também a versão Windows, instale o compilador MinGW-w64:\n  sudo apt install g++-mingw-w64-x86-64-posix";
        return false;
    }
    if (!plat::file_exists(plat::join(sdk, "SDL2-win/lib/libSDL2.dll.a"))) { problem = "Arquivos do SDL2 para Windows ausentes em sdk/SDL2-win. Reinstale o NativeBoy Studio."; return false; }
    plat::ProcResult r = plat::run(plat::quote(gxx) + " --version", app_dir, env());
    if (r.code != 0) { problem = "O compilador MinGW-w64 não pôde ser executado:\n" + r.output; return false; }
    version = r.output.substr(0, r.output.find('\n'));
    std::string sdkver;
    plat::read_text(plat::join(sdk, "VERSION"), sdkver);
    uint32_t h = 2166136261u;
    for (char c : version + trim(sdkver) + "win") { h ^= (uint8_t)c; h *= 16777619u; }
    char b[40]; snprintf(b, sizeof b, "runtime-win-%08x", h);
    cache_dir = plat::join(plat::join(user_dir, "cache"), b);
    plat::make_dirs(cache_dir);
    ok = true;
    return true;
#endif
}

std::string Toolchain::windres() const {
    if (!win_target()) return "";
    std::string g = gxx;
    size_t p = g.rfind("g++");
    if (p == std::string::npos) return "";
    return g.substr(0, p) + "windres" + (g.size() >= 4 && g.compare(g.size() - 4, 4, ".exe") == 0 ? ".exe" : "");
}

std::string Toolchain::include_flags() const {
    std::string s = plat::tool_path(sdk);
    std::string base = "-I" + plat::quote(plat::join(s, "core")) + " -I" + plat::quote(plat::join(s, "common")) +
           " -I" + plat::quote(plat::join(s, "imgui")) + " -I" + plat::quote(plat::join(s, "imgui/backends")) +
           " -I" + plat::quote(plat::join(s, "stb"));
#ifdef _WIN32
    return base + " -I" + plat::quote(plat::join(s, "SDL2/include/SDL2")) + " -DSDL_MAIN_HANDLED";
#else
    if (cross_win) return base + " -I" + plat::quote(plat::join(s, "SDL2-win/include/SDL2")) + " -DSDL_MAIN_HANDLED";
    return base + (bundled_sdl() ? " -I" + plat::quote(plat::join(s, "SDL2/include/SDL2")) + " -I" + plat::quote(plat::join(s, "SDL2/arch"))
                                 : std::string(" -I/usr/include/SDL2")) + " -D_REENTRANT -DSDL_MAIN_HANDLED";
#endif
}

std::string Toolchain::link_libs() const {
    static const char* WIN_LIBS = " -static -static-libgcc -static-libstdc++ -mwindows -lcomdlg32 -lole32 -lshell32 -lwininet -luuid -lws2_32";
#ifdef _WIN32
    return plat::quote(plat::join(plat::tool_path(sdk), "SDL2/lib/libSDL2.dll.a")) + WIN_LIBS;
#else
    if (cross_win) return plat::quote(plat::join(sdk, "SDL2-win/lib/libSDL2.dll.a")) + WIN_LIBS;
    // libstdc++ is linked statically and SDL2 is shipped next to the game ($ORIGIN),
    // so exported games also run on machines without the development packages
    std::string sdl = bundled_sdl() ? plat::quote(plat::join(sdk, "SDL2/lib/libSDL2-2.0.so.0")) : std::string("-lSDL2");
    return sdl + " -lpthread -static-libstdc++ -static-libgcc -Wl,-rpath,'$ORIGIN'";
#endif
}
std::string Toolchain::sdl_dll() const {
#ifdef _WIN32
    return plat::join(sdk, "SDL2/bin/SDL2.dll");
#else
    if (cross_win) return plat::join(sdk, "SDL2-win/bin/SDL2.dll");
    if (bundled_sdl()) return plat::join(sdk, "SDL2/lib/libSDL2-2.0.so.0");
    for (auto c : {"/usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0", "/usr/lib64/libSDL2-2.0.so.0", "/usr/lib/libSDL2-2.0.so.0"})
        if (plat::file_exists(c)) return c;
    return "";
#endif
}
bool Toolchain::bundled_sdl() const {
#ifdef _WIN32
    return false;
#else
    return plat::file_exists(plat::join(sdk, "SDL2/include/SDL2/SDL.h")) && plat::file_exists(plat::join(sdk, "SDL2/lib/libSDL2-2.0.so.0"));
#endif
}
std::string Toolchain::platform_name() const {
    return win_target() ? "Windows x64" : "Linux x64";
}
std::vector<std::pair<std::string, std::string>> Toolchain::env() const {
#ifdef _WIN32
    return {{"PATH", bin_dir}};
#else
    return {};
#endif
}
std::string Toolchain::font_file() const { return plat::join(app_dir, "assets/fonts/DejaVuSans.ttf"); }

bool compile_many(const Toolchain& tc, const std::vector<std::pair<std::string, std::string>>& jobs,
                  const std::string& flags, const std::string& cwd, BuildLog& log, std::string& errors) {
    std::mutex mu;
    size_t next = 0, done = 0;
    bool failed = false;
    int workers = std::max(1, std::min(plat::cpu_count(), 8));
    auto worker = [&]() {
        for (;;) {
            size_t i;
            {
                std::lock_guard<std::mutex> lk(mu);
                if (failed || next >= jobs.size() || (log.cancel && *log.cancel)) return;
                i = next++;
            }
            const auto& j = jobs[i];
            // sources/objects are given relative to cwd whenever possible: the bundled GCC
            // cannot open paths with characters outside the ANSI code page
            auto rel = [&](const std::string& p) {
                auto under = [](const std::string& p, const std::string& base) {
                    return !base.empty() && p.size() > base.size() + 1 && p.compare(0, base.size(), base) == 0 && (p[base.size()] == '/' || p[base.size()] == '\\');
                };
                if (under(p, cwd)) return p.substr(cwd.size() + 1);
                std::string up = plat::parent(cwd);
                if (under(p, up)) return "../" + p.substr(up.size() + 1);
                return plat::tool_path(p);
            };
            std::string out_rel = rel(j.second);
            std::string cmd = plat::quote(tc.gxx) + " " + flags + " -c " + plat::quote(rel(j.first)) + " -o " + plat::quote(out_rel + ".part");
            plat::ProcResult r = plat::run(cmd, cwd, tc.env());
            if (r.code == 0 && !plat::move_file(j.second + ".part", j.second)) { r.code = 1; r.output += "\nNão foi possível gravar " + j.second; }
            std::lock_guard<std::mutex> lk(mu);
            done++;
            if (log.line) log.line("  compilado: " + plat::file_name(j.first) + (r.code == 0 ? "" : "  [ERRO]"));
            if (log.progress) log.progress((float)done / jobs.size());
            if (r.code != 0) {
                failed = true;
                errors += "Erro ao compilar " + plat::file_name(j.first) + ":\n" + (r.started ? r.output : "o compilador não pôde ser iniciado") + "\n";
            }
        }
    };
    std::vector<std::thread> th;
    for (int i = 0; i < workers; i++) th.emplace_back(worker);
    for (auto& t : th) t.join();
    if (log.cancel && *log.cancel) { errors += "Cancelado pelo usuário.\n"; return false; }
    return !failed;
}

bool ensure_runtime_objects(const Toolchain& tc, std::vector<std::string>& objs, BuildLog& log, std::string& errors) {
    std::vector<std::pair<std::string, std::string>> jobs;
    objs.clear();
    for (auto& src : tc.runtime_sources()) {
        std::string rel = src.substr(tc.sdk.size() + 1);
        std::string name;
        for (char c : rel) name += (c == '/' || c == '\\') ? '_' : c;
        std::string obj = plat::join(tc.cache_dir, name + ".o");
        objs.push_back(obj);
        if (!plat::file_exists(obj)) jobs.push_back({src, obj});
    }
    if (jobs.empty()) { if (log.line) log.line("Runtime já compilado (cache)."); return true; }
    if (log.line) log.line("Compilando o runtime do NativeBoy pela primeira vez (" + std::to_string(jobs.size()) + " arquivos, só acontece uma vez)...");
    return compile_many(tc, jobs, "-O2 -std=c++17 -w " + tc.include_flags(), tc.cache_dir, log, errors);
}
