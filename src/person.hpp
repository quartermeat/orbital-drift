#pragma once
// What a person is, with no drawing and no raylib, so scene generation can own
// people and stay testable. figure.hpp draws one of these.
#include "mixer.hpp"
#include "hotreload.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace orbital {

// A small deterministic generator, shared by people and by scene layout:
// std::uniform_*_distribution is not specified to give identical sequences
// across implementations, and a world must be the same on every machine.
struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed) : state(seed * 6364136223846793005ull + 1442695040888963407ull) {}
    uint32_t next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        uint32_t x = uint32_t(state >> 33);
        x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
        return x;
    }
    float unit() { return float(next() >> 8) / 16777216.f; }
    float range(float low, float high) { return low + (high - low) * unit(); }
    int below(int bound) { return bound > 0 ? int(next() % uint32_t(bound)) : 0; }
};

// Colour utility shared by people, props and terrain.
inline Rgb shade(Rgb base, float gain, float mix, Rgb toward) {
    auto blend = [&](unsigned char channel, unsigned char other) {
        float value = channel * gain * (1 - mix) + other * mix;
        return static_cast<unsigned char>(std::clamp(value, 0.f, 255.f));
    };
    return {blend(base.r, toward.r), blend(base.g, toward.g), blend(base.b, toward.b)};
}

// People keep their own colours rather than the terrain palette, so they read
// as human on every world. Clothing is deliberately brighter than the ground.
inline constexpr int SkinCount = 5, HairCount = 6, ClothCount = 12;

inline const std::array<Rgb, SkinCount> SkinTones = {
    Rgb{247, 214, 186}, Rgb{226, 178, 140}, Rgb{193, 138, 100}, Rgb{141, 92, 62}, Rgb{86, 56, 40}};
inline const std::array<Rgb, HairCount> HairTones = {
    Rgb{38, 30, 28}, Rgb{86, 56, 36}, Rgb{176, 137, 74}, Rgb{190, 92, 44}, Rgb{150, 150, 156}, Rgb{234, 234, 232}};
inline const std::array<Rgb, ClothCount> ClothTones = {
    Rgb{206, 62, 62},  Rgb{56, 104, 186}, Rgb{74, 152, 96},  Rgb{232, 176, 58},
    Rgb{156, 84, 176}, Rgb{62, 166, 176}, Rgb{232, 128, 78}, Rgb{240, 240, 236},
    Rgb{70, 78, 92},   Rgb{188, 92, 132}, Rgb{124, 148, 70}, Rgb{158, 116, 78}};

enum class Pattern : unsigned char { Solid, Stripe, Spot };

// arm/leg angles are degrees from hanging straight down. `drop` sinks the body,
// `legs` shortens them, `tilt` leans the whole figure about `pivot`.
struct Pose { float armL, armR, legL, legR, drop, legs, tilt, pivot; const char* name; };

inline constexpr int PoseCount = 24;
// Legs with opposite signs stride; legs with the same sign fold to one side,
// which is the only way a single-segment leg reads as sitting in this view.
inline const std::array<Pose, PoseCount> Poses = {{
    {   6,   -6,   3,  -3, 0.00f, 1.00f,   0, 0.00f, "stand"},
    {  22,  -18,   2,  -4, 0.00f, 1.00f,   0, 0.00f, "stand2"},
    {  34,  -30,   6,  -6, 0.00f, 1.00f,   0, 0.00f, "akimbo"},
    { 118, -114,   5,  -5, 0.00f, 1.00f,   0, 0.00f, "shrug"},
    { -28,   32, -20,  22, 0.00f, 1.00f,   0, 0.00f, "walk"},
    {  32,  -28,  22, -20, 0.00f, 1.00f,   0, 0.00f, "walk2"},
    { -48,   54, -30,  34, 0.01f, 1.00f,   7, 0.00f, "run"},
    {  64,  -70,  -8,   8, 0.00f, 1.00f,  -5, 0.00f, "stroll"},
    { 168,  -12,   4,  -4, 0.00f, 1.00f,   0, 0.00f, "wave"},
    { 172, -174,   3,  -3, 0.00f, 1.00f,   0, 0.00f, "cheer"},
    {  96,  -14,   5,  -5, 0.00f, 1.00f,   0, 0.00f, "point"},
    {  84,  -84,   4,  -4, 0.00f, 1.00f,   0, 0.00f, "carry"},
    { 140, -136,   4,  -4, 0.00f, 1.00f,   0, 0.00f, "reach"},
    {  40,  -36,   8,  -8, 0.00f, 1.00f,  32, 0.00f, "bow"},
    {  88,  -84, -18,  22, 0.00f, 1.00f,  16, 0.00f, "push"},
    { 128,  -40, -26,  30, 0.00f, 1.00f,  10, 0.00f, "dance"},
    { -40,  128,  30, -26, 0.00f, 1.00f, -10, 0.00f, "dance2"},
    { 150, -150, -22,  26, 0.02f, 1.00f,   0, 0.00f, "jump"},
    {  30,  -26,  14, -12, 0.00f, 1.00f,  19, 0.00f, "lean"},
    { -26,   30, -12,  14, 0.00f, 1.00f, -19, 0.00f, "lean2"},
    {  52,  -40,  78,  72, 0.16f, 0.80f,   0, 0.00f, "sit"},
    { -52,   40, -78, -72, 0.16f, 0.80f,   0, 0.00f, "sit2"},
    {  60,  -52,  92,  86, 0.20f, 0.62f,   0, 0.00f, "sit-cross"},
    {  20,  -16,  58,  52, 0.15f, 0.58f,   0, 0.00f, "kneel"},
}};

struct Figure {
    unsigned char skin, hair, shirt, stripe, trousers, hat, prop;
    Pattern pattern;
    unsigned char pose;
    bool wearsHat, carries;
};

inline Figure rollFigure(Rng& rng) {
    Figure figure{};
    figure.skin = static_cast<unsigned char>(rng.below(SkinCount));
    figure.hair = static_cast<unsigned char>(rng.below(HairCount));
    figure.shirt = static_cast<unsigned char>(rng.below(ClothCount));
    figure.stripe = static_cast<unsigned char>(rng.below(ClothCount));
    figure.trousers = static_cast<unsigned char>(rng.below(ClothCount));
    figure.hat = static_cast<unsigned char>(rng.below(ClothCount));
    figure.prop = static_cast<unsigned char>(rng.below(ClothCount));
    float roll = rng.unit();
    figure.pattern = roll < .60f ? Pattern::Solid : (roll < .84f ? Pattern::Stripe : Pattern::Spot);
    figure.pose = static_cast<unsigned char>(rng.below(PoseCount));
    figure.wearsHat = rng.unit() < .42f;
    figure.carries = rng.unit() < .22f;
    return figure;
}

// A figure's identity: everything about how it looks, folded into one number.
// Two figures with the same id are the same Waldo -- same pose, same garments,
// same colours -- so a target can be named rather than merely pointed at.
//
// This is a stricter thing than `sameOutfit` below, and both are needed. The
// id is exact appearance; `sameOutfit` is what a player can actually tell
// apart in a crowd, which ignores pose and skin because at forty pixels they
// do not separate two people wearing the same clothes.
inline uint32_t figureId(const Figure& figure) {
    uint32_t hash = 2166136261u;
    auto fold = [&](uint32_t value) { hash = (hash ^ value) * 16777619u; };
    fold(figure.skin); fold(figure.hair); fold(figure.shirt); fold(figure.stripe);
    fold(figure.trousers); fold(figure.hat); fold(figure.prop);
    fold(uint32_t(figure.pattern)); fold(figure.pose);
    fold(figure.wearsHat ? 1u : 0u); fold(figure.carries ? 1u : 0u);
    hash ^= hash >> 16;
    return hash;
}

// The id as people will read it out: FIG-XXXX.
inline std::string figureTag(const Figure& figure) {
    static const char* digits = "0123456789ABCDEF";
    uint32_t id = figureId(figure) & 0xFFFFu;
    std::string tag = "FIG-";
    for (int shift = 12; shift >= 0; shift -= 4) tag += digits[(id >> shift) & 0xF];
    return tag;
}

// What a player actually scans for: the clothing, not the pose or the face
// there isn't. Two people sharing this are indistinguishable in a crowd, so
// the target must not share it with anyone.
inline bool sameOutfit(const Figure& a, const Figure& b) {
    if (a.pattern != b.pattern || a.shirt != b.shirt || a.trousers != b.trousers) return false;
    if (a.wearsHat != b.wearsHat) return false;
    if (a.wearsHat && a.hat != b.hat) return false;
    if (a.pattern != Pattern::Solid && a.stripe != b.stripe) return false;
    return true;
}
}
