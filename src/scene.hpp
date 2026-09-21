#pragma once
// One generated background image per track: an illustrated place seen from
// above, with coast, forest, farmland, roads and towns.
//
// This is the backdrop layer only. Zones that override it at close zoom come
// later; everything here is meant to still read at a distance.
//
// Coordinates are image pixels, so what the generator says and what gets drawn
// are the same numbers. No raylib: generation stays testable without a window.
#include "mixer.hpp"
#include "hotreload.hpp"
#include "person.hpp"
#include "prop.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace orbital {

inline constexpr int SceneWidth = 2560, SceneHeight = 1440;
inline constexpr int PaletteSize = 6;

inline std::array<Rgb, PaletteSize> buildPalette(Rgb base) {
    return {base,
            shade(base, .30f, .28f, {12, 18, 28}),
            shade(base, 1.16f, .48f, {246, 249, 252}),
            shade(base, .72f, .56f, {78, 106, 130}),
            shade(base, .84f, .48f, {204, 164, 114}),
            shade(base, .48f, .60f, {36, 76, 74})};
}

// ---- terrain ---------------------------------------------------------------

inline float hashNoise(int x, int y, uint64_t seed) {
    uint64_t h = seed ^ (uint64_t(uint32_t(x)) * 0x9E3779B97F4A7C15ull)
                      ^ (uint64_t(uint32_t(y)) * 0xC2B2AE3D27D4EB4Full);
    h ^= h >> 29; h *= 0x165667B19E3779F9ull; h ^= h >> 32;
    return float(h >> 40) / 16777216.f;
}

inline float smoothNoise(float x, float y, uint64_t seed) {
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    float fx = x - float(ix), fy = y - float(iy);
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = hashNoise(ix, iy, seed), b = hashNoise(ix + 1, iy, seed);
    float c = hashNoise(ix, iy + 1, seed), d = hashNoise(ix + 1, iy + 1, seed);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

inline float fbm(float x, float y, uint64_t seed, int octaves = 5) {
    float sum = 0, amplitude = .5f, frequency = 1;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * smoothNoise(x * frequency, y * frequency, seed + uint64_t(i) * 7919u);
        frequency *= 2.03f;
        amplitude *= .5f;
    }
    return sum;
}

// Elevation in 0..1 over image pixels. An island-ish falloff keeps the sea at
// the edges so a scene reads as a place rather than a crop of noise.
inline float elevationAt(uint64_t seed, float px, float py) {
    float x = px / SceneWidth, y = py / SceneHeight;
    float base = fbm(x * 3.4f, y * 3.4f * SceneHeight / SceneWidth, seed);
    float dx = (x - .5f) * 2.f, dy = (y - .5f) * 2.f;
    float falloff = 1.f - std::clamp(std::sqrt(dx * dx * .78f + dy * dy) * .92f, 0.f, 1.f);
    return std::clamp(base * .72f + falloff * .52f, 0.f, 1.f);
}

inline float moistureAt(uint64_t seed, float px, float py) {
    return fbm(px / SceneWidth * 5.1f, py / SceneHeight * 3.1f, seed ^ 0xA5A5A5A5ull, 4);
}

inline constexpr float SeaLevel = .46f, ShoreLevel = .50f, HighLevel = .74f;
inline bool isLand(uint64_t seed, float x, float y) { return elevationAt(seed, x, y) > ShoreLevel; }

// ---- features --------------------------------------------------------------

struct Building { float x, y, w, h, spin; unsigned char roof; };
struct Town { float x, y, radius; std::vector<Building> buildings; };
struct Blob { float x, y, r; unsigned char tone; };
struct Patch { std::vector<Blob> blobs; };
struct Field { float x, y, w, h, spin; unsigned char tone; int furrows; };
struct Road { std::vector<std::pair<float, float>> points; };
struct Marker { float x, y, size; unsigned char palette; };   // scenery: waymarks on the land
// Every person belongs to a track's layer, and a layer is only drawn while its
// track is playing. A world visibly fills up as the mix does, so a world you
// already searched has people in it you have never seen.
//
// Drawn live rather than baked, so they stay sharp as you zoom.
struct PersonSpot { float x, y, height; unsigned char layer; int skit; Figure figure; };

// A skit is a little vignette: a queue, a ring of talkers, a chase, a picnic.
// Nobody is scattered on their own account -- every person belongs to one,
// even a lone wanderer, which is a Stroll of one. Crowds made of arrangements
// read as a place; crowds made of random dots read as noise.
//
// A skit is not tied to one track but to a *configuration* of them: a set that
// must be sounding and a set that must be silent. Most want a single track and
// behave like a layer. Some want a pair, and appear only once both are up.
// A few want something off as well, so they can only be found by muting
// something -- content that a finished campaign still has to be mixed into
// existence.
enum class SkitKind : unsigned char { Queue, Ring, Chase, Pair, Audience, Picnic, Work, Stroll };
inline constexpr int SkitKindCount = 8;
inline const char* skitName(SkitKind kind) {
    switch (kind) {
        case SkitKind::Queue: return "queue";
        case SkitKind::Ring: return "ring";
        case SkitKind::Chase: return "chase";
        case SkitKind::Pair: return "pair";
        case SkitKind::Audience: return "audience";
        case SkitKind::Picnic: return "picnic";
        case SkitKind::Work: return "work";
        default: return "stroll";
    }
}
// Which prop belongs with which vignette. A skit is a reason for people to be
// somewhere, and the prop is usually that reason: a queue needs something to
// queue for.
inline PropKind propFor(SkitKind kind, Rng& rng) {
    switch (kind) {
        case SkitKind::Queue:    { static const PropKind any[] = {PropKind::Stall, PropKind::Well, PropKind::Cart};
                                   return any[rng.below(3)]; }
        case SkitKind::Ring:     { static const PropKind any[] = {PropKind::Fire, PropKind::Fountain, PropKind::Well};
                                   return any[rng.below(3)]; }
        case SkitKind::Chase:    { static const PropKind any[] = {PropKind::Cart, PropKind::Signpost, PropKind::Bench};
                                   return any[rng.below(3)]; }
        case SkitKind::Pair:     { static const PropKind any[] = {PropKind::Bench, PropKind::Signpost, PropKind::Well};
                                   return any[rng.below(3)]; }
        case SkitKind::Audience: { static const PropKind any[] = {PropKind::Stage, PropKind::Fire, PropKind::Stall};
                                   return any[rng.below(3)]; }
        case SkitKind::Picnic:   { static const PropKind any[] = {PropKind::Blanket, PropKind::Fire, PropKind::Boat};
                                   return any[rng.below(3)]; }
        case SkitKind::Work:     { static const PropKind any[] = {PropKind::Haystack, PropKind::Cart, PropKind::Well};
                                   return any[rng.below(3)]; }
        default:                 { static const PropKind any[] = {PropKind::Signpost, PropKind::Tent, PropKind::Bench};
                                   return any[rng.below(3)]; }
    }
}

struct Skit {
    SkitKind kind;
    float x, y;
    uint32_t wants = 0;    // every one of these tracks must be sounding
    uint32_t hides = 0;    // none of these may be
    unsigned char primary; // the track it belongs to first
    int members;
    int prop = -1;   // every skit has one; index into Scene::props
    float minX = 0, minY = 0, maxX = 0, maxY = 0;   // where its people actually ended up
    bool showing(uint32_t playing) const { return (playing & wants) == wants && (playing & hides) == 0; }
};

struct Scene {
    uint64_t seed = 0;
    std::array<Rgb, PaletteSize> palette{};
    Rgb sea{}, shallow{}, sand{}, grass{}, highland{}, forest{};
    std::vector<Town> towns;
    std::vector<Patch> woods;
    std::vector<Field> fields;
    std::vector<Road> roads;
    std::vector<Marker> markers;
    std::vector<PersonSpot> people;
    std::vector<Skit> skits;
    std::vector<Prop> props;
    int target = -1;   // the person the find box shows; always in this world's own layer
    bool found = false;
};

inline Scene generateScene(int track, Rgb trackColor, int layerCount, uint64_t campaignSeed = 0) {
    Scene scene;
    uint64_t mixed = (campaignSeed ? campaignSeed : 1013904223ull) ^ (uint64_t(track + 1) * 0x9E3779B97F4A7C15ull);
    mixed ^= mixed >> 29; mixed *= 0xBF58476D1CE4E5B9ull; mixed ^= mixed >> 32;
    scene.seed = mixed;
    scene.palette = buildPalette(trackColor);
    // Terrain stays naturalistic but takes a tint from the track, so the seven
    // worlds read as different places rather than recolours of one.
    scene.sea      = shade(trackColor, .34f, .74f, {18, 42, 74});
    scene.shallow  = shade(trackColor, .52f, .66f, {46, 96, 126});
    scene.sand     = shade(trackColor, .92f, .52f, {216, 198, 152});
    scene.grass    = shade(trackColor, .64f, .60f, {96, 132, 84});
    scene.highland = shade(trackColor, .70f, .58f, {138, 132, 118});
    scene.forest   = shade(trackColor, .44f, .66f, {48, 84, 60});
    Rng rng(scene.seed ^ 0x51ED270Bull);

    // Towns first: everything else is placed relative to them.
    for (int attempt = 0; attempt < 900 && int(scene.towns.size()) < 9; ++attempt) {
        float x = rng.range(SceneWidth * .10f, SceneWidth * .90f);
        float y = rng.range(SceneHeight * .12f, SceneHeight * .88f);
        if (elevationAt(scene.seed, x, y) < ShoreLevel + .03f) continue;
        bool crowded = false;
        for (const Town& other : scene.towns)
            crowded |= (other.x - x) * (other.x - x) + (other.y - y) * (other.y - y) < 330.f * 330.f;
        if (crowded) continue;
        Town town{x, y, rng.range(70.f, 165.f), {}};
        int count = 16 + rng.below(30);
        for (int i = 0; i < count; ++i) {
            float angle = rng.range(0, 6.2831853f), reach = town.radius * std::sqrt(rng.unit());
            Building building{};
            building.x = x + std::cos(angle) * reach;
            building.y = y + std::sin(angle) * reach;
            building.w = rng.range(13.f, 34.f);
            building.h = rng.range(13.f, 30.f);
            building.spin = rng.range(-.35f, .35f);
            building.roof = static_cast<unsigned char>(1 + rng.below(PaletteSize - 1));
            if (elevationAt(scene.seed, building.x, building.y) > ShoreLevel)
                town.buildings.push_back(building);
        }
        if (town.buildings.size() > 8) scene.towns.push_back(town);
    }

    // Roads: a greedy nearest-neighbour chain, so every town is reachable.
    if (scene.towns.size() > 1) {
        std::vector<int> left;
        for (int i = 1; i < int(scene.towns.size()); ++i) left.push_back(i);
        int at = 0;
        while (!left.empty()) {
            auto nearest = std::min_element(left.begin(), left.end(), [&](int a, int b) {
                auto span = [&](int i) {
                    float dx = scene.towns[size_t(i)].x - scene.towns[size_t(at)].x;
                    float dy = scene.towns[size_t(i)].y - scene.towns[size_t(at)].y;
                    return dx * dx + dy * dy;
                };
                return span(a) < span(b);
            });
            int to = *nearest;
            left.erase(nearest);
            Road road;
            float x0 = scene.towns[size_t(at)].x, y0 = scene.towns[size_t(at)].y;
            float x1 = scene.towns[size_t(to)].x, y1 = scene.towns[size_t(to)].y;
            int steps = 7;
            for (int i = 0; i <= steps; ++i) {
                float t = float(i) / float(steps);
                float wobble = (i == 0 || i == steps) ? 0.f : rng.range(-58.f, 58.f);
                road.points.emplace_back(x0 + (x1 - x0) * t - (y1 - y0) * wobble / 900.f,
                                         y0 + (y1 - y0) * t + (x1 - x0) * wobble / 900.f);
            }
            scene.roads.push_back(std::move(road));
            at = to;
        }
    }

    // Woods where it is damp and not too high.
    for (int attempt = 0; attempt < 2600 && int(scene.woods.size()) < 90; ++attempt) {
        float x = rng.range(0, float(SceneWidth)), y = rng.range(0, float(SceneHeight));
        float height = elevationAt(scene.seed, x, y);
        if (height < ShoreLevel + .02f || height > HighLevel) continue;
        if (moistureAt(scene.seed, x, y) < .48f) continue;
        bool nearTown = false;
        for (const Town& town : scene.towns)
            nearTown |= (town.x - x) * (town.x - x) + (town.y - y) * (town.y - y) < town.radius * town.radius * 1.7f;
        if (nearTown) continue;
        Patch patch;
        int trees = 12 + rng.below(26);
        float spread = rng.range(40.f, 120.f);
        for (int i = 0; i < trees; ++i) {
            float angle = rng.range(0, 6.2831853f), reach = spread * std::sqrt(rng.unit());
            float bx = x + std::cos(angle) * reach, by = y + std::sin(angle) * reach;
            if (elevationAt(scene.seed, bx, by) < ShoreLevel) continue;
            patch.blobs.push_back({bx, by, rng.range(7.f, 17.f), static_cast<unsigned char>(rng.below(3))});
        }
        if (patch.blobs.size() > 6) scene.woods.push_back(std::move(patch));
    }

    // Farmland ringing the towns.
    for (const Town& town : scene.towns) {
        int count = 4 + rng.below(7);
        for (int i = 0; i < count; ++i) {
            float angle = rng.range(0, 6.2831853f), reach = town.radius * rng.range(1.3f, 2.7f);
            Field field{};
            field.x = town.x + std::cos(angle) * reach;
            field.y = town.y + std::sin(angle) * reach;
            field.w = rng.range(70.f, 190.f);
            field.h = rng.range(50.f, 130.f);
            field.spin = rng.range(-.5f, .5f);
            field.tone = static_cast<unsigned char>(3 + rng.below(3));
            field.furrows = 4 + rng.below(7);
            if (elevationAt(scene.seed, field.x, field.y) > ShoreLevel + .015f
                && elevationAt(scene.seed, field.x, field.y) < HighLevel)
                scene.fields.push_back(field);
        }
    }

    // Waymarks: small spires dotted over the land, pure scenery.
    for (int attempt = 0; attempt < 2000 && int(scene.markers.size()) < 30; ++attempt) {
        float x = rng.range(SceneWidth * .05f, SceneWidth * .95f);
        float y = rng.range(SceneHeight * .06f, SceneHeight * .94f);
        if (elevationAt(scene.seed, x, y) < ShoreLevel + .02f) continue;
        bool crowded = false;
        for (const Marker& other : scene.markers)
            crowded |= (other.x - x) * (other.x - x) + (other.y - y) * (other.y - y) < 150.f * 150.f;
        if (crowded) continue;
        scene.markers.push_back({x, y, rng.range(15.f, 23.f),
                                 static_cast<unsigned char>(rng.below(PaletteSize))});
    }
    // Where a skit can happen, and what sort of place it is.
    struct Anchor { float x, y; unsigned char sort; };   // 0 town, 1 road, 2 field, 3 shore
    std::vector<Anchor> anchors;
    for (const Town& town : scene.towns) {
        int spots = 3 + int(town.radius / 40);
        for (int i = 0; i < spots; ++i) {
            float angle = rng.range(0, 6.2831853f), reach = town.radius * std::sqrt(rng.unit());
            anchors.push_back({town.x + std::cos(angle) * reach, town.y + std::sin(angle) * reach, 0});
        }
    }
    for (const Road& road : scene.roads)
        for (size_t i = 0; i + 1 < road.points.size(); ++i)
            anchors.push_back({road.points[i].first, road.points[i].second, 1});
    for (const Field& field : scene.fields) anchors.push_back({field.x, field.y, 2});
    size_t inland = anchors.size(), shoreWanted = inland / 4 + 8;
    for (int attempt = 0; attempt < 900 && anchors.size() - inland < shoreWanted; ++attempt) {
        float x = rng.range(0, float(SceneWidth)), y = rng.range(0, float(SceneHeight));
        float height = elevationAt(scene.seed, x, y);
        if (height > ShoreLevel - .025f && height < ShoreLevel + .05f) anchors.push_back({x, y, 3});
    }

    // People, one set per track, placed as skits. Each layer is built the same
    // way from its own seed, so turning a track on adds vignettes that were
    // never there before.
    for (int layer = 0; layer < layerCount && !anchors.empty(); ++layer) {
        Rng crowd(scene.seed ^ (uint64_t(layer + 1) * 0x9E3779B97F4A7C15ull));
        int skitCount = 52 + crowd.below(14);
        for (int i = 0; i < skitCount; ++i) {
            const Anchor& anchor = anchors[size_t(crowd.below(int(anchors.size())))];

            // What happens somewhere depends on where it is.
            SkitKind kind;
            switch (anchor.sort) {
                case 0: { static const SkitKind town[] = {SkitKind::Queue, SkitKind::Queue, SkitKind::Ring,
                                                          SkitKind::Ring, SkitKind::Audience, SkitKind::Audience,
                                                          SkitKind::Pair, SkitKind::Chase, SkitKind::Stroll};
                          kind = town[crowd.below(9)]; break; }
                case 1: { static const SkitKind road[] = {SkitKind::Chase, SkitKind::Chase, SkitKind::Stroll,
                                                          SkitKind::Pair, SkitKind::Work};
                          kind = road[crowd.below(5)]; break; }
                case 2: { static const SkitKind farm[] = {SkitKind::Work, SkitKind::Work, SkitKind::Picnic,
                                                          SkitKind::Pair};
                          kind = farm[crowd.below(4)]; break; }
                default: { static const SkitKind shore[] = {SkitKind::Picnic, SkitKind::Picnic, SkitKind::Stroll,
                                                            SkitKind::Ring};
                           kind = shore[crowd.below(4)]; break; }
            }

            int skitIndex = int(scene.skits.size());
            int placed = 0;

            // The configuration this skit needs. Most are a single track, so a
            // world still fills in as the mix does. A quarter want a second
            // track as well, and a few want one silent, which is the only way
            // to reach them.
            uint32_t wants = 1u << layer, hides = 0;
            if (layerCount > 1) {
                float roll = crowd.unit();
                if (roll > .68f) {
                    int partner = crowd.below(layerCount - 1);
                    if (partner >= layer) ++partner;
                    wants |= 1u << partner;
                }
                if (roll > .90f) {
                    for (int attempt = 0; attempt < 8; ++attempt) {
                        int quiet = crowd.below(layerCount);
                        if (!((wants >> quiet) & 1u)) { hides = 1u << quiet; break; }
                    }
                }
            }
            float facing = crowd.range(0, 6.2831853f);
            auto put = [&](float x, float y, unsigned char pose) {
                if (elevationAt(scene.seed, x, y) < SeaLevel + .015f) return;
                if (x < 8 || y < 8 || x > SceneWidth - 8 || y > SceneHeight - 8) return;
                Figure figure = rollFigure(crowd);
                figure.pose = pose;            // the skit decides what they are doing
                scene.people.push_back({x, y, crowd.range(11.f, 16.f),
                                        static_cast<unsigned char>(layer), skitIndex, figure});
                ++placed;
            };
            auto pick = [&](std::initializer_list<int> poses) {
                return static_cast<unsigned char>(*(poses.begin() + crowd.below(int(poses.size()))));
            };
            float step = crowd.range(17.f, 24.f);
            float dx = std::cos(facing), dy = std::sin(facing) * .7f;   // the map is seen from above

            switch (kind) {
                case SkitKind::Queue: {
                    int count = 3 + crowd.below(4);
                    for (int j = 0; j < count; ++j)
                        put(anchor.x + dx * step * float(j), anchor.y + dy * step * float(j),
                            j == 0 ? pick({12, 10}) : pick({0, 1, 3}));   // reach/point at the head
                    break;
                }
                case SkitKind::Ring: {
                    int count = 3 + crowd.below(3);
                    float radius = crowd.range(18.f, 27.f);
                    for (int j = 0; j < count; ++j) {
                        float a = facing + float(j) * 6.2831853f / float(count);
                        put(anchor.x + std::cos(a) * radius, anchor.y + std::sin(a) * radius * .7f,
                            pick({0, 2, 3, 10}));
                    }
                    break;
                }
                case SkitKind::Chase: {
                    int count = 2 + crowd.below(2);
                    for (int j = 0; j < count; ++j)
                        put(anchor.x + dx * step * 1.7f * float(j), anchor.y + dy * step * 1.7f * float(j),
                            pick({6, 4, 5}));
                    break;
                }
                case SkitKind::Pair: {
                    put(anchor.x, anchor.y, pick({10, 2, 3}));
                    put(anchor.x + dx * step, anchor.y + dy * step, pick({3, 2, 0}));
                    break;
                }
                case SkitKind::Audience: {
                    put(anchor.x, anchor.y, pick({15, 16, 17, 9}));   // the turn
                    int count = 4 + crowd.below(5);
                    for (int j = 0; j < count; ++j) {
                        float spread = (float(j % 4) - 1.5f) * step;
                        float row = step * 1.5f * (1.f + float(j / 4));
                        put(anchor.x - dy * spread + dx * row, anchor.y + dx * spread + dy * row,
                            pick({0, 1, 9, 3}));
                    }
                    break;
                }
                case SkitKind::Picnic: {
                    int count = 3 + crowd.below(3);
                    for (int j = 0; j < count; ++j) {
                        float a = facing + float(j) * 6.2831853f / float(count);
                        float radius = crowd.range(12.f, 22.f);
                        put(anchor.x + std::cos(a) * radius, anchor.y + std::sin(a) * radius * .7f,
                            pick({20, 21, 22, 23}));
                    }
                    break;
                }
                case SkitKind::Work: {
                    int count = 2 + crowd.below(3);
                    for (int j = 0; j < count; ++j)
                        put(anchor.x + crowd.range(-26.f, 26.f), anchor.y + crowd.range(-18.f, 18.f),
                            pick({11, 14, 13, 23}));
                    break;
                }
                default: {   // Stroll: one or two wanderers, still a skit
                    int count = 1 + crowd.below(2);
                    for (int j = 0; j < count; ++j)
                        put(anchor.x + crowd.range(-14.f, 14.f) + dx * step * float(j),
                            anchor.y + crowd.range(-10.f, 10.f) + dy * step * float(j),
                            pick({4, 5, 7}));
                    break;
                }
            }
            if (placed > 0) {
                // Every skit gets its object. A queue with nothing to queue
                // for is just a line of people.
                Prop prop{};
                prop.kind = propFor(kind, crowd);
                prop.size = crowd.range(26.f, 40.f) * propScale(prop.kind);
                prop.spin = crowd.range(-.12f, .12f);
                prop.palette = static_cast<unsigned char>(crowd.below(PaletteSize));
                // Set a little back from the group so it is not buried in them.
                prop.x = anchor.x - dx * step * 1.15f;
                prop.y = anchor.y - dy * step * 1.15f;
                if (elevationAt(scene.seed, prop.x, prop.y) < SeaLevel + .015f) {
                    prop.x = anchor.x; prop.y = anchor.y;
                }
                scene.skits.push_back({kind, anchor.x, anchor.y, wants, hides,
                                       static_cast<unsigned char>(layer), placed,
                                       int(scene.props.size())});
                scene.props.push_back(prop);
            }
        }
    }

    // Painter's order across every layer at once, so switching a layer on drops
    // its people into the right depth rather than on top of everything.
    std::sort(scene.people.begin(), scene.people.end(),
              [](const PersonSpot& a, const PersonSpot& b) { return a.y < b.y; });

    // The target lives in this world's own layer, which is always showing while
    // you are here -- a world whose track is silent cannot be reached. Nobody in
    // any layer may wear the same outfit, since every layer can become visible.
    std::vector<int> own;
    for (int i = 0; i < int(scene.people.size()); ++i)
        if (scene.people[size_t(i)].layer == track % std::max(1, layerCount)) own.push_back(i);
    if (!own.empty()) {
        scene.target = own[size_t(rng.below(int(own.size())))];
        // The target's own skit must ask for nothing but this world's track,
        // or the hunt could need a mix the player has no way to guess.
        Skit& hosting = scene.skits[size_t(scene.people[size_t(scene.target)].skit)];
        hosting.wants = 1u << (track % std::max(1, layerCount));
        hosting.hides = 0;
        // Nobody may stand in front of the target. People are drawn in order of
        // y, so someone slightly below them covers them completely -- clicking
        // still works, but the hunt is unwinnable because they cannot be seen.
        // Skits cluster people, which makes this likely rather than rare.
        PersonSpot& mark = scene.people[size_t(scene.target)];
        for (PersonSpot& other : scene.people) {
            if (&other == &mark) continue;
            float dx = other.x - mark.x, dy = other.y - mark.y;
            float near = mark.height * .75f;
            if (dy > -mark.height * .15f && dy < near && std::abs(dx) < near) {
                float push = near + 4.f - std::abs(dx);
                other.x += dx < 0 ? -push : push;   // step aside, same skit, same place
            }
        }
        const Figure wanted = scene.people[size_t(scene.target)].figure;
        for (int i = 0; i < int(scene.people.size()); ++i) {
            if (i == scene.target) continue;
            Figure& other = scene.people[size_t(i)].figure;
            for (int guard = 0; guard < 8 && sameOutfit(wanted, other); ++guard) {
                other.shirt = static_cast<unsigned char>(rng.below(ClothCount));
                other.hat = static_cast<unsigned char>(rng.below(ClothCount));
            }
        }
    }

    // Skit bounds last of all, so they include the nudge above. Taken from the
    // people only: the anchor is where a skit was aimed, not where it landed,
    // and including it inflates every outline.
    std::vector<bool> started(scene.skits.size(), false);
    for (const PersonSpot& spot : scene.people) {
        Skit& skit = scene.skits[size_t(spot.skit)];
        if (!started[size_t(spot.skit)]) {
            started[size_t(spot.skit)] = true;
            skit.minX = skit.maxX = spot.x;
            skit.minY = spot.y - spot.height;
            skit.maxY = spot.y;
        }
    }
    for (const PersonSpot& spot : scene.people) {
        Skit& skit = scene.skits[size_t(spot.skit)];
        skit.minX = std::min(skit.minX, spot.x);
        skit.maxX = std::max(skit.maxX, spot.x);
        skit.minY = std::min(skit.minY, spot.y - spot.height);
        skit.maxY = std::max(skit.maxY, spot.y);
    }
    return scene;
}

// Nobody but the target may wear the target's outfit.
inline int countOutfitMatches(const Scene& scene) {
    if (scene.target < 0) return 0;
    int total = 0;
    for (const PersonSpot& spot : scene.people)
        total += sameOutfit(scene.people[size_t(scene.target)].figure, spot.figure) ? 1 : 0;
    return total;
}
}
