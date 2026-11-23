// NativeBoy Studio - APK built on GitHub (see github_apk.h)
#include "github_apk.h"
#include "../common/platform.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <thread>
#include <vector>

namespace {

// ------------------------------------------------------------------ small helpers
std::string b64(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
        o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += T[v & 63];
    }
    if (i + 1 == in.size()) { unsigned v = (unsigned char)in[i] << 16; o += T[v >> 18]; o += T[(v >> 12) & 63]; o += "=="; }
    else if (i + 2 == in.size()) { unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8); o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += '='; }
    return o;
}

std::string json_esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o;
}

// value of "key" (string or number) after position `from` in a JSON text; "" if missing
std::string jget(const std::string& j, const std::string& key, size_t from = 0, size_t* at = nullptr) {
    const std::string k = "\"" + key + "\"";
    size_t p = j.find(k, from);
    while (p != std::string::npos) {
        size_t c = p + k.size();
        while (c < j.size() && (j[c] == ' ' || j[c] == '\n' || j[c] == '\r' || j[c] == '\t')) c++;
        if (c < j.size() && j[c] == ':') {
            c++;
            while (c < j.size() && (j[c] == ' ' || j[c] == '\n' || j[c] == '\r' || j[c] == '\t')) c++;
            std::string v;
            if (c < j.size() && j[c] == '"') {
                for (size_t q = c + 1; q < j.size() && j[q] != '"'; q++) {
                    if (j[q] == '\\' && q + 1 < j.size()) { q++; v += j[q] == 'n' ? '\n' : j[q]; }
                    else v += j[q];
                }
            } else {
                for (size_t q = c; q < j.size() && j[q] != ',' && j[q] != '}' && j[q] != ']' && j[q] != '\n'; q++) v += j[q];
                while (!v.empty() && v.back() == ' ') v.pop_back();
            }
            if (at) *at = c;
            return v;
        }
        p = j.find(k, p + 1);
    }
    return "";
}

// ------------------------------------------------------------------ SHA-1 (git object ids)
struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    void block(const unsigned char* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 80; i++) { uint32_t t = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]; w[i] = (t << 1) | (t >> 31); }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::string hex(const std::string& data) {
        std::string m = data;
        const uint64_t bits = (uint64_t)data.size() * 8;
        m += (char)0x80;
        while (m.size() % 64 != 56) m += (char)0;
        for (int i = 7; i >= 0; i--) m += (char)(bits >> (i * 8));
        for (size_t i = 0; i < m.size(); i += 64) block((const unsigned char*)m.data() + i);
        char o[41];
        for (int i = 0; i < 5; i++) snprintf(o + i * 8, 9, "%08x", h[i]);
        return std::string(o, 40);
    }
};

// ------------------------------------------------------------------ GitHub API
struct Gh {
    std::string token;
    int status = 0;
    std::string body, err;
    bool call(const std::string& method, const std::string& path, const std::string& json = "") {
        plat::HttpResult r;
        std::vector<std::string> h = {"Authorization: Bearer " + token, "Accept: application/vnd.github+json",
                                      "X-GitHub-Api-Version: 2022-11-28", "User-Agent: NativeBoyStudio"};
        if (!json.empty()) h.push_back("Content-Type: application/json");
        // (NB_GITHUB_API: another address for the API, used by the automatic tests)
        const char* api = getenv("NB_GITHUB_API");
        const std::string url = path.compare(0, 4, "http") == 0 ? path : std::string(api ? api : "https://api.github.com") + path;
        if (!plat::http_request(method, url, h, json, r, err)) { status = 0; body.clear(); return false; }
        status = r.status; body = r.body;
        return true;
    }
    // GitHub's own message for an error ("Bad credentials", "Not Found"...)
    std::string message() const { std::string m = jget(body, "message"); return m.empty() ? "código " + std::to_string(status) : m + " (código " + std::to_string(status) + ")"; }
};

void list_rec(const std::string& root, const std::string& rel, std::vector<std::string>& out) {
    const std::string d = rel.empty() ? root : plat::join(root, rel);
    for (auto& f : plat::list_dir(d, false)) out.push_back(rel.empty() ? f : rel + "/" + f);
    for (auto& s : plat::list_dir(d, true)) {
        const std::string r2 = rel.empty() ? s : rel + "/" + s;
        // what GitHub creates on its own while compiling is not sent
        if (r2 == "build" || r2 == ".gradle" || r2 == "SDL2" || r2 == "app/build" || r2 == "app/.cxx" || r2 == "app/src/main/java/org/libsdl" || r2 == ".git") continue;
        list_rec(root, r2, out);
    }
}

void wait_s(int s) { std::this_thread::sleep_for(std::chrono::seconds(s)); }

} // namespace

std::string git_blob_sha(const std::string& content) {
    Sha1 s;
    return s.hex("blob " + std::to_string(content.size()) + std::string(1, '\0') + content);
}

std::string apk_repo_name(const std::string& game) {
    std::string r = "nativeboy-apk-";
    bool dash = false;
    for (unsigned char c : game) {
        if (isalnum(c)) { r += (char)tolower(c); dash = false; }
        else if (!dash && r.back() != '-') { r += '-'; dash = true; }
    }
    while (!r.empty() && r.back() == '-') r.pop_back();
    return r.size() > 60 ? r.substr(0, 60) : r;
}

bool build_apk_on_github(const std::string& project_dir, const std::string& token, const std::string& repo,
                         const std::string& out_dir, BuildLog& log, ApkResult& res, std::string& err) {
    auto say = [&](const std::string& s) { if (log.line) log.line(s); };
    auto cancelled = [&]() { return log.cancel && log.cancel->load(); };
    Gh gh; gh.token = token;
    // 1. who is this key from
    if (!gh.call("GET", "/user")) { err = gh.err; return false; }
    if (gh.status == 401) { err = "A chave de acesso do GitHub não vale (expirou ou foi digitada errada). Crie outra."; return false; }
    if (gh.status != 200) { err = "O GitHub recusou a chave: " + gh.message(); return false; }
    const std::string user = jget(gh.body, "login");
    const std::string R = "/repos/" + user + "/" + repo;
    res.repo_url = "https://github.com/" + user + "/" + repo;
    say("Conta do GitHub: " + user);
    // 2. the private repository of this game
    if (!gh.call("GET", R)) { err = gh.err; return false; }
    if (gh.status == 404) {
        say("Criando o repositório privado " + user + "/" + repo + "...");
        if (!gh.call("POST", "/user/repos", "{\"name\":\"" + json_esc(repo) + "\",\"private\":true,\"has_wiki\":false,\"has_projects\":false,"
                                            "\"description\":\"APK gerado pelo NativeBoy Studio (contém dados do jogo: mantenha privado)\"}")) { err = gh.err; return false; }
        if (gh.status != 201) { err = "Não foi possível criar o repositório: " + gh.message() + ". A chave precisa da permissão \"repo\"."; return false; }
    } else if (gh.status != 200) { err = "Não foi possível ver o repositório: " + gh.message(); return false; }
    else if (jget(gh.body, "private") != "true") say("Aviso: o repositório " + repo + " é público; o jogo fica visível para todos.");
    // 3. current state of the branch (an empty repository gets its first file first)
    std::string head;
    for (int tries = 0; tries < 2 && head.empty(); tries++) {
        if (!gh.call("GET", R + "/git/ref/heads/main")) { err = gh.err; return false; }
        if (gh.status == 200) { size_t at = 0; jget(gh.body, "object", 0, &at); head = jget(gh.body, "sha", at); break; }
        std::string first; plat::read_text(plat::join(project_dir, "LEIA-ME-ANDROID.txt"), first);
        if (!gh.call("PUT", R + "/contents/LEIA-ME-ANDROID.txt", "{\"message\":\"Projeto Android do NativeBoy Studio\",\"branch\":\"main\",\"content\":\"" + b64(first) + "\"}")) { err = gh.err; return false; }
        if (gh.status != 201 && gh.status != 200) { err = "Não foi possível começar o repositório: " + gh.message(); return false; }
    }
    if (head.empty()) { err = "O repositório não tem o ramo main."; return false; }
    // 4. what is already there (files not changed since the last time are not sent again)
    std::map<std::string, std::string> remote;     // path -> sha
    if (gh.call("GET", R + "/git/trees/" + head + "?recursive=1") && gh.status == 200) {
        size_t p = 0;
        while ((p = gh.body.find("\"path\"", p)) != std::string::npos) {
            size_t e = gh.body.find('}', p);
            std::string obj = gh.body.substr(p - 1, e == std::string::npos ? std::string::npos : e - p + 2);
            if (jget(obj, "type") == "blob") remote[jget(obj, "path")] = jget(obj, "sha");
            p = e == std::string::npos ? gh.body.size() : e;
        }
    }
    // 5. the files
    std::vector<std::string> files;
    list_rec(project_dir, "", files);
    files.erase(std::remove_if(files.begin(), files.end(), [](const std::string& f) {
        return f.size() > 4 && f.compare(f.size() - 4, 4, ".apk") == 0; }), files.end());
    std::string tree = "{\"tree\":[";
    int sent = 0, kept = 0, n = 0;
    for (auto& f : files) {
        if (cancelled()) { err = "Cancelado."; return false; }
        std::string c;
        if (!plat::read_text(plat::join(project_dir, f), c)) continue;
        std::string sha = git_blob_sha(c);
        auto it = remote.find(f);
        if (it == remote.end() || it->second != sha) {
            if (!gh.call("POST", R + "/git/blobs", "{\"encoding\":\"base64\",\"content\":\"" + b64(c) + "\"}")) { err = gh.err; return false; }
            if (gh.status != 201) { err = "Falha ao enviar " + f + ": " + gh.message(); return false; }
            sha = jget(gh.body, "sha");
            sent++;
            if (sent % 10 == 0) say("Enviando arquivos... " + std::to_string(sent));
        } else kept++;
        tree += std::string(n++ ? "," : "") + "{\"path\":\"" + json_esc(f) + "\",\"mode\":\"100644\",\"type\":\"blob\",\"sha\":\"" + sha + "\"}";
    }
    // the app's signature, made by GitHub on the first build, stays (updates install over the old app)
    if (remote.count("assinatura.jks") && std::find(files.begin(), files.end(), "assinatura.jks") == files.end())
        tree += std::string(n++ ? "," : "") + "{\"path\":\"assinatura.jks\",\"mode\":\"100644\",\"type\":\"blob\",\"sha\":\"" + remote["assinatura.jks"] + "\"}";
    tree += "]}";
    say(std::to_string(sent) + " arquivo(s) enviados, " + std::to_string(kept) + " já estavam no GitHub.");
    if (!gh.call("POST", R + "/git/trees", tree)) { err = gh.err; return false; }
    if (gh.status != 201) { err = "Falha ao montar os arquivos no GitHub: " + gh.message(); return false; }
    const std::string tree_sha = jget(gh.body, "sha");
    if (!gh.call("POST", R + "/git/commits", "{\"message\":\"Jogo atualizado pelo NativeBoy Studio\",\"tree\":\"" + tree_sha + "\",\"parents\":[\"" + head + "\"]}")) { err = gh.err; return false; }
    if (gh.status != 201) { err = "Falha ao registrar a versão: " + gh.message(); return false; }
    const std::string commit = jget(gh.body, "sha");
    if (!gh.call("PATCH", R + "/git/refs/heads/main", "{\"sha\":\"" + commit + "\",\"force\":true}")) { err = gh.err; return false; }
    if (gh.status != 200) {
        err = "O GitHub não aceitou os arquivos: " + gh.message() +
              (gh.status == 404 || gh.status == 403 || gh.status == 422 ? ". A chave precisa das permissões \"repo\" e \"workflow\" (o arquivo .github/workflows/apk.yml)." : "");
        return false;
    }
    // 6. GitHub compiles (about 10 minutes the first time)
    say("Arquivos enviados. O GitHub está compilando o APK (cerca de 10 minutos)...");
    std::string run_id, status, conclusion;
    const auto t0 = std::chrono::steady_clock::now();
    int last_min = -1;
    for (;;) {
        if (cancelled()) { err = "Cancelado. A compilação continua no GitHub: " + res.repo_url + "/actions"; return false; }
        const int secs = (int)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count();
        if (secs > 50 * 60) { err = "A compilação passou de 50 minutos. Veja em " + res.repo_url + "/actions"; return false; }
        if (gh.call("GET", R + "/actions/runs?head_sha=" + commit + "&per_page=5") && gh.status == 200) {
            size_t at = 0;
            const std::string id = jget(gh.body, "id", gh.body.find("\"workflow_runs\""), &at);
            if (!id.empty() && id != "0") {
                run_id = id;
                status = jget(gh.body, "status", at);
                conclusion = jget(gh.body, "conclusion", at);
                res.run_url = jget(gh.body, "html_url", at);
            }
        }
        if (run_id.empty() && secs > 180) { err = "O GitHub não começou a compilação. Confira se o Actions está ativado em " + res.repo_url + "/settings/actions"; return false; }
        if (status == "completed") break;
        if (log.progress) log.progress(std::min(0.95f, secs / 720.f));
        if (secs / 60 != last_min) { last_min = secs / 60; if (secs >= 60) say("Compilando no GitHub... " + std::to_string(secs / 60) + " min"); }
        wait_s(15);
    }
    if (conclusion != "success") { err = "A compilação no GitHub falhou (" + conclusion + "). Veja o motivo em " + res.run_url; return false; }
    // 7. the APK, from the release the workflow made
    if (!gh.call("GET", R + "/releases/tags/apk-" + commit.substr(0, 7)) || gh.status != 200) { err = "O APK não apareceu em " + res.repo_url + "/releases"; return false; }
    size_t at = 0;
    jget(gh.body, "assets", 0, &at);
    const std::string asset_url = jget(gh.body, "url", at), asset_name = jget(gh.body, "name", at);
    if (asset_url.empty()) { err = "O APK não apareceu em " + res.repo_url + "/releases"; return false; }
    plat::HttpResult r;
    std::vector<std::string> h = {"Authorization: Bearer " + token, "Accept: application/octet-stream", "User-Agent: NativeBoyStudio"};
    // (GitHub answers with a link to its file storage; the key is not sent there)
    if (!plat::http_request("GET", asset_url, h, "", r, err, false)) return false;
    if (r.status >= 300 && r.status < 400 && !r.location.empty()) { const std::string where = r.location; if (!plat::http_request("GET", where, {"User-Agent: NativeBoyStudio"}, "", r, err, true)) return false; }
    if (r.status != 200 || r.body.size() < 1000 || r.body.compare(0, 2, "PK") != 0) { err = "Não foi possível baixar o APK (código " + std::to_string(r.status) + ")."; return false; }
    plat::make_dirs(out_dir);
    res.apk = plat::join(out_dir, asset_name.empty() ? repo + ".apk" : asset_name);
    if (!plat::write_file(res.apk, r.body.data(), r.body.size())) { err = "Não foi possível gravar " + res.apk; return false; }
    if (log.progress) log.progress(1.f);
    say("APK pronto: " + res.apk + " (" + std::to_string(r.body.size() / 1024) + " KB)");
    return true;
}
