#include "progress.hpp"
#include <iostream>

static void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using namespace orbital;
    try {
        Progress p;
        require(p.unlocked==1,"a fresh run opens exactly one track");
        require(p.isUnlocked(TrackCount-1),"the rightmost track starts unlocked");
        require(!p.isUnlocked(0),"the leftmost track starts sealed");
        require(p.mask()==(1u<<(TrackCount-1)),"mask holds only the rightmost bit");
        require(p.frontier()==TrackCount-1,"the frontier is the rightmost track");
        require(p.nextLocked()==TrackCount-2,"the next unlock is one step left");
        require(!p.complete(),"one of seven is not complete");

        p.advance();
        require(p.unlocked==2&&p.isUnlocked(TrackCount-2),"advancing opens the next track left");
        require(p.frontier()==TrackCount-2,"the frontier moves left");
        require(p.mask()==((1u<<(TrackCount-1))|(1u<<(TrackCount-2))),"mask grows leftward");

        for (int i=0;i<20;++i) p.advance();
        require(p.unlocked==TrackCount,"advance never runs past the last track");
        require(p.complete()&&p.nextLocked()==-1,"a full run reports complete with nothing left");
        require(p.mask()==AllTracks,"a full run enables every track");

        auto path=fs::temp_directory_path()/"od-progress.json";
        fs::remove(path);
        require(loadProgress(path).unlocked==1,"a missing save starts fresh");
        Progress saved; saved.unlocked=4;
        saveProgress(path,saved);
        require(loadProgress(path).unlocked==4,"progress round-trips through disk");
        std::ofstream(path)<<"{ not json at all";
        require(loadProgress(path).unlocked==1,"a corrupt save starts over rather than failing");
        std::ofstream(path)<<"{\"unlocked\":99}";
        require(loadProgress(path).unlocked==TrackCount,"an out-of-range save is clamped");
        std::ofstream(path)<<"{\"unlocked\":-3}";
        require(loadProgress(path).unlocked==1,"a negative save is clamped");
        fs::remove(path);

        // The sigil follows each stem's own loudness, so a quiet track is not
        // permanently unreadable and a loud one is not permanently open.
        Audibility a;
        require(!a.sounding(0,0.f,.016f,.45f),"silence never shows the sigil");
        for (int i=0;i<200;++i) a.sounding(0,.02f,.016f,.45f);
        require(a.sounding(0,.02f,.016f,.45f),"a track at its own level shows the sigil");
        require(!a.sounding(0,.004f,.016f,.45f),"a track well under its own level hides it");
        Audibility quiet;
        for (int i=0;i<200;++i) quiet.sounding(3,.0006f,.016f,.45f);
        require(quiet.sounding(3,.0006f,.016f,.45f),"a quiet stem still reaches its own threshold");

        std::cout<<"PASS: unlock order, mask, clamping, persistence, corrupt saves, self-calibrating sigil\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        return 1;
    }
}
