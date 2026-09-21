#pragma once
// The things people are gathered around. Every skit has at least one, because
// a queue with nothing to queue for is just a line of people.
//
// These are drawn to be *recognisable*: `od recognise` renders each one alone
// and asks a local vision model to name it, so the silhouettes have to read as
// the thing they are and not as abstract shapes. That test is what keeps the
// art honest.
//
// No raylib here: scene generation owns props and stays testable.
#include "person.hpp"

namespace orbital {

enum class PropKind : unsigned char {
    Stall,      // market stall with a striped awning
    Cart,       // two-wheeled cart
    Fire,       // campfire with logs
    Fountain,   // round basin with a spout
    Bench,      // seat with a back
    Stage,      // low platform with a backboard
    Blanket,    // picnic blanket with a basket
    Haystack,   // conical stack
    Signpost,   // post with two arms
    Boat,       // hull with a mast
    Well,       // round wall with a roof
    Tent,       // triangular tent with a door
};
inline constexpr int PropKindCount = 12;

inline const char* propName(PropKind kind) {
    switch (kind) {
        case PropKind::Stall:    return "market stall";
        case PropKind::Cart:     return "cart";
        case PropKind::Fire:     return "campfire";
        case PropKind::Fountain: return "fountain";
        case PropKind::Bench:    return "bench";
        case PropKind::Stage:    return "stage";
        case PropKind::Blanket:  return "picnic blanket";
        case PropKind::Haystack: return "haystack";
        case PropKind::Signpost: return "signpost";
        case PropKind::Boat:     return "boat";
        case PropKind::Well:     return "well";
        default:                 return "tent";
    }
}

// Words a vision model may reasonably answer with for each prop. The test
// accepts any of them: "wagon" for a cart is a correct reading, and insisting
// on one word would be testing vocabulary rather than the drawing.
inline const char* propAccepts(PropKind kind) {
    switch (kind) {
        case PropKind::Stall:    return "stall|market|shop|booth|kiosk|stand|awning|canopy|store";
        case PropKind::Cart:     return "cart|wagon|carriage|trolley|wheelbarrow|barrow|wheel";
        case PropKind::Fire:     return "fire|campfire|bonfire|flame|firewood|logs|wood";
        case PropKind::Fountain: return "fountain|well|pool|basin|water|pond";
        case PropKind::Bench:    return "bench|seat|chair|sofa|couch|furniture";
        case PropKind::Stage:    return "stage|platform|podium|screen|sign|billboard|board";
        case PropKind::Blanket:  return "blanket|picnic|rug|mat|cloth|basket|towel";
        case PropKind::Haystack: return "haystack|hay|straw|stack|cone|tent|pile";
        case PropKind::Signpost: return "sign|signpost|post|pole|direction|arrow|marker";
        case PropKind::Boat:     return "boat|ship|sailboat|sail|vessel|canoe";
        case PropKind::Well:     return "well|fountain|basin|barrel|bucket|water";
        default:                 return "tent|teepee|tipi|camp|cone|pyramid";
    }
}

// How big a thing is next to a person, who is 11-16 px tall. A bench the size
// of a tent makes a world of furniture rather than a world of people.
inline float propScale(PropKind kind) {
    switch (kind) {
        case PropKind::Tent:     return 1.00f;
        case PropKind::Haystack: return 0.95f;
        case PropKind::Stall:    return 0.95f;
        case PropKind::Boat:     return 0.90f;
        case PropKind::Stage:    return 0.85f;
        case PropKind::Well:     return 0.78f;
        case PropKind::Signpost: return 0.75f;
        case PropKind::Cart:     return 0.72f;
        case PropKind::Fountain: return 0.72f;
        case PropKind::Fire:     return 0.58f;
        case PropKind::Bench:    return 0.55f;
        default:                 return 0.55f;   // Blanket
    }
}

struct Prop {
    PropKind kind;
    float x, y, size;
    float spin;
    unsigned char palette;   // index into the scene palette
};

}  // namespace orbital
