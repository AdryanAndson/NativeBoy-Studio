// Gameplay recording: animated GIF and video (AVI with Motion-JPEG video + PCM sound).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct GifRecorder {
    bool start(const std::string& path, int w, int h);
    void add_frame(const uint32_t* argb, int w, int h, int scale);   // one game frame (it keeps every 2nd)
    void stop();
    bool on() const { return f != nullptr; }
    double seconds() const { return frames_seen / 59.7275; }
    std::string path;
private:
    FILE* f = nullptr;
    int W = 0, H = 0;
    uint64_t frames_seen = 0;
    double delay_acc = 0;
    std::vector<uint8_t> idx;
};

struct VideoRecorder {
    // w x h = image size per frame (already scaled); rate = audio sample rate (stereo, 16 bits)
    bool start(const std::string& path, int w, int h, int sample_rate);
    void add_frame(const uint32_t* argb, int w, int h, int scale);    // one game frame
    void add_audio(const float* stereo, size_t floats);
    void stop();
    bool on() const { return f != nullptr; }
    double seconds() const { return frames / 59.7275; }
    uint64_t bytes() const { return movi_bytes; }
    std::string path;
private:
    FILE* f = nullptr;
    int W = 0, H = 0, rate = 0;
    uint64_t frames = 0, audio_frames = 0, movi_bytes = 0;
    long movi_start = 0;
    struct Idx { uint32_t id, flags, off, size; };
    std::vector<Idx> index;
    std::vector<uint8_t> rgb, jpg;
    void chunk(const char* id, const void* data, uint32_t size, bool key);
};
