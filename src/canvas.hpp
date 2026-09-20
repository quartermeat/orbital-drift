#pragma once
// A track's world is an endlessly zoomable 2D artwork.
//
// Nothing is stored. The canvas is a recursive grid: the root square is
// [0,1]x[0,1], every node holds a handful of motifs and BRANCH^2 children, and
// a node's contents come from hashing its path. Zooming in makes deeper nodes
// large enough to draw, so detail keeps arriving for as long as you keep going.
//
// The hunt is a conjunction search, the trick that makes Where's Waldo work:
// the beacon is the only motif that is both a spire AND wearing the track's own
// colour. Generation refuses that pairing everywhere else, so the beacon is
// globally unique without anything having to be stored or searched.
//
// No raylib here on purpose: generation stays testable without a window.
#include "mixer.hpp"
#include "hotreload.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace orbital {

enum class Motif : unsigned char { Ring, Spire, Bar, Blossom, Lattice, Eye };
inline constexpr int MotifCount = 6;
inline constexpr int PaletteSize = 6;
inline constexpr Motif BeaconMotif = Motif::Spire;
inline constexpr unsigned char BeaconPalette = 0;
inline constexpr int Branch = 3;      // 3x3 children, so each level is 3x deeper
inline constexpr int MaxDepth = 20;

struct Element {
    double x, y, size;    // world units, root square spans [0,1]
    float spin;
    Motif motif;
    unsigned char palette;
    bool beacon;
};

struct NodeId { int depth; long long ix, iy; };

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
        base,                                       // 0: the track's colour; only the beacon pairs it with a spire
        shade(base, .26f, .30f, {10, 15, 24}),      // near-black, reads as ground
        shade(base, 1.18f, .52f, {248, 250, 252}),  // near-white, reads as light
        shade(base, .70f, .58f, {74, 104, 128}),
        shade(base, .82f, .50f, {206, 166, 116}),
        shade(base, .46f, .62f, {34, 74, 72}),
    };
}

inline double nodeSpan(int depth) {
    double span = 1;
    for (int i = 0; i < depth; ++i) span /= Branch;
    return span;
}

inline uint64_t nodeSeed(uint64_t world, const NodeId& id) {
    uint64_t h = world * 0x9E3779B97F4A7C15ull;
    h ^= uint64_t(id.depth + 1) * 0xC2B2AE3D27D4EB4Full;
    h = (h ^ uint64_t(id.ix)) * 0x165667B19E3779F9ull;
    h = (h ^ uint64_t(id.iy)) * 0x27D4EB2F165667C5ull;
    h ^= h >> 31;
    return h;
}

// The path the beacon lives at, chosen from the world seed alone.
inline NodeId beaconNode(uint64_t world, int depth) {
    Rng rng(world ^ 0x5851F42D4C957F2Dull);
    NodeId id{depth, 0, 0};
    for (int d = 0; d < depth; ++d) {
        id.ix = id.ix * Branch + rng.below(Branch);
        id.iy = id.iy * Branch + rng.below(Branch);
    }
    return id;
}

inline int elementCount(Rng& rng) { return 6 + rng.below(5); }

// Generates one node's motifs. `beaconAt` marks which element (if any) is the
// beacon; every other element is forbidden from wearing both beacon features.
inline void nodeElements(uint64_t world, const NodeId& id, int beaconAt, std::vector<Element>& out) {
    out.clear();
    Rng rng(nodeSeed(world, id));
    double span = nodeSpan(id.depth);
    double x0 = double(id.ix) * span, y0 = double(id.iy) * span;
    int count = elementCount(rng);
    // Each depth leans on two palette entries, so zooming passes through
    // visibly different worlds instead of more of the same texture.
    int bias = (id.depth * 2 + 1) % PaletteSize;
    for (int i = 0; i < count; ++i) {
        Element element{};
        // One backdrop, a couple of mid forms, then detail. Composition is what
        // separates an artwork from noise at every zoom level.
        if (i == 0)      element.size = span * double(rng.range(.42f, .68f));
        else if (i <= 2) element.size = span * double(rng.range(.15f, .29f));
        else             element.size = span * double(rng.range(.032f, .105f));
        double inset = element.size * .6;
        element.x = x0 + inset + double(rng.unit()) * std::max(0.0, span - inset * 2);
        element.y = y0 + inset + double(rng.unit()) * std::max(0.0, span - inset * 2);
        element.spin = rng.range(0.f, 6.2831853f);
        element.motif = Motif(rng.below(MotifCount));
        element.palette = rng.unit() < .58f
            ? static_cast<unsigned char>((bias + rng.below(2)) % PaletteSize)
            : static_cast<unsigned char>(rng.below(PaletteSize));
        if (i == beaconAt) {
            element.motif = BeaconMotif;
            element.palette = BeaconPalette;
            element.beacon = true;
            element.size = span * .17;   // a fair size: present, not a speck
        } else if (element.motif == BeaconMotif && element.palette == BeaconPalette) {
            // The pairing belongs to the beacon alone, everywhere on the canvas.
            element.palette = static_cast<unsigned char>(1 + rng.below(PaletteSize - 1));
        }
        out.push_back(element);
    }
    // Largest first: detail must land on top of the forms it decorates.
    std::stable_sort(out.begin(), out.end(),
                     [](const Element& a, const Element& b) { return a.size > b.size; });
}

struct Canvas {
    uint64_t world = 0;
    int beaconDepth = 4;
    NodeId beaconId{};
    int beaconSlot = 0;
    Element beacon{};
    std::array<Rgb, PaletteSize> palette{};
    Rgb ground{}, deep{};
    bool found = false;

    bool isBeaconNode(const NodeId& id) const {
        return id.depth == beaconId.depth && id.ix == beaconId.ix && id.iy == beaconId.iy;
    }
};

inline Canvas makeCanvas(int track, Rgb trackColor, int beaconDepth) {
    Canvas canvas;
    canvas.world = uint64_t(track) * 7919u + 1013904223u;
    canvas.beaconDepth = std::clamp(beaconDepth, 1, MaxDepth - 2);
    canvas.beaconId = beaconNode(canvas.world, canvas.beaconDepth);
    canvas.palette = buildPalette(trackColor);
    canvas.ground = shade(trackColor, .30f, .74f, {14, 21, 33});
    canvas.deep = shade(trackColor, .18f, .84f, {7, 11, 18});
    Rng pick(nodeSeed(canvas.world, canvas.beaconId) ^ 0xD1B54A32D192ED03ull);
    Rng counter(nodeSeed(canvas.world, canvas.beaconId));
    canvas.beaconSlot = pick.below(elementCount(counter));
    std::vector<Element> elements;
    nodeElements(canvas.world, canvas.beaconId, canvas.beaconSlot, elements);
    for (const Element& element : elements)
        if (element.beacon) canvas.beacon = element;
    return canvas;
}

// How many motifs on the whole canvas, down to a depth, wear both beacon
// features. Must always be exactly one.
inline int countBeaconMatches(const Canvas& canvas, int depth) {
    int total = 0;
    std::vector<Element> elements;
    long long side = 1;
    for (int d = 0; d <= depth; ++d) {
        for (long long iy = 0; iy < side; ++iy)
            for (long long ix = 0; ix < side; ++ix) {
                NodeId id{d, ix, iy};
                nodeElements(canvas.world, id, canvas.isBeaconNode(id) ? canvas.beaconSlot : -1, elements);
                for (const Element& element : elements)
                    total += (element.motif == BeaconMotif && element.palette == BeaconPalette) ? 1 : 0;
            }
        side *= Branch;
    }
    return total;
}
}
