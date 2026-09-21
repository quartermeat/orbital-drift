#pragma once
// Drawing the objects skits gather around. Same flat style as the figures:
// solid shapes, a soft offset shadow, no outlines.
//
// These are judged by `od recognise`, which renders each one alone and asks a
// local vision model to describe it. A silhouette that only reads as "some
// shapes" fails, which is the point: the test keeps the drawings honest.
#include "prop.hpp"
#include "figure.hpp"

namespace orbital {

// `at` is the ground the prop stands on; `size` is its height in pixels.
inline void drawProp(const Prop& prop, Vector2 at, float size, Rgb tint) {
    Color body = toColor(tint);
    Color dark = darken(body, .58f), light = toColor(shade(tint, 1.18f, .34f, {250, 250, 245}));
    Color wood = Color{146, 104, 66, 255}, woodDark = darken(wood, .62f);
    Color shadow = Fade(BLACK, .22f);
    float unit = size;
    auto slab = [&](float x, float y, float w, float h, float spin, Color colour) {
        DrawRectanglePro({at.x + x * unit, at.y + y * unit, w * unit, h * unit},
                         {w * unit * .5f, h * unit * .5f}, spin, colour);
    };
    DrawEllipse(int(at.x), int(at.y), .52f * unit, .12f * unit, shadow);

    switch (prop.kind) {
        case PropKind::Stall: {          // counter on legs, goods, striped canopy
            slab(-.44f, -.20f, .06f, .40f, 0, woodDark);    // legs, clear of the ground
            slab( .44f, -.20f, .06f, .40f, 0, woodDark);
            slab(0, -.44f, 1.00f, .14f, 0, wood);           // counter top
            slab(0, -.34f, .96f, .07f, 0, woodDark);
            for (int i = -2; i <= 2; ++i) {                 // goods on the counter
                Color crate = (i % 2) ? light : Color{196, 128, 74, 255};
                slab(float(i) * .19f, -.58f, .15f, .14f, 0, crate);
            }
            slab(-.50f, -.80f, .05f, .46f, 0, woodDark);    // canopy posts
            slab( .50f, -.80f, .05f, .46f, 0, woodDark);
            slab(0, -.88f, .96f, .34f, 0, darken(wood, .86f));   // back wall behind the goods
            slab(0, -1.02f, 1.30f, .20f, 0, light);              // awning
            for (int i = -2; i <= 2; ++i) slab(float(i) * .26f, -1.02f, .13f, .20f, 0, body);
            slab(0, -.90f, 1.30f, .05f, 0, darken(body, .7f));   // valance along the front edge
            break;
        }
        case PropKind::Cart: {           // bed, shafts, two spoked wheels
            slab(0, -.42f, .90f, .30f, 0, wood);
            slab(0, -.60f, .78f, .10f, 0, woodDark);
            slab(.62f, -.34f, .38f, .05f, -12, woodDark);   // shaft
            for (float side : {-.28f, .28f}) {
                DrawCircleV({at.x + side * unit, at.y - .16f * unit}, .20f * unit, dark);
                DrawCircleV({at.x + side * unit, at.y - .16f * unit}, .13f * unit, light);
                DrawCircleV({at.x + side * unit, at.y - .16f * unit}, .04f * unit, dark);
            }
            break;
        }
        case PropKind::Fire: {           // a stack of logs under layered flames
            Color ember{120, 66, 44, 255};
            slab(-.02f, -.07f, .86f, .15f, -14, wood);      // crossed logs, ends showing
            slab( .02f, -.07f, .86f, .15f, 14, woodDark);
            slab(0, -.20f, .70f, .13f, -6, wood);
            for (float side : {-.42f, .42f}) {              // cut ends read as timber
                DrawCircleV({at.x + side * unit, at.y - .09f * unit}, .075f * unit, ember);
                DrawCircleV({at.x + side * unit, at.y - .09f * unit}, .040f * unit, darken(ember, .7f));
            }
            // Rounded, overlapping tongues. Triangles read as a tree.
            Color outer{228, 104, 38, 255}, mid{242, 152, 44, 255}, core{250, 222, 104, 255};
            DrawCircleV({at.x - .10f * unit, at.y - .40f * unit}, .26f * unit, outer);
            DrawCircleV({at.x + .14f * unit, at.y - .36f * unit}, .22f * unit, outer);
            DrawCircleV({at.x + .02f * unit, at.y - .60f * unit}, .20f * unit, outer);
            DrawTriangle({at.x - .04f * unit, at.y - .96f * unit}, {at.x - .22f * unit, at.y - .44f * unit},
                         {at.x + .16f * unit, at.y - .48f * unit}, outer);
            DrawCircleV({at.x, at.y - .40f * unit}, .17f * unit, mid);
            DrawCircleV({at.x + .06f * unit, at.y - .56f * unit}, .12f * unit, mid);
            DrawCircleV({at.x, at.y - .38f * unit}, .10f * unit, core);
            for (int i = 0; i < 3; ++i)                     // sparks
                DrawCircleV({at.x + (float(i) - 1) * .18f * unit, at.y - (.92f + float(i) * .06f) * unit},
                            .026f * unit, core);
            break;
        }
        case PropKind::Fountain: {       // tiered stone basin with a jet of water
            Color stone{198, 194, 186, 255}, stoneDark{152, 148, 140, 255};
            Color water{86, 164, 206, 255}, foam{176, 218, 238, 255};
            DrawEllipse(int(at.x), int(at.y - .06f * unit), .60f * unit, .22f * unit, stoneDark);
            DrawEllipse(int(at.x), int(at.y - .12f * unit), .54f * unit, .19f * unit, stone);
            DrawEllipse(int(at.x), int(at.y - .14f * unit), .44f * unit, .14f * unit, water);
            slab(0, -.34f, .14f, .34f, 0, stone);           // pedestal
            DrawEllipse(int(at.x), int(at.y - .52f * unit), .30f * unit, .10f * unit, stone);
            DrawEllipse(int(at.x), int(at.y - .54f * unit), .24f * unit, .07f * unit, water);
            slab(0, -.72f, .07f, .24f, 0, foam);            // the jet
            DrawCircleV({at.x, at.y - .88f * unit}, .09f * unit, foam);
            for (float side : {-.20f, .20f}) {              // water falling back
                DrawCircleV({at.x + side * unit, at.y - .74f * unit}, .045f * unit, foam);
                DrawCircleV({at.x + side * 1.4f * unit, at.y - .56f * unit}, .035f * unit, foam);
            }
            break;
        }
        case PropKind::Bench: {          // seat, back, legs
            slab(-.34f, -.14f, .08f, .30f, 0, woodDark);
            slab( .34f, -.14f, .08f, .30f, 0, woodDark);
            slab(0, -.30f, .92f, .12f, 0, wood);            // seat
            slab(-.40f, -.52f, .07f, .46f, 0, woodDark);
            slab( .40f, -.52f, .07f, .46f, 0, woodDark);
            slab(0, -.62f, .92f, .10f, 0, wood);            // back rail
            slab(0, -.48f, .92f, .08f, 0, wood);
            break;
        }
        case PropKind::Stage: {          // platform with a backboard
            slab(0, -.12f, 1.10f, .24f, 0, wood);
            slab(0, -.30f, 1.02f, .14f, 0, woodDark);
            slab(0, -.66f, .86f, .58f, 0, body);            // backboard
            slab(0, -.66f, .74f, .46f, 0, light);
            break;
        }
        case PropKind::Blanket: {        // checked rug with a basket
            slab(0, -.06f, 1.04f, .34f, prop.spin * RAD2DEG, body);
            for (int i = -2; i <= 2; ++i) slab(float(i) * .22f, -.06f, .07f, .34f, prop.spin * RAD2DEG, light);
            slab(0, -.06f, 1.04f, .06f, prop.spin * RAD2DEG, light);
            slab(.30f, -.26f, .28f, .22f, 0, wood);         // basket
            slab(.30f, -.38f, .30f, .06f, 0, woodDark);
            break;
        }
        case PropKind::Haystack: {       // a rough dome of straw, not a ziggurat
            Color straw{224, 188, 92, 255}, strawLit{242, 214, 132, 255}, strawDark{182, 146, 62, 255};
            // Rounded mass: overlapping ellipses, widest at the bottom. The
            // banded cone this replaced was read, correctly, as a pyramid.
            DrawEllipse(int(at.x), int(at.y - .14f * unit), .50f * unit, .20f * unit, strawDark);
            DrawEllipse(int(at.x), int(at.y - .30f * unit), .46f * unit, .26f * unit, straw);
            DrawEllipse(int(at.x), int(at.y - .52f * unit), .34f * unit, .22f * unit, straw);
            DrawEllipse(int(at.x), int(at.y - .70f * unit), .20f * unit, .16f * unit, strawLit);
            DrawEllipse(int(at.x - .12f * unit), int(at.y - .44f * unit), .16f * unit, .13f * unit, strawLit);
            // Straw lies along the slope, so the strokes radiate rather than ring.
            for (int i = -5; i <= 5; ++i) {
                float lean = float(i) * 8.f;
                float x = float(i) * .075f, y = -.34f - std::abs(float(i)) * .02f;
                DrawRectanglePro({at.x + x * unit, at.y + y * unit, .035f * unit, .40f * unit},
                                 {.018f * unit, .20f * unit}, lean, strawDark);
            }
            for (int i = -4; i <= 4; ++i)   // loose stalks poking out at the foot
                DrawRectanglePro({at.x + float(i) * .11f * unit, at.y - .05f * unit, .028f * unit, .13f * unit},
                                 {.014f * unit, .065f * unit}, float(i) * 13.f, strawDark);
            break;
        }
        case PropKind::Signpost: {       // stout post with pointed arrow boards
            slab(0, -.50f, .14f, 1.00f, 0, wood);
            slab(0, -.99f, .22f, .09f, 0, woodDark);        // cap
            // Upper board pointing left, lower pointing right, both with points.
            slab(-.28f, -.78f, .56f, .19f, 0, light);
            DrawTriangle({at.x - .70f * unit, at.y - .78f * unit},
                         {at.x - .56f * unit, at.y - .875f * unit},
                         {at.x - .56f * unit, at.y - .685f * unit}, light);
            slab( .28f, -.50f, .56f, .19f, 0, body);
            DrawTriangle({at.x + .70f * unit, at.y - .50f * unit},
                         {at.x + .56f * unit, at.y - .595f * unit},
                         {at.x + .56f * unit, at.y - .405f * unit}, body);
            for (int i = 0; i < 3; ++i) {                   // lettering, so it reads as a sign
                slab(-.34f + float(i) * .13f, -.78f, .09f, .04f, 0, darken(light, .55f));
                slab( .16f + float(i) * .13f, -.50f, .09f, .04f, 0, darken(body, .55f));
            }
            break;
        }
        case PropKind::Boat: {           // hull, mast, triangular sail
            DrawTriangle({at.x - .56f * unit, at.y - .22f * unit}, {at.x - .34f * unit, at.y},
                         {at.x + .56f * unit, at.y - .22f * unit}, wood);
            slab(0, -.26f, 1.06f, .16f, 0, wood);
            slab(.02f, -.62f, .05f, .60f, 0, woodDark);     // mast
            DrawTriangle({at.x + .04f * unit, at.y - .92f * unit},
                         {at.x + .04f * unit, at.y - .34f * unit},
                         {at.x + .46f * unit, at.y - .34f * unit}, light);
            break;
        }
        case PropKind::Well: {           // round wall, posts, little roof
            DrawEllipse(int(at.x), int(at.y - .12f * unit), .44f * unit, .17f * unit, dark);
            slab(0, -.22f, .76f, .30f, 0, body);
            DrawEllipse(int(at.x), int(at.y - .36f * unit), .38f * unit, .13f * unit, Color{62, 92, 124, 255});
            for (int i = -2; i <= 2; ++i)                   // stonework courses
                slab(float(i) * .16f, -.22f, .05f, .30f, 0, darken(body, .82f));
            slab(-.30f, -.62f, .06f, .52f, 0, woodDark);
            slab( .30f, -.62f, .06f, .52f, 0, woodDark);
            slab(0, -.80f, .52f, .06f, 0, woodDark);        // winding bar
            slab(.02f, -.70f, .025f, .20f, 0, woodDark);    // rope
            slab(.02f, -.56f, .17f, .15f, 0, wood);         // bucket
            slab(.02f, -.63f, .19f, .04f, 0, darken(wood, .7f));
            DrawTriangle({at.x, at.y - 1.06f * unit}, {at.x - .52f * unit, at.y - .76f * unit},
                         {at.x + .52f * unit, at.y - .76f * unit}, wood);
            DrawTriangle({at.x, at.y - 1.06f * unit}, {at.x, at.y - .76f * unit},
                         {at.x + .52f * unit, at.y - .76f * unit}, darken(wood, .86f));
            break;
        }
        default: {                       // Tent: ridge, open door flap, guy ropes
            DrawTriangle({at.x - .10f * unit, at.y - .96f * unit}, {at.x - .62f * unit, at.y},
                         {at.x + .40f * unit, at.y}, body);
            DrawTriangle({at.x - .10f * unit, at.y - .96f * unit}, {at.x + .40f * unit, at.y},
                         {at.x + .62f * unit, at.y}, darken(body, .82f));   // lit and shaded faces
            slab(-.10f, -.48f, .035f, .98f, 0, darken(body, .6f));          // ridge
            DrawTriangle({at.x - .10f * unit, at.y - .58f * unit},          // door opening
                         {at.x - .26f * unit, at.y}, {at.x + .06f * unit, at.y}, Color{44, 40, 44, 255});
            DrawTriangle({at.x - .10f * unit, at.y - .58f * unit},          // rolled flap
                         {at.x + .04f * unit, at.y}, {at.x + .20f * unit, at.y}, light);
            for (float side : {-1.f, 1.f}) {                                // guy ropes and pegs
                DrawLineEx({at.x + side * .56f * unit, at.y - .10f * unit},
                           {at.x + side * .84f * unit, at.y}, .022f * unit, woodDark);
                DrawCircleV({at.x + side * .84f * unit, at.y}, .035f * unit, woodDark);
            }
            slab(0, -.01f, 1.30f, .05f, 0, darken(body, .66f));
            break;
        }
    }
}
}
