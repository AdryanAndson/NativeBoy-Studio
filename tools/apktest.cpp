// "APK on GitHub" test against tests/fake_github.py: sends an Android project twice
// (the second time only the changed file goes), waits for the build and downloads the APK.
// usage: apktest <android project folder> <output folder>    (NB_GITHUB_API = fake server address)
#include "../studio/github_apk.h"
#include "../common/platform.h"
#include <cstdio>
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    BuildLog log; log.line = [](const std::string& s) { printf("  %s\n", s.c_str()); fflush(stdout); };
    ApkResult r; std::string err;
    const char* token = getenv("NB_TOKEN") ? getenv("NB_TOKEN") : "chave-de-teste";
    bool ok = build_apk_on_github(argv[1], token, apk_repo_name("Jogo de Teste: Ação!"), argv[2], log, r, err);
    printf("primeira: %s apk=%s repo=%s %s\n", ok ? "ok" : "FALHOU", r.apk.c_str(), r.repo_url.c_str(), err.c_str());
    if (!ok) return 1;
    plat::write_text(plat::join(argv[1], "jogo/src/mudou.txt"), "outra versão\n");
    ApkResult r2;
    bool ok2 = build_apk_on_github(argv[1], token, apk_repo_name("Jogo de Teste: Ação!"), argv[2], log, r2, err);
    printf("segunda: %s %s\n", ok2 ? "ok" : "FALHOU", err.c_str());
    return ok && ok2 ? 0 : 1;
}
