#include "campaign.hpp"
#include <iostream>

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
        Campaign real=loadCampaign("campaigns/orbital-drift.conf");
        require(real.note.empty(),"the shipped campaign parses cleanly");
        require(real.count()==7,"the shipped campaign has seven tracks");
        require(real.title=="Orbital Drift","title is read");
        require(real.tempo==72&&real.bars==16,"tempo and bars are read");
        require(real.rightToLeft,"the shipped campaign unlocks right to left");
        require(real.tracks[0].name=="Nebula Pad"&&real.tracks[6].name=="Orbit Hats","track order is kept");
        require(real.tracks[6].file=="07 Orbit Hats.wav","stem filenames are read");
        require(real.tracks[0].colour.r==0xaf,"colours are read as hex");
        require(real.allMask()==0x7f,"seven tracks make a seven-bit mask");
        require(real.nthUnlock(0)==6&&real.nthUnlock(6)==0,"right-to-left opens the last track first");
        require(real.seed!=0,"a campaign always has a world seed");
        require(loadCampaign("campaigns/three-signals.conf").seed!=real.seed,
                "two campaigns never share a world seed, or they share islands");

        // A campaign with a different shape, which is the whole point.
        auto three=write("od-three.conf",
            "title = Three\nunlock = left-to-right\ntempo = 90\nbars = 8\nstems = other\n"
            "track.1.name = A\ntrack.1.color = ff0000\n"
            "track.2.name = B\ntrack.2.color = 00ff00\n"
            "track.3.name = C\ntrack.3.color = 0000ff\n");
        Campaign c=loadCampaign(three);
        require(c.note.empty(),"a three-track campaign parses cleanly");
        require(c.count()==3,"track count follows the file, not a constant");
        require(c.allMask()==0x7,"the mask follows the count");
        require(!c.rightToLeft&&c.nthUnlock(0)==0,"left-to-right opens the first track first");
        require(c.tempo==90&&c.bars==8&&c.stems=="other","every field is per campaign");
        require(c.tracks[0].file=="A.wav","a missing filename falls back to the track name");

        require(!loadCampaign("/nonexistent.conf").note.empty(),"a missing file is reported");
        require(loadCampaign("/nonexistent.conf").count()==0,"a missing file yields no tracks");
        auto empty=write("od-empty.conf","title = Nothing\n");
        require(!loadCampaign(empty).note.empty(),"a campaign with no tracks is reported");

        // A gap in the numbering would silently drop a stem from the mix.
        auto gap=write("od-gap.conf",
            "title = Gap\ntrack.1.name = A\ntrack.1.color = ff0000\n"
            "track.3.name = C\ntrack.3.color = 0000ff\n");
        require(loadCampaign(gap).count()==1,"numbering stops at the first gap rather than skipping it");

        std::cout<<"PASS: shipped campaign, differing shapes, unlock order, fallbacks, gaps\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        return 1;
    }
}
