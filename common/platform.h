// Small platform layer (Windows first, Linux for development builds).
// All strings are UTF-8.
#pragma once
// phones and tablets (Android): touch controls, files inside the app, no windows to move
#if defined(__ANDROID__) && !defined(NB_MOBILE)
#define NB_MOBILE 1
#endif
#include <cstdio>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace plat {

std::string exe_dir();                       // folder containing the running executable
std::string user_data_dir(const std::string& app);   // %LOCALAPPDATA%\app
std::string documents_dir();                 // "Documentos" folder
std::string user_data_dir();                 // Windows: %APPDATA%; Linux: $XDG_DATA_HOME or ~/.local/share
std::string desktop_dir();                   // the user's desktop ("Área de Trabalho")
std::string start_menu_dir();                // Windows: Start Menu\Programs; Linux: ~/.local/share/applications
// Creates a launcher: Windows .lnk (link_path + ".lnk"), Linux .desktop (link_path + ".desktop").
bool create_shortcut(const std::string& link_path, const std::string& name, const std::string& target,
                     const std::string& workdir, const std::string& icon, const std::string& comment);
bool make_dirs(const std::string& path);
bool file_exists(const std::string& path);
bool dir_exists(const std::string& path);
bool read_file(const std::string& path, std::vector<uint8_t>& out);
bool read_text(const std::string& path, std::string& out);
bool write_file(const std::string& path, const void* data, size_t n);
bool write_text(const std::string& path, const std::string& s);
FILE* fopen_utf8(const std::string& path, const char* mode);   // UTF-8 path (wide API on Windows)
bool copy_file(const std::string& from, const std::string& to);
bool move_file(const std::string& from, const std::string& to);
bool remove_file(const std::string& path);
void set_executable(const std::string& path);   // chmod +x (no-op on Windows)
bool copy_tree(const std::string& from, const std::string& to);
bool remove_tree(const std::string& path);
std::vector<std::string> list_dir(const std::string& path, bool dirs);   // names only
std::string join(const std::string& a, const std::string& b);
std::string file_name(const std::string& path);
std::string stem(const std::string& path);
std::string parent(const std::string& path);
int64_t file_mtime(const std::string& path);          // only for comparing two files (the clock's epoch varies)
int64_t file_time_unix(const std::string& path);      // seconds since 1970 (0 = missing)
std::string now_stamp();                      // 2026-11-07_08-42-00
int cpu_count();

// dialogs (empty string = cancelled)
std::string open_file_dialog(const std::string& title, const std::string& filter_desc, const std::string& patterns);
std::string save_file_dialog(const std::string& title, const std::string& filter_desc, const std::string& patterns, const std::string& def);
std::string folder_dialog(const std::string& title);
void open_in_explorer(const std::string& path);
void open_url(const std::string& url);
void message_box(const std::string& title, const std::string& text, bool error);

// processes
struct ProcResult { int code = -1; std::string output; bool started = false; };
// Runs a command line synchronously (no console window), capturing stdout+stderr.
ProcResult run(const std::string& cmdline, const std::string& cwd, const std::vector<std::pair<std::string, std::string>>& env = {});
// Starts a program detached (used for "Jogar no PC").
bool launch(const std::string& exe, const std::string& args, const std::string& cwd);
std::string quote(const std::string& s);
// Path safe to give to programs without Unicode support (the bundled GCC): returns the
// 8.3 short name when the path has non-ASCII characters and a short name exists.
std::string tool_path(const std::string& p);
bool is_ascii(const std::string& s);
// HTTP GET (used by the update checker). Returns false on any error.
bool http_get(const std::string& url, std::string& body, std::string& err);
// HTTPS request with headers and body (GitHub API). status = HTTP code (0 = no answer).
// follow = follow redirects (the extra headers are not sent to the other site: signed download links)
struct HttpResult { int status = 0; std::string body, location; };
bool http_request(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                  const std::string& body, HttpResult& res, std::string& err, bool follow = false);

} // namespace plat
