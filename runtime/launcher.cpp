// NativeBoy - single-file game launcher.
// The exported game folder (program, SDL2, assets, default settings) is appended to this small
// program as a compressed pack. On start it unpacks the game once into a per-game data folder
// (again only when the pack changes) and runs it from there; saves and settings stay in that folder.
//
// Pack layout (at the end of the file):
//   [zlib streams...] [index] [trailer: "NBPACK01" u64 index_offset u64 index_size u64 pack_id]
//   index: u32 count, utf8 game id (u16 len + bytes), utf8 program name (u16 len + bytes),
//          then per file: u16 name_len, name (utf8, '/' separators), u64 offset, u64 packed, u64 size, u8 flags
//   flags: 1 = user file (only written when missing: the player's settings)
//
// Portable mode: if a folder named "<file name> - dados" exists next to this file, it is used
// instead of the user's data folder (for pen drives).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#endif

namespace {

#ifdef _WIN32
std::wstring wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, 0);
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
std::string utf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, 0);
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}
const char SEP = '\\';
#else
const char SEP = '/';
#endif

void fail(const std::string& msg) {
#ifdef _WIN32
    MessageBoxW(nullptr, wide(msg).c_str(), L"NativeBoy", MB_ICONERROR | MB_OK);
#else
    fprintf(stderr, "NativeBoy: %s\n", msg.c_str());
    std::string cmd = "command -v zenity >/dev/null 2>&1 && zenity --error --title=NativeBoy --text=\"" + msg + "\" >/dev/null 2>&1";
    if (system(cmd.c_str())) {}
#endif
    exit(1);
}

std::string self_path() {
#ifdef _WIN32
    std::wstring b(32768, 0);
    DWORD n = GetModuleFileNameW(nullptr, &b[0], (DWORD)b.size());
    b.resize(n);
    return utf8(b);
#else
    char b[PATH_MAX + 1];
    ssize_t n = readlink("/proc/self/exe", b, PATH_MAX);
    if (n <= 0) return "";
    b[n] = 0;
    return b;
#endif
}

FILE* open_file(const std::string& p, const char* mode) {
#ifdef _WIN32
    return _wfopen(wide(p).c_str(), wide(mode).c_str());
#else
    return fopen(p.c_str(), mode);
#endif
}

bool exists(const std::string& p) {
#ifdef _WIN32
    return GetFileAttributesW(wide(p).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st; return stat(p.c_str(), &st) == 0;
#endif
}

bool is_dir(const std::string& p) {
#ifdef _WIN32
    DWORD a = GetFileAttributesW(wide(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

void make_dirs(const std::string& p) {
    for (size_t i = 1; i <= p.size(); i++) {
        if (i < p.size() && p[i] != '/' && p[i] != '\\') continue;
        std::string d = p.substr(0, i);
        if (d.empty() || (d.size() == 2 && d[1] == ':')) continue;
#ifdef _WIN32
        CreateDirectoryW(wide(d).c_str(), nullptr);
#else
        mkdir(d.c_str(), 0755);
#endif
    }
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    std::string r = a;
    if (r.back() != '/' && r.back() != '\\') r += SEP;
    for (char c : b) r += (c == '/') ? SEP : c;
    return r;
}

std::string data_root() {
#ifdef _WIN32
    PWSTR p = nullptr;
    std::string r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) r = utf8(p);
    if (p) CoTaskMemFree(p);
    if (r.empty()) { const char* t = getenv("TEMP"); r = t ? t : "."; }
    return join(r, "NativeBoy/Jogos");
#else
    const char* x = getenv("XDG_DATA_HOME");
    std::string base = (x && *x) ? std::string(x) : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.local/share";
    return join(base, "nativeboy/jogos");
#endif
}

struct Reader {
    const std::vector<char>& d; size_t p;
    bool ok = true;
    template <class T> T get() { T v{}; if (p + sizeof(T) > d.size()) { ok = false; return v; } memcpy(&v, &d[p], sizeof(T)); p += sizeof(T); return v; }
    std::string str() { uint16_t n = get<uint16_t>(); if (p + n > d.size()) { ok = false; return ""; } std::string s(&d[p], n); p += n; return s; }
};

struct Entry { std::string name; uint64_t off, packed, size; uint8_t flags; };

} // namespace

int main(int argc, char** argv) {
    std::string me = self_path();
    if (me.empty()) fail("Não foi possível localizar o próprio executável.");
    FILE* f = open_file(me, "rb");
    if (!f) fail("Não foi possível ler " + me);
    fseek(f, 0, SEEK_END);
    long long fsize = ftell(f);
    char tr[32];
    if (fsize < 32 || fseek(f, (long)(fsize - 32), SEEK_SET) || fread(tr, 1, 32, f) != 32 || memcmp(tr, "NBPACK01", 8))
        fail("Este executável está incompleto (os dados do jogo não foram encontrados). Exporte o jogo novamente.");
    uint64_t ioff, isize, pid;
    memcpy(&ioff, tr + 8, 8); memcpy(&isize, tr + 16, 8); memcpy(&pid, tr + 24, 8);
    if (ioff + isize > (uint64_t)fsize) fail("Dados do jogo corrompidos.");
    std::vector<char> idx(isize);
    fseek(f, (long)ioff, SEEK_SET);
    if (fread(idx.data(), 1, isize, f) != isize) fail("Dados do jogo corrompidos.");
    Reader rd{idx, 0};
    uint32_t count = rd.get<uint32_t>();
    std::string id = rd.str(), program = rd.str();
    std::vector<Entry> files;
    for (uint32_t i = 0; i < count && rd.ok; i++) {
        Entry e; e.name = rd.str(); e.off = rd.get<uint64_t>(); e.packed = rd.get<uint64_t>(); e.size = rd.get<uint64_t>(); e.flags = rd.get<uint8_t>();
        files.push_back(e);
    }
    if (!rd.ok || id.empty() || program.empty()) fail("Dados do jogo corrompidos.");

    // where the game lives: a portable folder next to this file, or the user's data folder
    std::string dir;
    {
        std::string base = me;
        size_t sl = base.find_last_of("/\\");
        std::string folder = sl == std::string::npos ? "." : base.substr(0, sl), fname = sl == std::string::npos ? base : base.substr(sl + 1);
        size_t dot = fname.rfind('.');
        std::string stem = dot == std::string::npos ? fname : fname.substr(0, dot);
        std::string portable = join(folder, stem + " - dados");
        dir = is_dir(portable) ? portable : join(data_root(), id);
    }
    make_dirs(dir);
    char want[32]; snprintf(want, sizeof want, "%016llx", (unsigned long long)pid);
    std::string have;
    if (FILE* v = open_file(join(dir, ".pacote"), "rb")) { char b[32] = {0}; size_t n = fread(b, 1, 31, v); have.assign(b, n); fclose(v); }
    if (have != want || !exists(join(dir, program))) {
        // first start or a new version of the game: unpack (the player's own settings are kept)
        std::vector<char> packed, data;
        for (auto& e : files) {
            std::string out = join(dir, e.name);
            if ((e.flags & 1) && exists(out)) continue;
            size_t sl = out.find_last_of("/\\");
            if (sl != std::string::npos) make_dirs(out.substr(0, sl));
            packed.resize(e.packed); data.resize(e.size);
            fseek(f, (long)e.off, SEEK_SET);
            if (fread(packed.data(), 1, e.packed, f) != e.packed) fail("Dados do jogo corrompidos (" + e.name + ").");
            if (e.size && stbi_zlib_decode_buffer(data.data(), (int)e.size, packed.data(), (int)e.packed) != (int)e.size)
                fail("Dados do jogo corrompidos (" + e.name + ").");
            std::string tmp = out + ".novo";
            FILE* o = open_file(tmp, "wb");
            if (!o) fail("Não foi possível gravar em " + dir + "\n(o jogo já está aberto?)");
            bool w_ok = e.size == 0 || fwrite(data.data(), 1, e.size, o) == e.size;
            fclose(o);
            if (!w_ok) fail("Sem espaço para instalar o jogo em " + dir);
#ifdef _WIN32
            if (!MoveFileExW(wide(tmp).c_str(), wide(out).c_str(), MOVEFILE_REPLACE_EXISTING))
                fail("Não foi possível atualizar " + out + "\nFeche o jogo se ele estiver aberto e tente de novo.");
#else
            if (rename(tmp.c_str(), out.c_str())) fail("Não foi possível atualizar " + out);
            if (e.name == program || e.name.find(".so") != std::string::npos) chmod(out.c_str(), 0755);
#endif
        }
        if (FILE* v = open_file(join(dir, ".pacote"), "wb")) { fputs(want, v); fclose(v); }
    }
    fclose(f);

    std::string exe = join(dir, program);
#ifdef _WIN32
    std::wstring cmd = L"\"" + wide(exe) + L"\"";
    for (int i = 1; i < argc; i++) cmd += L" \"" + wide(argv[i]) + L"\"";
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessW(wide(exe).c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, wide(dir).c_str(), &si, &pi))
        fail("Não foi possível iniciar o jogo (" + exe + ").");
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)code;
#else
    if (chdir(dir.c_str())) {}
    std::vector<char*> args;
    args.push_back(const_cast<char*>(exe.c_str()));
    for (int i = 1; i < argc; i++) args.push_back(argv[i]);
    args.push_back(nullptr);
    execv(exe.c_str(), args.data());
    fail("Não foi possível iniciar o jogo (" + exe + ").");
    return 1;
#endif
}
