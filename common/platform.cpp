#include "platform.h"
#ifdef __ANDROID__
#include <android/log.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <thread>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wininet.h>
#else
#include <SDL.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace plat {

static fs::path P(const std::string& s) { return fs::u8path(s); }
static std::string S(const fs::path& p) { auto u = p.u8string(); return std::string(u.begin(), u.end()); }

#ifdef _WIN32
static std::wstring W(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string U(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
#endif

std::string exe_dir() {
#if defined(__ANDROID__)
    // the app's private folder: the game files are copied there from the APK on the first start
    const char* p = SDL_AndroidGetInternalStoragePath();
    return p ? p : ".";
#elif defined(_WIN32)
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return S(fs::path(std::wstring(buf, n)).parent_path());
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    buf[n] = 0;
    return S(fs::path(buf).parent_path());
#endif
}

std::string user_data_dir(const std::string& app) {
#ifdef _WIN32
    wchar_t* p = nullptr;
    std::string base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) { base = U(p); CoTaskMemFree(p); }
    else base = exe_dir();
    std::string d = join(base, app);
#elif defined(__ANDROID__)
    std::string d = join(exe_dir(), app);
#else
    const char* h = getenv("HOME");
    std::string d = join(join(h ? h : ".", ".local/share"), app);
#endif
    make_dirs(d);
    return d;
}

std::string documents_dir() {
#ifdef _WIN32
    wchar_t* p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p))) { std::string r = U(p); CoTaskMemFree(p); return r; }
    return exe_dir();
#else
    // XDG documents folder (e.g. ~/Documentos on a Portuguese system)
    FILE* p = popen("xdg-user-dir DOCUMENTS 2>/dev/null", "r");
    std::string r;
    if (p) { char b[1024]; if (fgets(b, sizeof b, p)) r = b; pclose(p); }
    while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) r.pop_back();
    const char* h = getenv("HOME");
    if (r.empty() || (h && r == h)) r = std::string(h ? h : ".") + "/Documentos";
    return r;
#endif
}

std::string desktop_dir() {
#ifdef _WIN32
    wchar_t* p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &p))) { std::string r = U(p); CoTaskMemFree(p); return r; }
    return "";
#else
    FILE* p = popen("xdg-user-dir DESKTOP 2>/dev/null", "r");
    std::string r;
    if (p) { char b[1024]; if (fgets(b, sizeof b, p)) r = b; pclose(p); }
    while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) r.pop_back();
    const char* h = getenv("HOME");
    if (r.empty() || (h && r == h)) {
        std::string base = h ? h : ".";
        r = dir_exists(base + "/Área de Trabalho") ? base + "/Área de Trabalho" : base + "/Desktop";
    }
    return r;
#endif
}

std::string start_menu_dir() {
#ifdef _WIN32
    wchar_t* p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &p))) { std::string r = U(p); CoTaskMemFree(p); return r; }
    return "";
#else
    const char* h = getenv("HOME");
    return std::string(h ? h : ".") + "/.local/share/applications";
#endif
}

bool create_shortcut(const std::string& link, const std::string& name, const std::string& target,
                     const std::string& workdir, const std::string& icon, const std::string& comment) {
    make_dirs(parent(link));
#ifdef _WIN32
    (void)name;
    HRESULT hi = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW* sl = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) {
        sl->SetPath(W(target).c_str());
        sl->SetWorkingDirectory(W(workdir).c_str());
        sl->SetDescription(W(comment).c_str());
        sl->SetIconLocation(W(icon.empty() ? target : icon).c_str(), 0);
        IPersistFile* pf = nullptr;
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
            ok = SUCCEEDED(pf->Save(W(link + ".lnk").c_str(), TRUE));
            pf->Release();
        }
        sl->Release();
    }
    if (SUCCEEDED(hi)) CoUninitialize();
    return ok;
#else
    std::string d = "[Desktop Entry]\nType=Application\nName=" + name + "\nComment=" + comment +
                    "\nExec=\"" + target + "\"\nPath=" + workdir + "\nIcon=" + (icon.empty() ? std::string("applications-games") : icon) +
                    "\nTerminal=false\nCategories=Game;\nStartupWMClass=" + file_name(target) + "\n";
    std::string f = link + ".desktop";
    if (!write_text(f, d)) return false;
    set_executable(f);
    // GNOME/Nautilus asks to "trust" launchers on the desktop; mark it as trusted when possible
    std::string cmd = "command -v gio >/dev/null 2>&1 && gio set " + quote(f) + " metadata::trusted true >/dev/null 2>&1";
    if (system(cmd.c_str())) {}
    return true;
#endif
}

bool make_dirs(const std::string& path) { std::error_code ec; fs::create_directories(P(path), ec); return dir_exists(path); }
bool file_exists(const std::string& path) { std::error_code ec; return fs::is_regular_file(P(path), ec); }
bool dir_exists(const std::string& path) { std::error_code ec; return fs::is_directory(P(path), ec); }

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(P(path), std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0) return false;
    f.seekg(0);
    out.resize((size_t)n);
    if (n) f.read((char*)out.data(), n);
    return (bool)f;
}
bool read_text(const std::string& path, std::string& out) {
    std::vector<uint8_t> d;
    if (!read_file(path, d)) return false;
    out.assign(d.begin(), d.end());
    return true;
}
FILE* fopen_utf8(const std::string& path, const char* mode) {
#ifdef _WIN32
    std::wstring m; for (const char* c = mode; *c; c++) m += (wchar_t)*c;
    return _wfopen(P(path).wstring().c_str(), m.c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}

bool write_file(const std::string& path, const void* data, size_t n) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(P(tmp), std::ios::binary);
        if (!f) return false;
        f.write((const char*)data, (std::streamsize)n);
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(P(tmp), P(path), ec);     // atomic replace: a crash never leaves a half-written file
    if (ec) { fs::remove(P(path), ec); fs::rename(P(tmp), P(path), ec); }
    return !ec;
}
bool write_text(const std::string& path, const std::string& s) { return write_file(path, s.data(), s.size()); }
bool copy_file(const std::string& from, const std::string& to) {
    std::error_code ec;
    fs::copy_file(P(from), P(to), fs::copy_options::overwrite_existing, ec);
    return !ec;
}
bool move_file(const std::string& from, const std::string& to) {
    std::error_code ec;
    fs::remove(P(to), ec);
    fs::rename(P(from), P(to), ec);
    return !ec;
}
bool remove_file(const std::string& path) { std::error_code ec; return fs::remove(P(path), ec); }
void set_executable(const std::string& path) {
#ifndef _WIN32
    std::error_code ec;
    fs::permissions(P(path), fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#else
    (void)path;
#endif
}
bool copy_tree(const std::string& from, const std::string& to) {
    std::error_code ec;
    fs::create_directories(P(to), ec);
    fs::copy(P(from), P(to), fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    return !ec;
}
bool remove_tree(const std::string& path) { std::error_code ec; fs::remove_all(P(path), ec); return !ec; }
std::vector<std::string> list_dir(const std::string& path, bool dirs) {
    std::vector<std::string> r;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(P(path), ec)) {
        if (dirs ? e.is_directory(ec) : e.is_regular_file(ec)) r.push_back(S(e.path().filename()));
    }
    std::sort(r.begin(), r.end());
    return r;
}
std::string join(const std::string& a, const std::string& b) { if (a.empty()) return b; return S((P(a) / P(b)).make_preferred()); }
std::string file_name(const std::string& p) { return S(P(p).filename()); }
std::string stem(const std::string& p) { return S(P(p).stem()); }
std::string parent(const std::string& p) { return S(P(p).parent_path()); }
int64_t file_mtime(const std::string& p) {
    std::error_code ec;
    auto t = fs::last_write_time(P(p), ec);
    if (ec) return 0;
    return (int64_t)t.time_since_epoch().count();
}
int64_t file_time_unix(const std::string& p) {
#ifdef _WIN32
    struct _stat64 st;
    if (_wstat64(W(p).c_str(), &st) != 0) return 0;
#else
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return 0;
#endif
    return (int64_t)st.st_mtime;
}
std::string now_stamp() {
    time_t t = time(nullptr);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char b[32];
    strftime(b, sizeof b, "%Y-%m-%d_%H-%M-%S", &tmv);
    return b;
}
int cpu_count() { unsigned n = std::thread::hardware_concurrency(); return n ? (int)n : 2; }

bool is_ascii(const std::string& s) { for (unsigned char c : s) if (c >= 128) return false; return true; }
std::string tool_path(const std::string& p) {
    if (is_ascii(p)) return p;
#ifdef _WIN32
    std::wstring w = W(p);
    DWORD n = GetShortPathNameW(w.c_str(), nullptr, 0);
    if (n) {
        std::wstring sh(n, 0);
        DWORD m = GetShortPathNameW(w.c_str(), &sh[0], n);
        sh.resize(m);
        std::string r = U(sh);
        if (m && is_ascii(r)) return r;
    }
#endif
    return p;
}

std::string quote(const std::string& s) {
    std::string r = "\"";
    for (char c : s) { if (c == '"') r += "\\\""; else r += c; }
    return r + "\"";
}

#ifdef _WIN32
static std::wstring make_filter(const std::string& desc, const std::string& pat) {
    std::wstring f = W(desc);
    f.push_back(0);
    f += W(pat);
    f.push_back(0);
    f += L"Todos os arquivos";
    f.push_back(0);
    f += L"*.*";
    f.push_back(0);
    f.push_back(0);
    return f;
}
std::string open_file_dialog(const std::string& title, const std::string& desc, const std::string& pat) {
    wchar_t buf[4096] = L"";
    std::wstring filter = make_filter(desc, pat), wt = W(title);
    OPENFILENAMEW of; memset(&of, 0, sizeof of);
    of.lStructSize = sizeof of; of.lpstrFilter = filter.c_str(); of.lpstrFile = buf; of.nMaxFile = 4096;
    of.lpstrTitle = wt.c_str(); of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&of)) return "";
    return U(buf);
}
std::string save_file_dialog(const std::string& title, const std::string& desc, const std::string& pat, const std::string& def) {
    wchar_t buf[4096] = L"";
    std::wstring wd = W(def);
    wcsncpy(buf, wd.c_str(), 4095);
    std::wstring filter = make_filter(desc, pat), wt = W(title);
    OPENFILENAMEW of; memset(&of, 0, sizeof of);
    of.lStructSize = sizeof of; of.lpstrFilter = filter.c_str(); of.lpstrFile = buf; of.nMaxFile = 4096;
    of.lpstrTitle = wt.c_str(); of.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&of)) return "";
    return U(buf);
}
std::string folder_dialog(const std::string& title) {
    std::string result;
    IFileDialog* pfd = nullptr;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileDialog, (void**)&pfd))) {
        DWORD opts; pfd->GetOptions(&opts); pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        std::wstring wt = W(title); pfd->SetTitle(wt.c_str());
        if (SUCCEEDED(pfd->Show(nullptr))) {
            IShellItem* it = nullptr;
            if (SUCCEEDED(pfd->GetResult(&it))) {
                PWSTR p = nullptr;
                if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { result = U(p); CoTaskMemFree(p); }
                it->Release();
            }
        }
        pfd->Release();
    }
    return result;
}
void open_in_explorer(const std::string& path) { ShellExecuteW(nullptr, L"open", W(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
void open_url(const std::string& url) { ShellExecuteW(nullptr, L"open", W(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
void message_box(const std::string& title, const std::string& text, bool error) {
    MessageBoxW(nullptr, W(text).c_str(), W(title).c_str(), MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

ProcResult run(const std::string& cmdline, const std::string& cwd, const std::vector<std::pair<std::string, std::string>>& env) {
    ProcResult r;
    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wrh = nullptr;
    if (!CreatePipe(&rd, &wrh, &sa, 0)) return r;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si; memset(&si, 0, sizeof si);
    si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    si.hStdOutput = wrh; si.hStdError = wrh; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi; memset(&pi, 0, sizeof pi);
    std::wstring wcmd = W(cmdline), wcwd = W(cwd);
    std::vector<wchar_t> cmdbuf(wcmd.begin(), wcmd.end()); cmdbuf.push_back(0);
    // environment block: current env + overrides
    std::vector<wchar_t> envblock;
    if (!env.empty()) {
        wchar_t* cur = GetEnvironmentStringsW();
        for (wchar_t* p = cur; *p; p += wcslen(p) + 1) {
            std::wstring e(p);
            bool overridden = false;
            for (auto& kv : env) {
                std::wstring k = W(kv.first) + L"=";
                if (_wcsnicmp(e.c_str(), k.c_str(), k.size()) == 0) overridden = true;
            }
            if (!overridden) { envblock.insert(envblock.end(), e.begin(), e.end()); envblock.push_back(0); }
        }
        FreeEnvironmentStringsW(cur);
        for (auto& kv : env) { std::wstring e = W(kv.first + "=" + kv.second); envblock.insert(envblock.end(), e.begin(), e.end()); envblock.push_back(0); }
        envblock.push_back(0);
    }
    BOOL ok = CreateProcessW(nullptr, cmdbuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                             env.empty() ? nullptr : envblock.data(), cwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    CloseHandle(wrh);
    if (!ok) { CloseHandle(rd); r.output = "Não foi possível iniciar: " + cmdline; return r; }
    r.started = true;
    char buf[4096]; DWORD n;
    while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n) r.output.append(buf, n);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    r.code = (int)code;
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
    return r;
}
bool launch(const std::string& exe, const std::string& args, const std::string& cwd) {
    HINSTANCE h = ShellExecuteW(nullptr, L"open", W(exe).c_str(), W(args).c_str(), W(cwd).c_str(), SW_SHOWNORMAL);
    return (INT_PTR)h > 32;
}
bool http_request(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                  const std::string& body, HttpResult& res, std::string& err, bool follow) {
    res = HttpResult();
    std::string cur = url;
    for (int hop = 0; hop < 5; hop++) {
        URL_COMPONENTSW uc; memset(&uc, 0, sizeof uc); uc.dwStructSize = sizeof uc;
        wchar_t host[256] = {0}, path[4096] = {0};
        uc.lpszHostName = host; uc.dwHostNameLength = 255; uc.lpszUrlPath = path; uc.dwUrlPathLength = 4095;
        wchar_t extra[4096] = {0}; uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 4095;
        std::wstring wurl = W(cur);
        if (!InternetCrackUrlW(wurl.c_str(), 0, 0, &uc)) { err = "Endereço inválido: " + cur; return false; }
        HINTERNET in = InternetOpenW(L"NativeBoyStudio", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!in) { err = "Sem acesso à internet."; return false; }
        HINTERNET con = InternetConnectW(in, host, uc.nPort, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
        std::wstring target = std::wstring(path) + extra;
        const bool first = hop == 0;
        HINTERNET rq = con ? HttpOpenRequestW(con, W(first ? method : std::string("GET")).c_str(), target.c_str(), nullptr, nullptr, nullptr,
                                              INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_AUTO_REDIRECT | INTERNET_FLAG_NO_COOKIES |
                                              (uc.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_FLAG_SECURE : 0), 0) : nullptr;
        if (!rq) { if (con) InternetCloseHandle(con); InternetCloseHandle(in); err = "Não foi possível conectar a " + cur; return false; }
        std::wstring hdr;
        if (first) for (auto& h : headers) hdr += W(h) + L"\r\n";       // (not sent to the site of a redirect)
        const bool send_body = first && !body.empty();
        BOOL ok = HttpSendRequestW(rq, hdr.empty() ? nullptr : hdr.c_str(), (DWORD)-1L, send_body ? (LPVOID)body.data() : nullptr, send_body ? (DWORD)body.size() : 0);
        if (!ok) { InternetCloseHandle(rq); InternetCloseHandle(con); InternetCloseHandle(in); err = "Sem resposta de " + cur + " (sem internet?)"; return false; }
        DWORD code = 0, len = sizeof code;
        HttpQueryInfoW(rq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &len, nullptr);
        res.status = (int)code;
        wchar_t loc[4096] = {0}; DWORD ll = sizeof loc;
        res.location.clear();
        if (HttpQueryInfoW(rq, HTTP_QUERY_LOCATION, loc, &ll, nullptr)) res.location = U(loc);
        res.body.clear();
        char buf[16384]; DWORD n;
        while (InternetReadFile(rq, buf, sizeof buf, &n) && n) res.body.append(buf, n);
        InternetCloseHandle(rq); InternetCloseHandle(con); InternetCloseHandle(in);
        if (follow && code >= 300 && code < 400 && !res.location.empty()) { cur = res.location; continue; }
        return true;
    }
    err = "Redirecionamentos demais em " + url;
    return false;
}
bool http_get(const std::string& url, std::string& body, std::string& err) {
    HINTERNET in = InternetOpenW(L"NativeBoyStudio", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!in) { err = "Sem acesso à internet."; return false; }
    HINTERNET h = InternetOpenUrlW(in, W(url).c_str(), nullptr, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_SECURE, 0);
    if (!h) { InternetCloseHandle(in); err = "Não foi possível acessar " + url; return false; }
    char buf[4096]; DWORD n;
    while (InternetReadFile(h, buf, sizeof buf, &n) && n) body.append(buf, n);
    InternetCloseHandle(h); InternetCloseHandle(in);
    return true;
}
#else
static std::vector<std::string> split_lines_plat(const std::string& t) {
    std::vector<std::string> out; std::string cur;
    for (char c : t) { if (c == '\n') { if (!cur.empty() && cur.back() == '\r') cur.pop_back(); out.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
static std::string capture(const std::string& cmd) {
    FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!p) return "";
    char buf[4096]; std::string r;
    while (fgets(buf, sizeof buf, p)) r += buf;
    pclose(p);
    while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) r.pop_back();
    return r;
}
static bool have(const char* prog) { return system((std::string("command -v ") + prog + " >/dev/null 2>&1").c_str()) == 0; }
// "*.gb;*.gbc" -> "*.gb *.gbc"
static std::string pats(const std::string& p) { std::string r = p; for (auto& c : r) if (c == ';') c = ' '; return r; }
std::string open_file_dialog(const std::string& title, const std::string& desc, const std::string& pat) {
    if (have("zenity"))
        return capture("zenity --file-selection --title=" + quote(title) + " --file-filter=" + quote(desc + " | " + pats(pat)) + " --file-filter=" + quote("Todos os arquivos | *"));
    if (have("kdialog")) return capture("kdialog --getopenfilename ~ " + quote(pats(pat) + "|" + desc) + " --title " + quote(title));
    message_box(title, "Instale o zenity para usar as janelas de seleção de arquivo:\n  sudo apt install zenity\n\nVocê também pode arrastar o arquivo para a janela.", true);
    return "";
}
std::string save_file_dialog(const std::string& title, const std::string& desc, const std::string& pat, const std::string& def) {
    if (have("zenity"))
        return capture("zenity --file-selection --save --confirm-overwrite --title=" + quote(title) + " --filename=" + quote(def) + " --file-filter=" + quote(desc + " | " + pats(pat)));
    if (have("kdialog")) return capture("kdialog --getsavefilename " + quote(def) + " " + quote(pats(pat) + "|" + desc) + " --title " + quote(title));
    message_box(title, "Instale o zenity: sudo apt install zenity", true);
    return "";
}
std::string folder_dialog(const std::string& title) {
    if (have("zenity")) return capture("zenity --file-selection --directory --title=" + quote(title));
    if (have("kdialog")) return capture("kdialog --getexistingdirectory ~ --title " + quote(title));
    message_box(title, "Instale o zenity: sudo apt install zenity", true);
    return "";
}
void open_in_explorer(const std::string& path) { std::string c = "xdg-open " + quote(path) + " >/dev/null 2>&1 &"; if (system(c.c_str())) {} }
void open_url(const std::string& url) { open_in_explorer(url); }
void message_box(const std::string& title, const std::string& text, bool error) {
    fprintf(stderr, "[%s] %s\n", title.c_str(), text.c_str());
#ifdef __ANDROID__
    __android_log_print(error ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "NativeBoy", "[%s] %s", title.c_str(), text.c_str());
#endif
    SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION, title.c_str(), text.c_str(), nullptr);
}
ProcResult run(const std::string& cmdline, const std::string& cwd, const std::vector<std::pair<std::string, std::string>>& env) {
    ProcResult r;
    std::string full;
    for (auto& kv : env) full += kv.first + "=" + quote(kv.second) + " ";
    full = (cwd.empty() ? "" : "cd " + quote(cwd) + " && ") + full + cmdline + " 2>&1";
    FILE* p = popen(full.c_str(), "r");
    if (!p) return r;
    r.started = true;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) r.output.append(buf, n);
    int st = pclose(p);
    r.code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return r;
}
bool launch(const std::string& exe, const std::string& args, const std::string& cwd) {
    std::string c = "cd " + quote(cwd) + " && setsid " + quote(exe) + " " + args + " >/dev/null 2>&1 &";
    return system(c.c_str()) == 0;
}
bool http_request(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                  const std::string& body, HttpResult& res, std::string& err, bool follow) {
    res = HttpResult();
    // curl: the headers (with the access key) go in a private file, not on the command line where other users could see them
    char tdir[] = "/tmp/nbhttpXXXXXX";
    if (!mkdtemp(tdir)) { err = "Não foi possível criar um arquivo temporário."; return false; }
    const std::string d = tdir, hf = d + "/h", bf = d + "/b", of = d + "/o", lf = d + "/l";
    std::string hs; for (auto& h : headers) hs += h + "\n";
    write_text(hf, hs);
    chmod(hf.c_str(), 0600);
    write_file(bf, body.data(), body.size());
    std::string cmd = "curl -sS --max-time 900 -X " + quote(method) + " -H @" + quote(hf) + " -o " + quote(of) + " -D " + quote(lf) + " -w '%{http_code}'";
    if (!body.empty() || method == "POST" || method == "PUT" || method == "PATCH") cmd += " --data-binary @" + quote(bf);
    if (follow) cmd += " -L";
    cmd += " " + quote(url);
    ProcResult r = run(cmd, "");
    read_text(of, res.body);
    std::string hdrs; read_text(lf, hdrs);
    remove_tree(d);
    if (r.code != 0) {
        err = r.code == 127 || r.output.find("not found") != std::string::npos ? "O programa curl não está instalado (sudo apt install curl)." : "Sem resposta de " + url + " (" + r.output.substr(0, 200) + ")";
        return false;
    }
    std::string code = r.output; while (!code.empty() && !isdigit((unsigned char)code.back())) code.pop_back();
    res.status = atoi(code.substr(code.size() >= 3 ? code.size() - 3 : 0).c_str());
    for (auto& line : split_lines_plat(hdrs)) {
        if (line.size() > 9 && (line.compare(0, 9, "location:") == 0 || line.compare(0, 9, "Location:") == 0)) {
            res.location = line.substr(9);
            while (!res.location.empty() && (res.location[0] == ' ')) res.location.erase(0, 1);
            while (!res.location.empty() && (res.location.back() == '\r' || res.location.back() == ' ')) res.location.pop_back();
        }
    }
    return true;
}
bool http_get(const std::string& url, std::string& body, std::string& err) {
    ProcResult r = run("curl -fsSL --max-time 20 " + quote(url), "");
    if (r.code != 0) { err = "Não foi possível acessar " + url + " (o curl está instalado?)"; return false; }
    body = r.output;
    return true;
}
#endif

} // namespace plat

std::string plat::user_data_dir() {
#ifdef _WIN32
    const wchar_t* a = _wgetenv(L"APPDATA");
    if (a && *a) return U(a);
    return join(documents_dir(), "..");
#else
    const char* x = getenv("XDG_DATA_HOME");
    if (x && *x) return x;
    const char* h = getenv("HOME");
    return std::string(h ? h : "/tmp") + "/.local/share";
#endif
}
