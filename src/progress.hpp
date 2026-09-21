#pragma once
// Progression: tracks unlock right to left. Only the leftmost unlocked track
// (the frontier) carries a clue, and solving it unlocks the track to its left.
//
// Locked tracks are silent and cannot be toggled, so the mix itself is the
// record of how far the player has come. A run begins in silence: the one
// available track must be switched on before its layer, and its clue, exist.
#include "mixer.hpp"

namespace orbital {
namespace fs = std::filesystem;

struct Progress {
    int unlocked = 1;
    int count = 1;         // how many tracks this campaign has
    bool rightToLeft = true;   // which end opens first; the campaign decides

    bool isUnlocked(int index) const { return rightToLeft ? index >= count - unlocked : index < unlocked; }
    // The newest unlocked track: the one whose world holds the next key.
    int frontier() const {
        return std::clamp(rightToLeft ? count - unlocked : unlocked - 1, 0, count - 1);
    }
    // Next track to unlock, or -1 once every track is open.
    int nextLocked() const {
        if (unlocked >= count) return -1;
        return rightToLeft ? count - unlocked - 1 : unlocked;
    }
    bool complete() const { return unlocked >= count; }
    uint32_t mask() const {
        uint32_t bits = 0;
        for (int i = 0; i < count; ++i) if (isUnlocked(i)) bits |= 1u << i;
        return bits;
    }
    void advance() { if (unlocked < count) ++unlocked; }
};

inline Progress loadProgress(const fs::path& path, int trackCount, bool rightToLeft) {
    Progress progress;
    progress.count = std::max(1, trackCount);
    progress.rightToLeft = rightToLeft;
    std::ifstream file(path);
    if (!file) return progress;
    // Deliberately tiny: one integer. Anything unreadable starts over rather
    // than failing, because losing progress must never block launching.
    std::string text((std::istreambuf_iterator<char>(file)), {});
    size_t at = text.find("\"unlocked\"");
    if (at == std::string::npos) return progress;
    at = text.find(':', at);
    if (at == std::string::npos) return progress;
    try { progress.unlocked = std::clamp(std::stoi(text.substr(at + 1)), 1, progress.count); }
    catch (...) { }
    return progress;
}

inline void saveProgress(const fs::path& path, const Progress& progress) {
    std::error_code code;
    fs::create_directories(path.parent_path(), code);
    auto temp = path; temp += ".tmp";
    { std::ofstream out(temp); out << "{\"unlocked\":" << progress.unlocked << "}\n"; }
    fs::rename(temp, path, code);
}
}
