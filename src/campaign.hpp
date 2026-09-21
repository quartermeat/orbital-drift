#pragma once
// A campaign is one Bitwig project turned into a scenario: its tracks, their
// names and colours, where its stems live, and the order they unlock in.
//
// Everything that used to be hardcoded about "Orbital Drift" lives here, so a
// second project is a second config file rather than a second build.
//
// No raylib: parsing stays testable without a window.
#include "person.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace orbital {
namespace fs = std::filesystem;

struct CampaignTrack {
    std::string name, file, role;
    Rgb colour{200, 200, 200};
};

struct Campaign {
    std::string title = "Untitled", subtitle, musicalKey = "A MINOR", stems = "audio";
    int tempo = 72, bars = 16;
    bool rightToLeft = true;   // which end of the track list opens first
    uint64_t seed = 0;         // makes this campaign's worlds its own
    std::vector<CampaignTrack> tracks;
    std::string note;          // parse warning, empty when clean

    int count() const { return int(tracks.size()); }
    uint32_t allMask() const { return count() >= 32 ? ~0u : (1u << count()) - 1; }
    // Index of the nth track to unlock, counting from the start of a run.
    int nthUnlock(int n) const { return rightToLeft ? count() - 1 - n : n; }
};

inline std::string trimField(std::string value) {
    size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}

inline bool parseHex(const std::string& text, Rgb& out) {
    if (text.empty()) return false;
    std::string hex = text[0] == '#' ? text.substr(1) : text;
    if (hex.size() != 6) return false;
    for (char c : hex) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    auto byte = [&](int i) { return static_cast<unsigned char>(std::stoi(hex.substr(size_t(i), 2), nullptr, 16)); };
    out = {byte(0), byte(2), byte(4)};
    return true;
}

// `key = value` lines; a `#` comment must be on its own line, because a
// trailing one cannot be told apart from a #rrggbb colour.
inline Campaign loadCampaign(const fs::path& path) {
    Campaign campaign;
    std::ifstream file(path);
    if (!file) { campaign.note = "cannot read " + path.string(); return campaign; }
    std::map<std::string, std::string> values;
    std::string line;
    int bad = 0;
    while (std::getline(file, line)) {
        line = trimField(line);
        if (line.empty() || line[0] == '#') continue;
        size_t split = line.find('=');
        if (split == std::string::npos) { ++bad; continue; }
        values[trimField(line.substr(0, split))] = trimField(line.substr(split + 1));
    }
    auto text = [&](const char* key, const std::string& fallback) {
        auto found = values.find(key);
        return found == values.end() || found->second.empty() ? fallback : found->second;
    };
    auto number = [&](const char* key, int fallback) {
        auto found = values.find(key);
        if (found == values.end()) return fallback;
        try { return std::stoi(found->second); } catch (...) { ++bad; return fallback; }
    };
    campaign.title = text("title", "Untitled");
    campaign.subtitle = text("subtitle", "");
    campaign.musicalKey = text("key", "A MINOR");
    campaign.stems = text("stems", "audio");
    campaign.tempo = std::clamp(number("tempo", 72), 20, 400);
    campaign.bars = std::clamp(number("bars", 16), 1, 512);
    campaign.rightToLeft = text("unlock", "right-to-left") != "left-to-right";
    // Worlds are seeded from the campaign as well as the track, or every
    // campaign would generate the same islands in different colours.
    auto seedFound = values.find("seed");
    if (seedFound != values.end()) {
        try { campaign.seed = std::stoull(seedFound->second); } catch (...) { ++bad; }
    }
    if (!campaign.seed) {
        uint64_t hash = 1469598103934665603ull;
        for (char ch : campaign.title) { hash ^= uint64_t((unsigned char)ch); hash *= 1099511628211ull; }
        campaign.seed = hash | 1ull;
    }

    // Tracks are numbered from 1 and must be contiguous: a gap means a typo,
    // and silently skipping it would drop a stem from the mix.
    for (int index = 1; index <= 64; ++index) {
        std::string prefix = "track." + std::to_string(index) + '.';
        auto name = values.find(prefix + "name");
        if (name == values.end()) break;
        CampaignTrack track;
        track.name = name->second;
        track.file = text((prefix + "file").c_str(), track.name + ".wav");
        track.role = text((prefix + "role").c_str(), "");
        auto colour = values.find(prefix + "color");
        if (colour == values.end() || !parseHex(colour->second, track.colour)) ++bad;
        campaign.tracks.push_back(track);
    }
    if (campaign.tracks.empty()) campaign.note = "no tracks in " + path.filename().string();
    else if (bad) campaign.note = std::to_string(bad) + " bad line(s) in " + path.filename().string();
    return campaign;
}
}
