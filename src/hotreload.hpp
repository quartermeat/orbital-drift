#pragma once
// Data hot reload: watch asset files and re-read them while the app runs.
//
// Nothing here may touch the mixer or the audio thread. Reloading data must
// never interrupt playback: the stems and the playhead live in the host and
// survive every reload, so the music keeps running while visuals change.
#include "mixer.hpp"
#include <chrono>
#include <map>
#include <string>

namespace orbital {
namespace fs = std::filesystem;

// Polls a file's mtime. Reports a change only once the write has settled, so a
// half-saved file is not parsed mid-write.
struct Watched {
    fs::path path;
    fs::file_time_type stamp{};
    bool changed() {
        std::error_code code;
        auto now = fs::last_write_time(path, code);
        if (code || now == stamp) return false;
        if (fs::file_time_type::clock::now() - now < std::chrono::milliseconds(120)) return false;
        stamp = now;
        return true;
    }
    void prime() { std::error_code code; stamp = fs::last_write_time(path, code); }
};

struct Rgb { unsigned char r, g, b; };

// Every field has a working default, so a missing or broken file still runs.
struct LayerConfig {
    std::array<Rgb, TrackCount> colors = {Rgb{175,149,246},{116,222,239},{147,180,206},
        {113,213,179},{239,187,119},{227,143,167},{214,210,149}};
    std::array<std::string, TrackCount> roles = {"ATMOSPHERE","MELODY","TEXTURE","LOW END","PULSE","BACKBEAT","MOVEMENT"};
    int starCount = 260, beaconDepth = 4;
    float orbitBase = .43f, orbitStep = .092f, glowScale = 1.f, energyGain = 7.f;
    std::string note;   // parse warning, empty when clean
};

inline std::string trim(std::string value) {
    size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}

inline bool parseRgb(const std::string& text, Rgb& out) {
    if (text.empty()) return false;
    std::string hex = text[0] == '#' ? text.substr(1) : text;
    if (hex.size() != 6) return false;
    for (char c : hex) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    auto byte = [&](int i) { return static_cast<unsigned char>(std::stoi(hex.substr(i, 2), nullptr, 16)); };
    out = {byte(0), byte(2), byte(4)};
    return true;
}

// Reads `key = value` lines. A `#` comment must be on its own line, because a
// trailing one is indistinguishable from a `#rrggbb` colour. Unknown keys are
// reported but never fatal, so an editor typo degrades to a warning.
inline LayerConfig loadLayerConfig(const fs::path& path) {
    LayerConfig config;
    std::ifstream file(path);
    if (!file) { config.note = "missing " + path.filename().string() + "; using defaults"; return config; }
    std::map<std::string, std::string> values;
    std::string line;
    int badLines = 0;
    for (int number = 1; std::getline(file, line); ++number) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t split = line.find('=');
        if (split == std::string::npos) { ++badLines; continue; }
        values[trim(line.substr(0, split))] = trim(line.substr(split + 1));
    }
    auto number = [&](const char* key, float fallback) {
        auto found = values.find(key);
        if (found == values.end()) return fallback;
        try { return std::stof(found->second); } catch (...) { ++badLines; return fallback; }
    };
    config.starCount = std::clamp(int(number("star.count", 260)), 0, 4000);
    config.beaconDepth = std::clamp(int(number("beacon.depth", 4)), 1, 12);
    config.orbitBase = number("orbit.base", .43f);
    config.orbitStep = number("orbit.step", .092f);
    config.glowScale = number("glow.scale", 1.f);
    config.energyGain = number("energy.gain", 7.f);
    for (int i = 0; i < TrackCount; ++i) {
        std::string prefix = "track." + std::to_string(i + 1) + '.';
        auto role = values.find(prefix + "role");
        if (role != values.end() && !role->second.empty()) config.roles[i] = role->second;
        auto color = values.find(prefix + "color");
        if (color != values.end() && !parseRgb(color->second, config.colors[i])) ++badLines;
    }
    if (badLines) config.note = std::to_string(badLines) + " bad line(s); those kept previous values";
    return config;
}
}
