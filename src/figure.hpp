#pragma once
// What a person looks like in this world's art style.
//
// Drawn from a high oblique angle, not top-down: seen from straight above a
// person is a cap and two shoulders and carries no character at all. Flat
// shapes, a soft offset shadow, no outlines, and deliberately no faces.
//
// Limbs pivot at the joint, not their middle, which is what lets an arm point
// upward instead of spinning in place.
#include "scene.hpp"
#include "raylib.h"
#include "raymath.h"

namespace orbital {

// People keep their own colours rather than the terrain palette, so they read
// as human on every world. Clothing is deliberately brighter than the ground.
inline constexpr int SkinCount = 5, HairCount = 6, ClothCount = 12;

inline const std::array<Color, SkinCount> SkinTones = {
    Color{247, 214, 186, 255}, Color{226, 178, 140, 255}, Color{193, 138, 100, 255},
    Color{141, 92, 62, 255},   Color{86, 56, 40, 255}};
inline const std::array<Color, HairCount> HairTones = {
    Color{38, 30, 28, 255},  Color{86, 56, 36, 255},   Color{176, 137, 74, 255},
    Color{190, 92, 44, 255}, Color{150, 150, 156, 255}, Color{234, 234, 232, 255}};
inline const std::array<Color, ClothCount> ClothTones = {
    Color{206, 62, 62, 255},  Color{56, 104, 186, 255}, Color{74, 152, 96, 255},
    Color{232, 176, 58, 255}, Color{156, 84, 176, 255}, Color{62, 166, 176, 255},
    Color{232, 128, 78, 255}, Color{240, 240, 236, 255}, Color{70, 78, 92, 255},
    Color{188, 92, 132, 255}, Color{124, 148, 70, 255}, Color{158, 116, 78, 255}};

enum class Pattern : unsigned char { Solid, Stripe, Spot };

// arm/leg angles are degrees from hanging straight down, positive swinging one
// way. `drop` sinks the body, `legs` shortens them, `tilt` leans the whole
// figure about its feet.
// `pivot` is the local height the lean turns about; 0 is the feet, which is
// right for every lean here. Lying down is deliberately absent: a standing
// figure turned on its side is incoherent however it is pivoted, because the
// limbs swing with it. Sunbathers and sleepers need their own draw path.
struct Pose { float armL, armR, legL, legR, drop, legs, tilt, pivot; const char* name; };

inline constexpr int PoseCount = 24;
// Legs with opposite signs stride; legs with the same sign fold to one side,
// which is the only way a single-segment leg reads as sitting in this view.
inline const std::array<Pose, PoseCount> Poses = {{
    {   6,   -6,   3,  -3, 0.00f, 1.00f,   0,  0.00f, "stand"},
    {  22,  -18,   2,  -4, 0.00f, 1.00f,   0,  0.00f, "stand2"},
    {  34,  -30,   6,  -6, 0.00f, 1.00f,   0,  0.00f, "akimbo"},
    { 118, -114,   5,  -5, 0.00f, 1.00f,   0,  0.00f, "shrug"},
    { -28,   32, -20,  22, 0.00f, 1.00f,   0,  0.00f, "walk"},
    {  32,  -28,  22, -20, 0.00f, 1.00f,   0,  0.00f, "walk2"},
    { -48,   54, -30,  34, 0.01f, 1.00f,   7,  0.00f, "run"},
    {  64,  -70,  -8,   8, 0.00f, 1.00f,  -5,  0.00f, "stroll"},
    { 168,  -12,   4,  -4, 0.00f, 1.00f,   0,  0.00f, "wave"},
    { 172, -174,   3,  -3, 0.00f, 1.00f,   0,  0.00f, "cheer"},
    {  96,  -14,   5,  -5, 0.00f, 1.00f,   0,  0.00f, "point"},
    {  84,  -84,   4,  -4, 0.00f, 1.00f,   0,  0.00f, "carry"},
    { 140, -136,   4,  -4, 0.00f, 1.00f,   0,  0.00f, "reach"},
    {  40,  -36,   8,  -8, 0.00f, 1.00f,  32,  0.00f, "bow"},
    {  88,  -84, -18,  22, 0.00f, 1.00f,  16,  0.00f, "push"},
    { 128,  -40, -26,  30, 0.00f, 1.00f,  10,  0.00f, "dance"},
    { -40,  128,  30, -26, 0.00f, 1.00f, -10,  0.00f, "dance2"},
    { 150, -150, -22,  26, 0.02f, 1.00f,   0,  0.00f, "jump"},
    {  30,  -26,  14, -12, 0.00f, 1.00f,  19,  0.00f, "lean"},
    { -26,   30, -12,  14, 0.00f, 1.00f, -19,  0.00f, "lean2"},
    {  52,  -40,  78,  72, 0.16f, 0.80f,   0,  0.00f, "sit"},
    { -52,   40, -78, -72, 0.16f, 0.80f,   0,  0.00f, "sit2"},
    {  60,  -52,  92,  86, 0.20f, 0.62f,   0,  0.00f, "sit-cross"},
    {  20,  -16,  58,  52, 0.15f, 0.58f,   0,  0.00f, "kneel"},
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

inline Color darken(Color c, float amount) {
    return Color{(unsigned char)(c.r * amount), (unsigned char)(c.g * amount),
                 (unsigned char)(c.b * amount), c.a};
}

// `at` is the ground the figure stands on; `height` is its full height.
inline void drawFigure(const Figure& figure, Vector2 at, float height) {
    const Pose& pose = Poses[figure.pose % PoseCount];
    Color skin = SkinTones[figure.skin], hair = HairTones[figure.hair];
    Color shirt = ClothTones[figure.shirt], trousers = ClothTones[figure.trousers];
    float unit = height;
    float lean = pose.tilt * DEG2RAD, cosLean = std::cos(lean), sinLean = std::sin(lean);

    // Local space: x right, y down, origin at the feet. Everything leans about
    // the feet so a lying figure pivots where it touches the ground.
    auto place = [&](float x, float y) {
        float px = x * unit, py = (y + pose.drop) * unit, ay = pose.pivot * unit;
        float dx = px, dy = py - ay;
        return Vector2{at.x + dx * cosLean - dy * sinLean, at.y + ay + dx * sinLean + dy * cosLean};
    };
    auto slab = [&](float x, float y, float w, float h, float spin, Color tint) {
        Vector2 p = place(x, y);
        DrawRectanglePro({p.x, p.y, w * unit, h * unit}, {w * unit * .5f, h * unit * .5f},
                         pose.tilt + spin, tint);
    };
    // A limb hangs from its joint, so an angle of 0 points straight down.
    auto limb = [&](float jx, float jy, float w, float len, float spin, Color tint) {
        Vector2 p = place(jx, jy);
        DrawRectanglePro({p.x, p.y, w * unit, len * unit}, {w * unit * .5f, 0}, pose.tilt + spin, tint);
        float total = (pose.tilt + spin) * DEG2RAD;
        return Vector2{p.x - std::sin(total) * len * unit, p.y + std::cos(total) * len * unit};
    };

    float sprawl = std::abs(pose.tilt) / 90.f;
    DrawEllipse(int(at.x), int(at.y - .015f * unit), (.26f + sprawl * .34f) * unit,
                (.075f + sprawl * .02f) * unit, Fade(BLACK, .22f));

    float legLen = .34f * pose.legs;
    Vector2 footL = limb(-.075f, -.36f, .115f, legLen, pose.legL, trousers);
    Vector2 footR = limb( .075f, -.36f, .115f, legLen, pose.legR, trousers);
    DrawCircleV(footL, .062f * unit, darken(trousers, .42f));
    DrawCircleV(footR, .062f * unit, darken(trousers, .42f));

    slab(0, -.50f, .34f, .30f, 0, shirt);
    if (figure.pattern == Pattern::Stripe) {
        Color band = ClothTones[figure.stripe];
        for (int i = 0; i < 3; ++i) slab(0, -.595f + i * .078f, .34f, .040f, 0, band);
    } else if (figure.pattern == Pattern::Spot) {
        Color dot = ClothTones[figure.stripe];
        Vector2 a = place(-.07f, -.55f), b = place(.06f, -.45f);
        DrawCircleV(a, .036f * unit, dot);
        DrawCircleV(b, .036f * unit, dot);
    }

    Vector2 handL = limb(-.205f, -.61f, .088f, .29f, pose.armL, shirt);
    Vector2 handR = limb( .205f, -.61f, .088f, .29f, pose.armR, shirt);
    DrawCircleV(handL, .050f * unit, skin);
    DrawCircleV(handR, .050f * unit, skin);

    Vector2 head = place(0, -.775f);
    float headRadius = .155f * unit;
    DrawCircleV(head, headRadius, skin);
    // A skull cap: a half circle over the crown. Hair is the same shape in a
    // natural tone, so a hat reads by colour. Brims and bobbles come later.
    DrawCircleSector(head, headRadius * (figure.wearsHat ? 1.07f : 1.02f),
                     180 + pose.tilt, 360 + pose.tilt, 14,
                     figure.wearsHat ? ClothTones[figure.hat] : hair);
    if (figure.carries) DrawCircleV(handR, .078f * unit, ClothTones[figure.prop]);
}
}
