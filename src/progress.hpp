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
    int unlocked = 1;   // counted from the right: 1 means only track 7 is playable

    bool isUnlocked(int index) const { return index >= TrackCount - unlocked; }
    // Leftmost unlocked track: the one whose layer holds the clue.
    int frontier() const { return std::clamp(TrackCount - unlocked, 0, TrackCount - 1); }
    // Next track to unlock, or -1 once every track is open.
    int nextLocked() const { return unlocked >= TrackCount ? -1 : TrackCount - unlocked - 1; }
    bool complete() const { return unlocked >= TrackCount; }
    uint32_t mask() const {
        uint32_t bits = 0;
        for (int i = 0; i < TrackCount; ++i) if (isUnlocked(i)) bits |= 1u << i;
        return bits;
    }
    void advance() { if (unlocked < TrackCount) ++unlocked; }
};

inline Progress loadProgress(const fs::path& path) {
    Progress progress;
    std::ifstream file(path);
    if (!file) return progress;
    // Deliberately tiny: one integer. Anything unreadable starts over rather
    // than failing, because losing progress must never block launching.
    std::string text((std::istreambuf_iterator<char>(file)), {});
    size_t at = text.find("\"unlocked\"");
    if (at == std::string::npos) return progress;
    at = text.find(':', at);
    if (at == std::string::npos) return progress;
    try { progress.unlocked = std::clamp(std::stoi(text.substr(at + 1)), 1, TrackCount); }
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
