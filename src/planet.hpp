#pragma once
// Each track owns a planet. Switching the track on lets you zoom into it; the
// surface is a crowded scene with one thing hidden in it.
//
// The search is a *conjunction* search, which is what makes Where's Waldo work:
// the beacon is the only prop that is both a Spire AND wearing the track's own
// colour. Plenty of spires wear other colours and plenty of other shapes wear
// the track colour, so neither feature alone narrows it down and the eye has to
// scan rather than pop straight to it.
//
// No raylib here on purpose: generation stays testable without a window.
#include "mixer.hpp"
#include "hotreload.hpp"
#include <cstdint>
#include <vector>

namespace orbital {

enum class PropKind : unsigned char { Tower, Dome, Spire, Grove, Arch, Crystal };
inline constexpr int PropKindCount = 6;
inline constexpr int PaletteSize = 6;
inline constexpr PropKind BeaconKind = PropKind::Spire;
inline constexpr unsigned char BeaconPalette = 0;   // index 0 is the track's own colour

struct V3 { float x, y, z; };

struct Prop {
    V3 normal;            // unit vector: where it stands on the globe
    float scale, spin;
    PropKind kind;
    unsigned char palette;
    bool beacon;
};

struct Planet {
    std::vector<Prop> props;
    std::array<Rgb, PaletteSize> palette{};
    Rgb ground{}, sea{}, land{};
    int beacon = -1;
    bool found = false;
};

// Small deterministic generator: std::uniform_*_distribution is not specified
// to give identical sequences across implementations, and these planets must be
// the same every run and on every machine.
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

inline Rgb shade(Rgb base, float gain, float mix, Rgb toward) {
    auto blend = [&](unsigned char channel, unsigned char other) {
        float value = channel * gain * (1 - mix) + other * mix;
        return static_cast<unsigned char>(std::clamp(value, 0.f, 255.f));
    };
    return {blend(base.r, toward.r), blend(base.g, toward.g), blend(base.b, toward.b)};
}

inline std::array<Rgb, PaletteSize> buildPalette(Rgb base) {
    return {
        base,                                     // 0: the track's colour, worn by the beacon
        shade(base, .55f, .10f, {18, 26, 38}),    // 1: the same colour in shadow
        shade(base, 1.15f, .38f, {244, 247, 250}),// 2: bleached
        shade(base, .80f, .55f, {96, 122, 142}),  // 3: slate
        shade(base, .70f, .62f, {188, 156, 116}), // 4: sand
        shade(base, .60f, .70f, {58, 92, 88}),    // 5: moss
    };
}

// Fibonacci sphere: an even scatter with no clumping at the poles, jittered so
// it does not read as a lattice.
inline V3 scatterPoint(int index, int count, Rng& rng) {
    float y = 1.f - 2.f * (float(index) + .5f) / float(count);
    float ring = std::sqrt(std::max(0.f, 1.f - y * y));
    float theta = float(index) * 2.39996323f + rng.range(-.06f, .06f);
    y = std::clamp(y + rng.range(-.012f, .012f), -1.f, 1.f);
    float scale = std::sqrt(std::max(0.f, 1.f - y * y)) / std::max(ring, 1e-5f);
    return {std::cos(theta) * ring * scale, y, std::sin(theta) * ring * scale};
}

inline Planet generatePlanet(int track, Rgb trackColor, int count) {
    Planet planet;
    planet.palette = buildPalette(trackColor);
    planet.ground = shade(trackColor, .62f, .52f, {46, 63, 82});
    planet.sea = shade(trackColor, .42f, .66f, {26, 52, 86});
    planet.land = shade(trackColor, .80f, .46f, {92, 118, 86});
    Rng rng(uint64_t(track) * 7919u + 1013904223u);
    planet.props.reserve(size_t(count));
    for (int i = 0; i < count; ++i) {
        Prop prop{};
        prop.normal = scatterPoint(i, count, rng);
        prop.scale = rng.range(.62f, 1.28f);
        prop.spin = rng.range(0.f, 6.2831853f);
        prop.kind = PropKind(rng.below(PropKindCount));
        prop.palette = static_cast<unsigned char>(rng.below(PaletteSize));
        planet.props.push_back(prop);
    }

    auto matches = [&](const Prop& prop) {
        return prop.kind == BeaconKind && prop.palette == BeaconPalette;
    };
    // Exactly one prop may wear both beacon features.
    std::vector<int> pairs;
    for (int i = 0; i < count; ++i) if (matches(planet.props[i])) pairs.push_back(i);
    if (pairs.empty()) {
        int pick = rng.below(count);
        planet.props[pick].kind = BeaconKind;
        planet.props[pick].palette = BeaconPalette;
        pairs.push_back(pick);
    }
    planet.beacon = pairs[size_t(rng.below(int(pairs.size())))];
    for (int index : pairs)
        if (index != planet.beacon)
            planet.props[size_t(index)].palette = static_cast<unsigned char>(1 + rng.below(PaletteSize - 1));
    planet.props[size_t(planet.beacon)].beacon = true;

    // Decoys are the point: without enough of each half-match the beacon pops
    // out and there is no search left.
    auto ensure = [&](int wanted, auto predicate, auto convert) {
        int have = 0;
        for (const Prop& prop : planet.props) have += predicate(prop) ? 1 : 0;
        for (int guard = 0; have < wanted && guard < count * 4; ++guard) {
            int pick = rng.below(count);
            if (pick == planet.beacon || predicate(planet.props[size_t(pick)])) continue;
            convert(planet.props[size_t(pick)]);
            ++have;
        }
    };
    int shapeDecoys = std::max(12, count / 22), colorDecoys = std::max(12, count / 22);
    ensure(shapeDecoys,
           [](const Prop& p) { return p.kind == BeaconKind && p.palette != BeaconPalette; },
           [&](Prop& p) { p.kind = BeaconKind; p.palette = static_cast<unsigned char>(1 + rng.below(PaletteSize - 1)); });
    ensure(colorDecoys,
           [](const Prop& p) { return p.palette == BeaconPalette && p.kind != BeaconKind; },
           [&](Prop& p) { p.palette = BeaconPalette; if (p.kind == BeaconKind) p.kind = PropKind::Dome; });
    return planet;
}

inline int countBeaconMatches(const Planet& planet) {
    int total = 0;
    for (const Prop& prop : planet.props)
        total += (prop.kind == BeaconKind && prop.palette == BeaconPalette) ? 1 : 0;
    return total;
}
}
