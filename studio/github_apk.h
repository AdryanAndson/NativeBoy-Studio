// NativeBoy Studio - APK built on GitHub.
// Sends the Android project of a game to a PRIVATE repository of the user's account, GitHub compiles
// the APK (workflow .github/workflows/apk.yml of the project) and the APK is downloaded back.
// Needs a GitHub access key ("token") with the permissions repo and workflow.
#pragma once
#include "toolchain.h"
#include <string>

struct ApkResult {
    std::string apk;        // downloaded APK
    std::string repo_url;   // https://github.com/<user>/<repo>
    std::string run_url;    // the build on GitHub (to see the error, when there is one)
};

// project_dir = Meus Jogos/Android/<name>; repo = repository name; out_dir = where the APK is saved
bool build_apk_on_github(const std::string& project_dir, const std::string& token, const std::string& repo,
                         const std::string& out_dir, BuildLog& log, ApkResult& res, std::string& err);

// repository name from the game name (letters, digits and -)
std::string apk_repo_name(const std::string& game);
// git's id of a file: SHA-1 of "blob <size>\0<content>" (files already on GitHub are not sent again)
std::string git_blob_sha(const std::string& content);
