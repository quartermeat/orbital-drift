#include "progress.hpp"
#include <iostream>

static void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using namespace orbital;
    constexpr int Tracks = 7;
    try {
        Progress p;
        p.count = Tracks;
        require(p.unlocked==1,"a fresh run opens exactly one track");
        require(p.isUnlocked(Tracks-1),"the rightmost track starts unlocked");
        require(!p.isUnlocked(0),"the leftmost track starts sealed");
        require(p.mask()==(1u<<(Tracks-1)),"mask holds only the rightmost bit");
        require(p.frontier()==Tracks-1,"the frontier is the rightmost track");
        require(p.nextLocked()==Tracks-2,"the next unlock is one step left");
        require(!p.complete(),"one of seven is not complete");

        p.advance();
        require(p.unlocked==2&&p.isUnlocked(Tracks-2),"advancing opens the next track left");
        require(p.frontier()==Tracks-2,"the frontier moves left");
        require(p.mask()==((1u<<(Tracks-1))|(1u<<(Tracks-2))),"mask grows leftward");

        for (int i=0;i<20;++i) p.advance();
        require(p.unlocked==Tracks,"advance never runs past the last track");
        require(p.complete()&&p.nextLocked()==-1,"a full run reports complete with nothing left");
        require(p.mask()==(1u<<Tracks)-1,"a full run enables every track");

        auto path=fs::temp_directory_path()/"od-progress.json";
        fs::remove(path);
        require(loadProgress(path,Tracks,true).unlocked==1,"a missing save starts fresh");
        Progress saved; saved.count=Tracks; saved.unlocked=4;
        saveProgress(path,saved);
        require(loadProgress(path,Tracks,true).unlocked==4,"progress round-trips through disk");
        std::ofstream(path)<<"{ not json at all";
        require(loadProgress(path,Tracks,true).unlocked==1,"a corrupt save starts over rather than failing");
        std::ofstream(path)<<"{\"unlocked\":99}";
        require(loadProgress(path,Tracks,true).unlocked==Tracks,"an out-of-range save is clamped");
        std::ofstream(path)<<"{\"unlocked\":-3}";
        require(loadProgress(path,Tracks,true).unlocked==1,"a negative save is clamped");
        fs::remove(path);

        // Left-to-right campaigns open the other end first.
        Progress left; left.count=Tracks; left.rightToLeft=false;
        require(left.isUnlocked(0)&&!left.isUnlocked(Tracks-1),"left-to-right opens the first track");
        require(left.frontier()==0,"its frontier is the first track");
        require(left.nextLocked()==1,"its next unlock is the second track");
        left.advance();
        require(left.isUnlocked(1)&&left.frontier()==1,"advancing moves rightward");
        for (int i=0;i<20;++i) left.advance();
        require(left.complete()&&left.mask()==(1u<<Tracks)-1,"a full left-to-right run enables everything");

        std::cout<<"PASS: unlock order both directions, mask, clamping, persistence, corrupt saves\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        return 1;
    }
}
