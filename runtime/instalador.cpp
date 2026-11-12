// NativeBoy - Windows installer for one exported game.
// The game folder is appended to this program as a compressed pack (same idea as launcher.cpp).
//
//   Instalar Jogo.exe              installer window (folder, shortcuts, progress)
//   Instalar Jogo.exe /S [/D=dir]  silent install (tests, scripts)
//   Desinstalar.exe /desinstalar   uninstaller (copied into the game folder, without the pack)
//
// The game is installed per user (no administrator rights): %LOCALAPPDATA%\Programs\<name>, with
// Start Menu and Desktop shortcuts and an entry in "Installed apps" (HKCU ...\Uninstall\NativeBoy.<id>).
// Updating keeps the player's settings; uninstalling keeps saves and captures unless asked otherwise.
//
// Pack layout (end of file): [zlib streams] [index] [trailer: "NBINST01" u64 index_offset u64 index_size u64 pack_id]
//   index: u32 count, str id, str program, str name, str version, u64 stub_size,
//          per file: str name, u64 offset, u64 packed, u64 size, u8 flags (1 = only when missing)
//   str = u16 length + utf8 bytes
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <objbase.h>

namespace {

std::wstring W(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, 0);
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
std::string U(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, 0);
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

struct Entry { std::wstring name; uint64_t off, packed, size; uint8_t flags; };
struct Pack {
    std::wstring self, id, program, name, version;
    uint64_t stub = 0, total = 0;
    std::vector<Entry> files;
};

bool silent = false;

void say(const std::wstring& msg, bool error) {
    if (!silent) MessageBoxW(nullptr, msg.c_str(), L"Instalador", (error ? MB_ICONERROR : MB_ICONINFORMATION) | MB_OK);
    else fwprintf(stderr, L"%ls\n", msg.c_str());
}

std::wstring self_path() {
    std::wstring b(32768, 0);
    DWORD n = GetModuleFileNameW(nullptr, &b[0], (DWORD)b.size());
    b.resize(n);
    return b;
}

std::wstring known(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr; std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
    if (p) CoTaskMemFree(p);
    return r;
}

std::wstring join(const std::wstring& a, const std::wstring& b) {
    std::wstring r = a;
    if (!r.empty() && r.back() != L'\\' && r.back() != L'/') r += L'\\';
    for (wchar_t c : b) r += c == L'/' ? L'\\' : c;
    return r;
}

void make_dirs(const std::wstring& p) {
    for (size_t i = 3; i <= p.size(); i++)
        if (i == p.size() || p[i] == L'\\' || p[i] == L'/') CreateDirectoryW(p.substr(0, i).c_str(), nullptr);
}

bool read_pack(Pack& pk) {
    pk.self = self_path();
    FILE* f = _wfopen(pk.self.c_str(), L"rb");
    if (!f) return false;
    _fseeki64(f, 0, SEEK_END);
    long long fs = _ftelli64(f);
    char tr[32];
    bool ok = fs >= 32 && !_fseeki64(f, fs - 32, SEEK_SET) && fread(tr, 1, 32, f) == 32 && !memcmp(tr, "NBINST01", 8);
    uint64_t io = 0, is = 0;
    if (ok) { memcpy(&io, tr + 8, 8); memcpy(&is, tr + 16, 8); ok = io + is <= (uint64_t)fs; }
    std::vector<char> d;
    if (ok) { d.resize(is); _fseeki64(f, io, SEEK_SET); ok = fread(d.data(), 1, is, f) == is; }
    fclose(f);
    if (!ok) return false;
    size_t p = 0;
    auto get = [&](void* v, size_t n) { if (p + n > d.size()) { ok = false; memset(v, 0, n); return; } memcpy(v, &d[p], n); p += n; };
    auto str = [&]() { uint16_t n = 0; get(&n, 2); if (p + n > d.size()) { ok = false; return std::wstring(); } std::string s(&d[p], n); p += n; return W(s); };
    uint32_t count = 0; get(&count, 4);
    pk.id = str(); pk.program = str(); pk.name = str(); pk.version = str();
    get(&pk.stub, 8);
    for (uint32_t i = 0; i < count && ok; i++) {
        Entry e; e.name = str(); get(&e.off, 8); get(&e.packed, 8); get(&e.size, 8); get(&e.flags, 1);
        pk.total += e.size;
        pk.files.push_back(e);
    }
    return ok && !pk.id.empty();
}

std::wstring reg_key(const std::wstring& id) { return L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NativeBoy." + id; }

std::wstring reg_get(const std::wstring& id, const wchar_t* value) {
    HKEY k; std::wstring r;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, reg_key(id).c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
        wchar_t b[2048]; DWORD sz = sizeof b, type = 0;
        if (RegQueryValueExW(k, value, nullptr, &type, (BYTE*)b, &sz) == ERROR_SUCCESS && type == REG_SZ) r.assign(b, wcsnlen(b, sz / 2));
        RegCloseKey(k);
    }
    return r;
}

bool shortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& dir, const std::wstring& desc) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) return false;
    sl->SetPath(target.c_str()); sl->SetWorkingDirectory(dir.c_str()); sl->SetDescription(desc.c_str()); sl->SetIconLocation(target.c_str(), 0);
    IPersistFile* pf = nullptr; bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) { ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE)); pf->Release(); }
    sl->Release();
    return ok;
}

std::wstring safe_name(std::wstring s) { for (auto& c : s) if (wcschr(L"\\/:*?\"<>|", c)) c = L'_'; return s; }

// ------------------------------------------------------------------ install
struct Progress { HWND bar = nullptr; HWND label = nullptr; };

bool install(const Pack& pk, const std::wstring& dir, bool desktop, Progress pr, std::wstring& err) {
    make_dirs(dir);
    FILE* f = _wfopen(pk.self.c_str(), L"rb");
    if (!f) { err = L"Não foi possível ler o instalador."; return false; }
    std::vector<char> packed, data;
    uint64_t done = 0;
    for (auto& e : pk.files) {
        std::wstring out = join(dir, e.name);
        if (pr.label) SetWindowTextW(pr.label, (L"Copiando " + e.name).c_str());
        if (!((e.flags & 1) && GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES)) {
            size_t sl = out.find_last_of(L'\\');
            if (sl != std::wstring::npos) make_dirs(out.substr(0, sl));
            packed.resize(e.packed); data.resize(e.size);
            _fseeki64(f, e.off, SEEK_SET);
            if (fread(packed.data(), 1, e.packed, f) != e.packed ||
                (e.size && stbi_zlib_decode_buffer(data.data(), (int)e.size, packed.data(), (int)e.packed) != (int)e.size)) {
                fclose(f); err = L"O instalador está corrompido (" + e.name + L"). Baixe-o de novo."; return false;
            }
            FILE* o = _wfopen(out.c_str(), L"wb");
            if (!o) { fclose(f); err = L"Não foi possível gravar " + out + L"\n\nSe o jogo estiver aberto, feche-o e tente de novo."; return false; }
            bool ok = e.size == 0 || fwrite(data.data(), 1, e.size, o) == e.size;
            fclose(o);
            if (!ok) { fclose(f); err = L"Sem espaço em disco."; return false; }
        }
        done += e.size;
        if (pr.bar) { SendMessageW(pr.bar, PBM_SETPOS, (WPARAM)(pk.total ? done * 100 / pk.total : 100), 0); }
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    // the uninstaller: this same program, without the game inside
    {
        std::vector<char> stub(pk.stub);
        _fseeki64(f, 0, SEEK_SET);
        bool ok = fread(stub.data(), 1, stub.size(), f) == stub.size();
        FILE* o = ok ? _wfopen(join(dir, L"Desinstalar.exe").c_str(), L"wb") : nullptr;
        if (o) { fwrite(stub.data(), 1, stub.size(), o); fclose(o); }
    }
    fclose(f);
    std::wstring exe = join(dir, pk.program);
    // shortcuts
    std::wstring lnk_desktop;
    std::wstring menu = known(FOLDERID_Programs);
    if (!menu.empty()) shortcut(join(menu, safe_name(pk.name) + L".lnk"), exe, dir, pk.name);
    if (desktop) {
        std::wstring dt = known(FOLDERID_Desktop);
        if (!dt.empty()) { lnk_desktop = join(dt, safe_name(pk.name) + L".lnk"); shortcut(lnk_desktop, exe, dir, pk.name); }
    }
    // information for the uninstaller
    if (FILE* o = _wfopen(join(dir, L"desinstalar.ini").c_str(), L"wb")) {
        std::string t = "id=" + U(pk.id) + "\nnome=" + U(pk.name) + "\natalho_menu=" + U(join(menu, safe_name(pk.name) + L".lnk")) + "\natalho_area=" + U(lnk_desktop) + "\n";
        fwrite(t.data(), 1, t.size(), o); fclose(o);
    }
    // "Installed apps"
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, reg_key(pk.id).c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        auto set = [&](const wchar_t* n, const std::wstring& v) { RegSetValueExW(k, n, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * 2)); };
        auto setd = [&](const wchar_t* n, DWORD v) { RegSetValueExW(k, n, 0, REG_DWORD, (const BYTE*)&v, 4); };
        set(L"DisplayName", pk.name);
        set(L"DisplayVersion", pk.version);
        set(L"DisplayIcon", exe + L",0");
        set(L"Publisher", L"NativeBoy Studio");
        set(L"InstallLocation", dir);
        set(L"UninstallString", L"\"" + join(dir, L"Desinstalar.exe") + L"\" /desinstalar");
        set(L"QuietUninstallString", L"\"" + join(dir, L"Desinstalar.exe") + L"\" /desinstalar /S");
        setd(L"NoModify", 1); setd(L"NoRepair", 1);
        setd(L"EstimatedSize", (DWORD)(pk.total / 1024));
        RegCloseKey(k);
    }
    return true;
}

// ------------------------------------------------------------------ uninstall
std::string ini_get(const std::string& t, const std::string& key) {
    size_t p = 0;
    while (p < t.size()) {
        size_t e = t.find('\n', p); if (e == std::string::npos) e = t.size();
        std::string l = t.substr(p, e - p);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.compare(0, key.size() + 1, key + "=") == 0) return l.substr(key.size() + 1);
        p = e + 1;
    }
    return "";
}

void remove_tree(const std::wstring& dir, bool keep_player) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(join(dir, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        std::wstring p = join(dir, n);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (keep_player && (n == L"saves" || n == L"capturas" || n == L"config")) continue;
            remove_tree(p, false);
            RemoveDirectoryW(p.c_str());
        } else {
            SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (!DeleteFileW(p.c_str())) MoveFileExW(p.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

int uninstall(const std::wstring& dir) {
    std::string info;
    if (FILE* f = _wfopen(join(dir, L"desinstalar.ini").c_str(), L"rb")) {
        char b[4096]; size_t n = fread(b, 1, sizeof b, f); info.assign(b, n); fclose(f);
    }
    std::wstring id = W(ini_get(info, "id")), name = W(ini_get(info, "nome"));
    if (id.empty()) { say(L"Não encontrei as informações da instalação em " + dir, true); return 1; }
    bool keep = true;
    if (!silent) {
        if (MessageBoxW(nullptr, (L"Remover " + name + L" deste computador?").c_str(), L"Desinstalar", MB_ICONQUESTION | MB_YESNO) != IDYES) return 2;
        keep = MessageBoxW(nullptr, (L"Manter seus saves e capturas de " + name + L"?\n\nSim: ficam em " + dir +
                                     L" (para jogar de novo depois).\nNão: apaga tudo.").c_str(),
                           L"Desinstalar", MB_ICONQUESTION | MB_YESNO) == IDYES;
    }
    for (const char* k : {"atalho_menu", "atalho_area"}) { std::wstring l = W(ini_get(info, k)); if (!l.empty()) DeleteFileW(l.c_str()); }
    RegDeleteKeyW(HKEY_CURRENT_USER, reg_key(id).c_str());
    remove_tree(dir, keep);
    if (!keep) RemoveDirectoryW(dir.c_str());
    say(name + L" foi removido.", false);
    return 0;
}

// ------------------------------------------------------------------ window
enum { ID_PATH = 100, ID_BROWSE, ID_DESKTOP, ID_RUN, ID_GO, ID_CANCEL };
Pack g_pack;
HWND g_path, g_desktop, g_run, g_go, g_cancel, g_bar, g_label;
bool g_done = false;
HFONT g_font, g_big;

LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BROWSE: {
            IFileOpenDialog* d = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, (void**)&d))) {
                DWORD o; d->GetOptions(&o); d->SetOptions(o | FOS_PICKFOLDERS);
                if (SUCCEEDED(d->Show(h))) {
                    IShellItem* it = nullptr;
                    if (SUCCEEDED(d->GetResult(&it))) {
                        PWSTR p = nullptr;
                        if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { SetWindowTextW(g_path, join(p, safe_name(g_pack.name)).c_str()); CoTaskMemFree(p); }
                        it->Release();
                    }
                }
                d->Release();
            }
            return 0;
        }
        case ID_GO: {
            if (g_done) {
                if (SendMessageW(g_run, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    wchar_t b[MAX_PATH * 4]; GetWindowTextW(g_path, b, MAX_PATH * 4);
                    ShellExecuteW(nullptr, L"open", join(b, g_pack.program).c_str(), nullptr, b, SW_SHOWNORMAL);
                }
                DestroyWindow(h);
                return 0;
            }
            wchar_t b[MAX_PATH * 4]; GetWindowTextW(g_path, b, MAX_PATH * 4);
            std::wstring dir = b, err;
            if (dir.size() < 3) return 0;
            EnableWindow(g_go, FALSE); EnableWindow(g_cancel, FALSE); EnableWindow(g_path, FALSE);
            EnableWindow(GetDlgItem(h, ID_BROWSE), FALSE); EnableWindow(g_desktop, FALSE);
            ShowWindow(g_bar, SW_SHOW);
            bool ok = install(g_pack, dir, SendMessageW(g_desktop, BM_GETCHECK, 0, 0) == BST_CHECKED, Progress{g_bar, g_label}, err);
            if (!ok) {
                say(err, true);
                EnableWindow(g_go, TRUE); EnableWindow(g_cancel, TRUE); EnableWindow(g_path, TRUE);
                EnableWindow(GetDlgItem(h, ID_BROWSE), TRUE); EnableWindow(g_desktop, TRUE);
                return 0;
            }
            g_done = true;
            SetWindowTextW(g_label, (g_pack.name + L" foi instalado. Ele está no menu Iniciar" +
                                     (SendMessageW(g_desktop, BM_GETCHECK, 0, 0) == BST_CHECKED ? L" e na Área de Trabalho." : L".")).c_str());
            SetWindowTextW(g_go, L"Concluir");
            ShowWindow(g_run, SW_SHOW);
            EnableWindow(g_go, TRUE);
            return 0;
        }
        case ID_CANCEL: DestroyWindow(h); return 0;
        }
        break;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp; SetBkMode(dc, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int run_window(HINSTANCE inst, const std::wstring& default_dir) {
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    HDC sdc = GetDC(nullptr); int dpi = GetDeviceCaps(sdc, LOGPIXELSY); ReleaseDC(nullptr, sdc);
    auto S = [&](int v) { return MulDiv(v, dpi, 96); };
    g_font = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_big = CreateFontW(-S(22), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    WNDCLASSW wc{}; wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = L"NativeBoyInstalador";
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW); wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    RECT r{0, 0, S(540), S(300)};
    DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, st, FALSE);
    HWND h = CreateWindowW(wc.lpszClassName, (L"Instalar " + g_pack.name).c_str(), st, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    auto mk = [&](const wchar_t* cls, const std::wstring& text, DWORD style, int x, int y, int w, int hh, int id, HFONT f) {
        HWND c = CreateWindowW(cls, text.c_str(), WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(hh), h, (HMENU)(intptr_t)id, inst, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        return c;
    };
    mk(L"STATIC", g_pack.name, 0, 24, 18, 490, 32, 0, g_big);
    mk(L"STATIC", L"Este assistente instala o jogo neste computador, para o seu usuário (não precisa de administrador).", 0, 24, 56, 490, 40, 0, g_font);
    mk(L"STATIC", L"Pasta de instalação:", 0, 24, 102, 300, 20, 0, g_font);
    g_path = mk(L"EDIT", default_dir, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 24, 124, 390, 26, ID_PATH, g_font);
    mk(L"BUTTON", L"Procurar...", BS_PUSHBUTTON | WS_TABSTOP, 422, 123, 94, 28, ID_BROWSE, g_font);
    g_desktop = mk(L"BUTTON", L"Criar atalho na Área de Trabalho", BS_AUTOCHECKBOX | WS_TABSTOP, 24, 162, 300, 24, ID_DESKTOP, g_font);
    SendMessageW(g_desktop, BM_SETCHECK, BST_CHECKED, 0);
    g_label = mk(L"STATIC", L"", 0, 24, 196, 490, 22, 0, g_font);
    g_bar = CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD, S(24), S(220), S(492), S(14), h, nullptr, inst, nullptr);
    g_run = mk(L"BUTTON", L"Abrir o jogo agora", BS_AUTOCHECKBOX | WS_TABSTOP, 24, 252, 220, 24, ID_RUN, g_font);
    SendMessageW(g_run, BM_SETCHECK, BST_CHECKED, 0);
    ShowWindow(g_run, SW_HIDE);
    g_cancel = mk(L"BUTTON", L"Cancelar", BS_PUSHBUTTON | WS_TABSTOP, 318, 250, 96, 30, ID_CANCEL, g_font);
    g_go = mk(L"BUTTON", L"Instalar", BS_DEFPUSHBUTTON | WS_TABSTOP, 422, 250, 96, 30, ID_GO, g_font);
    ShowWindow(h, SW_SHOW);
    SetFocus(g_go);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) { if (!IsDialogMessageW(h, &m)) { TranslateMessage(&m); DispatchMessageW(&m); } }
    return g_done ? 0 : 2;
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool un = false, un2 = false; std::wstring dir_arg, un_dir;
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"/S" || a == L"/s") silent = true;
        else if (a.compare(0, 3, L"/D=") == 0) dir_arg = a.substr(3);
        else if (a == L"/desinstalar") un = true;
        else if (a == L"/desinstalar2" && i + 1 < argc) { un2 = true; un_dir = argv[++i]; }
    }
    if (un2) {                                  // running from a temporary copy: remove the game folder
        Sleep(400);
        int r = uninstall(un_dir);
        MoveFileExW(self_path().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        return r;
    }
    if (un) {                                   // Desinstalar.exe inside the game folder: continue from a temporary copy
        std::wstring me = self_path(), dir = me.substr(0, me.find_last_of(L'\\'));
        wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        std::wstring copy = join(tmp, L"nativeboy_desinstalar_" + std::to_wstring(GetCurrentProcessId()) + L".exe");
        if (!CopyFileW(me.c_str(), copy.c_str(), FALSE)) return uninstall(dir);
        std::wstring cmd = L"\"" + copy + L"\" /desinstalar2 \"" + dir + L"\"" + (silent ? L" /S" : L"");
        STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
        if (!CreateProcessW(copy.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, tmp, &si, &pi)) return uninstall(dir);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 0;
    }
    if (!read_pack(g_pack)) { say(L"Este instalador está incompleto ou corrompido. Baixe-o de novo.", true); return 1; }
    std::wstring dir = reg_get(g_pack.id, L"InstallLocation");           // updating: same folder as before
    if (dir.empty()) {
        std::wstring la = known(FOLDERID_LocalAppData);
        dir = join(join(la.empty() ? L"C:\\Jogos" : la, L"Programs"), safe_name(g_pack.name));
    }
    if (!dir_arg.empty()) dir = dir_arg;
    if (silent) {
        std::wstring err;
        if (!install(g_pack, dir, true, Progress{}, err)) { say(err, true); return 1; }
        return 0;
    }
    return run_window(inst, dir);
}
