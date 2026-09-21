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

        auto good=write("od-good.conf",
            "# comment\nstar.count = 12\norbit.base=0.5\n\nglow.scale = 2.5\n");
        LayerConfig c=loadLayerConfig(good);
        require(c.note.empty(),"clean file parses without a warning");
        require(c.starCount==12,"numeric key parses");
        require(std::abs(c.orbitBase-.5f)<1e-6,"float key parses without spaces");
        require(std::abs(c.glowScale-2.5f)<1e-6,"a later key parses too");

        auto bad=write("od-bad.conf","star.count = banana\nthis line has no equals\n");
        LayerConfig b=loadLayerConfig(bad);
        require(!b.note.empty(),"bad lines are reported");
        require(b.starCount==260,"unparseable number keeps the default");

        auto clamped=write("od-clamp.conf","star.count = 99999\n");
        require(loadLayerConfig(clamped).starCount==4000,"star count is clamped");

        Watched watch{good};
        require(!watch.changed(),"a just-written file is left alone until the write settles");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        require(watch.changed(),"a settled change is picked up");
        require(!watch.changed(),"an unchanged file does not re-trigger");

        std::cout<<"PASS: defaults, parsing, bad lines, clamping, settle-debounce\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        return 1;
    }
}
