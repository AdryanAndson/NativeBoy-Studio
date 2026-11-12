// Soundtrack replacement ("trilha remasterizada") and audio recording.
// A music pack is a folder with musica.ini:
//   endereco=C0EF        RAM address where the game keeps the number of the current song
//   mudo=1234            Game Boy sound channels silenced while a new song plays
//   volume=80
//   01=tema.ogg          song number (hex) = OGG file in the same folder
#pragma once
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct MusicPack {
    std::string dir;
    int addr = -1;                       // -1 = no pack
    uint8_t mute_mask = 0x0F;            // bit n = channel n+1
    float volume = 0.8f;
    std::map<int, std::string> files;    // song number -> file name
    bool load(const std::string& dir, std::string& err);
    bool empty() const { return addr < 0 || files.empty(); }
    bool save(const std::string& dir) const;
};

struct MusicPlayer {
    int rate = 48000;
    void play(const std::string& path, float volume);   // loops until stop()
    void stop() { cur.reset(); }
    bool playing() const { return (bool)cur; }
    void mix(std::vector<float>& out);                  // adds to interleaved stereo float samples
    std::string error;
private:
    struct Track { std::vector<float> pcm; int rate = 44100; };   // stereo interleaved
    std::map<std::string, std::shared_ptr<Track>> cache;
    std::shared_ptr<Track> cur;
    double pos = 0; float vol = 0.8f;
};

// Decodes an OGG Vorbis file to stereo float samples; false + err on failure
bool decode_ogg(const std::string& path, std::vector<float>& stereo, int& rate, std::string& err);

struct WavRecorder {
    bool start(const std::string& path, int rate);
    void add(const float* stereo, size_t floats);
    void stop();
    bool on() const { return f != nullptr; }
    std::string path;
    double seconds() const { return rate ? (double)frames / rate : 0; }
private:
    FILE* f = nullptr; int rate = 0; uint64_t frames = 0;
};
