#pragma once
// Draws a person in the world's art style: flat shapes, a soft offset shadow,
// no outlines, no face, a half-circle skull cap for headwear.
//
// Drawn from a high oblique angle, not top-down: seen from straight above a
// person is a cap and two shoulders and carries no character at all.
//
// Limbs pivot at the joint, not their middle, which is what lets an arm point
// upward instead of spinning in place.
#include "person.hpp"
#include "raylib.h"
#include "raymath.h"

namespace orbital {

inline Color toColor(Rgb c) { return Color{c.r, c.g, c.b, 255}; }

inline Color darken(Color c, float amount) {
    return Color{(unsigned char)(c.r * amount), (unsigned char)(c.g * amount),
                 (unsigned char)(c.b * amount), c.a};
}

// `at` is the ground the figure stands on; `height` is its full height.
inline void drawFigure(const Figure& figure, Vector2 at, float height) {
    const Pose& pose = Poses[figure.pose % PoseCount];
    Color skin = toColor(SkinTones[figure.skin]), hair = toColor(HairTones[figure.hair]);
    Color shirt = toColor(ClothTones[figure.shirt]), trousers = toColor(ClothTones[figure.trousers]);
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
        Color band = toColor(ClothTones[figure.stripe]);
        for (int i = 0; i < 3; ++i) slab(0, -.595f + i * .078f, .34f, .040f, 0, band);
    } else if (figure.pattern == Pattern::Spot) {
        Color dot = toColor(ClothTones[figure.stripe]);
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
                     figure.wearsHat ? toColor(ClothTones[figure.hat]) : hair);
    if (figure.carries) DrawCircleV(handR, .078f * unit, toColor(ClothTones[figure.prop]));
}
}
