// Background jobs (analysis, compilation...) so the interface never freezes.
#pragma once
#include "toolchain.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct Job {
    std::thread th;
    std::atomic<bool> running{false}, cancel{false}, finished{false};
    std::mutex mu;
    std::vector<std::string> lines;
    float progress = 0, stage_base = 0, stage_span = 1;
    std::string title, stage, error;
    bool ok = false;
    std::function<void(bool)> done;

    void start(const std::string& t, std::function<bool(BuildLog&, std::string&)> fn, std::function<void(bool)> on_done) {
        if (running) return;
        if (th.joinable()) th.join();
        title = t; error.clear(); ok = false; progress = 0; stage.clear(); stage_base = 0; stage_span = 1;
        { std::lock_guard<std::mutex> lk(mu); lines.clear(); }
        cancel = false; finished = false; running = true;
        done = on_done;
        th = std::thread([this, fn]() {
            BuildLog log;
            log.cancel = &cancel;
            log.line = [this](const std::string& s) { std::lock_guard<std::mutex> lk(mu); lines.push_back(s); };
            log.progress = [this](float p) { std::lock_guard<std::mutex> lk(mu); progress = stage_base + stage_span * std::max(0.f, std::min(1.f, p)); };
            std::string err;
            bool r = false;
            try { r = fn(log, err); } catch (const std::exception& e) { err = std::string("Erro interno: ") + e.what(); r = false; }
            {
                std::lock_guard<std::mutex> lk(mu);
                ok = r; error = err;
                if (!r && !err.empty()) lines.push_back("ERRO: " + err.substr(0, err.find('\n')));
            }
            finished = true;
        });
    }
    void set_stage(const std::string& s, float base, float span) {
        std::lock_guard<std::mutex> lk(mu);
        stage = s; stage_base = base; stage_span = span; progress = base;
        lines.push_back("== " + s + " ==");
    }
    // call every frame from the UI thread
    void poll() {
        if (finished && running) {
            if (th.joinable()) th.join();
            running = false;
            finished = false;
            if (done) { auto d = done; done = nullptr; d(ok); }
        }
    }
    ~Job() { cancel = true; if (th.joinable()) th.join(); }
};
