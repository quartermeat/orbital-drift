#include "hotreload.hpp"
#include <iostream>
#include <thread>

static void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static orbital::fs::path write(const std::string& name,const std::string& body) {
    auto path=orbital::fs::temp_directory_path()/name;
    std::ofstream(path)<<body;
    return path;
}

int main() {
    using namespace orbital;
    try {
        LayerConfig defaults=loadLayerConfig("/nonexistent/layers.conf");
        require(!defaults.note.empty(),"missing file is reported");
        require(defaults.starCount==260,"missing file falls back to defaults");
        require(defaults.roles[0]=="ATMOSPHERE","default roles survive a missing file");

        auto good=write("od-good.conf",
            "# comment\nstar.count = 12\norbit.base=0.5\n\n"
            "track.1.role = DRIFT\ntrack.1.color = #01ff80\ntrack.7.color = 102030\n");
        LayerConfig c=loadLayerConfig(good);
        require(c.note.empty(),"clean file parses without a warning");
        require(c.starCount==12,"numeric key parses");
        require(std::abs(c.orbitBase-.5f)<1e-6,"float key parses without spaces");
        require(c.roles[0]=="DRIFT","role overrides default");
        require(c.roles[1]=="MELODY","unset role keeps default");
        require(c.colors[0].r==1&&c.colors[0].g==255&&c.colors[0].b==128,"hex colour with # parses");
        require(c.colors[6].r==16&&c.colors[6].g==32&&c.colors[6].b==48,"hex colour without # parses");

        auto bad=write("od-bad.conf","star.count = banana\nthis line has no equals\ntrack.2.color = xyz\n");
        LayerConfig b=loadLayerConfig(bad);
        require(!b.note.empty(),"bad lines are reported");
        require(b.starCount==260,"unparseable number keeps the default");
        require(b.colors[1].r==116,"unparseable colour keeps the default");

        auto clamped=write("od-clamp.conf","star.count = 99999\n");
        require(loadLayerConfig(clamped).starCount==4000,"star count is clamped");

        Watched watch{good};
        require(!watch.changed(),"a just-written file is left alone until the write settles");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        require(watch.changed(),"a settled change is picked up");
        require(!watch.changed(),"an unchanged file does not re-trigger");

        std::cout<<"PASS: defaults, parsing, colours, bad lines, clamping, settle-debounce\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        return 1;
    }
}
