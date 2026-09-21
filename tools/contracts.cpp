// Emits the canonical example for every interface between layers, straight
// from the live structs. Committed under testdata/interfaces, regenerated and
// diffed by `od interfaces`, so an interface cannot change without the
// fixture changing and the documentation being forced to follow.
//
//   build/contracts <dir>     write every fixture into <dir>
//
// sizeof is included on purpose: C++ has no reflection, so a field added
// without updating this emitter would otherwise slip through. A changed
// sizeof makes the diff fail and someone has to look.
#include "campaign.hpp"
#include "scene.hpp"
#include "progress.hpp"
#include "person.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace orbital;

namespace {

struct Json {
    std::ostringstream out;
    int depth = 0;
    bool fresh = true;
    void indent() { out << '\n' << std::string(size_t(depth) * 2, ' '); }
    void comma() { if (!fresh) out << ','; fresh = false; indent(); }
    void open(char brace) { out << brace; ++depth; fresh = true; }
    void close(char brace) { --depth; if (!fresh) indent(); out << brace; fresh = false; }
    void key(const std::string& name) { comma(); out << '"' << name << "\": "; }
    void value(const std::string& text) { out << '"' << text << '"'; }
    void field(const std::string& name, const std::string& text) { key(name); value(text); }
    void field(const std::string& name, long long number) { key(name); out << number; }
    void field(const std::string& name, double number) {
        key(name);
        out << std::fixed << std::setprecision(3) << number;
    }
    void field(const std::string& name, bool flag) { key(name); out << (flag ? "true" : "false"); }
};

void shape(Json& json, std::initializer_list<std::pair<const char*, const char*>> fields) {
    json.key("fields");
    json.open('{');
    for (auto& [name, type] : fields) json.field(name, std::string(type));
    json.close('}');
}

void header(Json& json, const char* name, const char* producer, const char* consumer,
            const char* doc, size_t bytes) {
    json.field("interface", std::string(name));
    json.field("produced_by", std::string(producer));
    json.field("consumed_by", std::string(consumer));
    json.field("documented_in", std::string(doc));
    json.field("sizeof", (long long)bytes);
}

void write(const std::string& dir, const std::string& name, Json& json) {
    std::ofstream file(dir + "/" + name + ".json");
    file << json.out.str() << '\n';
    if (!file) { std::cerr << "cannot write " << name << '\n'; std::exit(1); }
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "testdata/interfaces";

    {   // campaign.conf -> Campaign
        Campaign campaign = loadCampaign("campaigns/orbital-drift.conf");
        Json json; json.open('{');
        header(json, "campaign", "campaigns/*.conf", "src/campaign.hpp loadCampaign()",
               "docs/interfaces/campaign.md", sizeof(Campaign));
        shape(json, {{"title","string"},{"subtitle","string"},{"key","string"},{"stems","path"},
                     {"tempo","int"},{"bars","int"},{"unlock","right-to-left|left-to-right"},
                     {"seed","uint64 (from title unless given)"},
                     {"track.N.name","string"},{"track.N.file","filename"},
                     {"track.N.role","string"},{"track.N.color","rrggbb"}});
        json.key("example"); json.open('{');
        json.field("title", campaign.title);
        json.field("tempo", (long long)campaign.tempo);
        json.field("bars", (long long)campaign.bars);
        json.field("right_to_left", campaign.rightToLeft);
        json.field("tracks", (long long)campaign.count());
        json.field("first_track", campaign.tracks.empty() ? std::string("-") : campaign.tracks[0].name);
        json.field("first_file", campaign.tracks.empty() ? std::string("-") : campaign.tracks[0].file);
        json.field("all_mask", (long long)campaign.allMask());
        json.close('}');
        json.close('}');
        write(dir, "campaign", json);
    }
    {   // layers.conf -> LayerConfig
        LayerConfig layers = loadLayerConfig("assets/layers.conf");
        Json json; json.open('{');
        header(json, "layers", "assets/layers.conf", "src/hotreload.hpp loadLayerConfig()",
               "docs/interfaces/layers.md", sizeof(LayerConfig));
        shape(json, {{"star.count","int 0..4000"},{"orbit.base","float"},{"orbit.step","float"},
                     {"glow.scale","float"},{"energy.gain","float"}});
        json.key("example"); json.open('{');
        json.field("starCount", (long long)layers.starCount);
        json.field("orbitBase", (double)layers.orbitBase);
        json.field("orbitStep", (double)layers.orbitStep);
        json.field("glowScale", (double)layers.glowScale);
        json.field("energyGain", (double)layers.energyGain);
        json.field("hot_reloaded", true);
        json.close('}');
        json.close('}');
        write(dir, "layers", json);
    }
    {   // progress-<campaign>.json
        Progress progress; progress.count = 7; progress.unlocked = 3;
        Json json; json.open('{');
        header(json, "progress", "src/progress.hpp saveProgress()", "src/progress.hpp loadProgress()",
               "docs/interfaces/progress.md", sizeof(Progress));
        shape(json, {{"unlocked","int 1..count"}});
        json.key("example"); json.open('{');
        json.field("unlocked", (long long)progress.unlocked);
        json.field("file", std::string("artifacts/progress-<campaign>.json"));
        json.field("count_and_direction", std::string("supplied by the campaign, never stored"));
        json.field("frontier_at_3_of_7_right_to_left", (long long)progress.frontier());
        json.field("next_locked", (long long)progress.nextLocked());
        json.close('}');
        json.close('}');
        write(dir, "progress", json);
    }
    {   // Figure -> drawFigure
        Rng rng(4242);
        Figure figure = rollFigure(rng);
        Json json; json.open('{');
        header(json, "figure", "src/person.hpp rollFigure()", "src/figure.hpp drawFigure()",
               "docs/interfaces/figure.md", sizeof(Figure));
        shape(json, {{"skin","index 0..4"},{"hair","index 0..5"},{"shirt","index 0..11"},
                     {"stripe","index 0..11"},{"trousers","index 0..11"},{"hat","index 0..11"},
                     {"prop","index 0..11"},{"pattern","Solid|Stripe|Spot"},
                     {"pose","index 0..23"},{"wearsHat","bool"},{"carries","bool"}});
        json.key("example"); json.open('{');
        json.field("skin", (long long)figure.skin);
        json.field("shirt", (long long)figure.shirt);
        json.field("pattern", (long long)int(figure.pattern));
        json.field("pose", (long long)figure.pose);
        json.field("pose_name", std::string(Poses[figure.pose % PoseCount].name));
        json.field("wearsHat", figure.wearsHat);
        json.field("pose_count", (long long)PoseCount);
        json.field("cloth_count", (long long)ClothCount);
        json.close('}');
        json.close('}');
        write(dir, "figure", json);
    }
    {   // generateScene() -> the world view
        Scene scene = generateScene(0, Rgb{175, 149, 246}, 7, 12345);
        Json json; json.open('{');
        header(json, "scene", "src/scene.hpp generateScene()", "src/main.cpp world view",
               "docs/interfaces/scene.md", sizeof(Scene));
        shape(json, {{"towns","Town[] (x,y,radius,buildings)"},{"roads","Road[] (points)"},
                     {"woods","Patch[] (blobs)"},{"fields","Field[]"},{"markers","Marker[]"},
                     {"people","PersonSpot[] (x,y,height,layer,skit,figure)"},
                     {"skits","Skit[] (kind,x,y,wants,hides,primary,members,bounds)"},
                     {"target","index into people"},{"palette","Rgb[6]"},
                     {"sea/shallow/sand/grass/highland/forest","Rgb"}});
        json.key("example"); json.open('{');
        json.field("seed_inputs", std::string("track index + campaign seed"));
        json.field("scene_width", (long long)SceneWidth);
        json.field("scene_height", (long long)SceneHeight);
        json.field("towns", (long long)scene.towns.size());
        json.field("roads", (long long)scene.roads.size());
        json.field("woods", (long long)scene.woods.size());
        json.field("fields", (long long)scene.fields.size());
        json.field("markers", (long long)scene.markers.size());
        json.field("people", (long long)scene.people.size());
        json.field("skits", (long long)scene.skits.size());
        json.field("sizeof_PersonSpot", (long long)sizeof(PersonSpot));
        json.field("sizeof_Skit", (long long)sizeof(Skit));
        json.field("target_index", (long long)scene.target);
        json.field("target_layer", (long long)scene.people[size_t(scene.target)].layer);
        json.field("target_outfit_is_unique", countOutfitMatches(scene) == 1);
        json.close('}');
        json.close('}');
        write(dir, "scene", json);
    }
    {   // skit configuration, the part the mix drives
        Scene scene = generateScene(0, Rgb{175, 149, 246}, 7, 12345);
        int single = 0, paired = 0, needsQuiet = 0;
        for (const Skit& skit : scene.skits) {
            int bits = 0;
            for (int i = 0; i < 7; ++i) bits += (skit.wants >> i) & 1u;
            if (bits == 1) ++single; else ++paired;
            if (skit.hides) ++needsQuiet;
        }
        Json json; json.open('{');
        header(json, "skit", "src/scene.hpp generateScene()", "src/main.cpp visibility + dev overlay",
               "docs/interfaces/skit.md", sizeof(Skit));
        shape(json, {{"kind","Queue|Ring|Chase|Pair|Audience|Picnic|Work|Stroll"},
                     {"wants","track bitmask, all must sound"},
                     {"hides","track bitmask, none may sound"},
                     {"primary","track index, used for grouping"},
                     {"members","int"},{"minX/minY/maxX/maxY","bounds of its people"}});
        json.key("example"); json.open('{');
        json.field("kinds", (long long)SkitKindCount);
        json.field("total", (long long)scene.skits.size());
        json.field("want_one_track", (long long)single);
        json.field("want_two_tracks", (long long)paired);
        json.field("need_a_track_muted", (long long)needsQuiet);
        json.field("showing_rule", std::string("(playing & wants) == wants && (playing & hides) == 0"));
        json.close('}');
        json.close('}');
        write(dir, "skit", json);
    }
    std::cout << "wrote 6 interface fixtures to " << dir << '\n';
    return 0;
}
