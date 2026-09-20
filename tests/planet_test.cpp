#include "planet.hpp"
#include <iostream>
#include <map>

static void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using namespace orbital;
    try {
        Rgb color{175,149,246};
        Planet a=generatePlanet(3,color,900), b=generatePlanet(3,color,900);
        require(a.props.size()==900,"the requested number of props is generated");
        bool identical=true;
        for (size_t i=0;i<a.props.size();++i)
            identical&=a.props[i].kind==b.props[i].kind&&a.props[i].palette==b.props[i].palette
                     &&a.props[i].scale==b.props[i].scale&&a.props[i].normal.x==b.props[i].normal.x;
        require(identical,"the same track always generates the same planet");
        require(generatePlanet(4,color,900).beacon!=a.beacon||
                generatePlanet(4,color,900).props[0].kind!=a.props[0].kind,"different tracks differ");

        require(a.beacon>=0&&a.beacon<int(a.props.size()),"the beacon is a real prop");
        require(a.props[size_t(a.beacon)].beacon,"the beacon is flagged");
        require(countBeaconMatches(a)==1,"exactly one prop wears both beacon features");
        require(a.props[size_t(a.beacon)].kind==BeaconKind,"the beacon is the beacon shape");
        require(a.props[size_t(a.beacon)].palette==BeaconPalette,"the beacon wears the track colour");

        // Without decoys on both axes there is no search, only a pop-out.
        int shapeDecoys=0,colorDecoys=0;
        for (const Prop& p:a.props) {
            if (p.kind==BeaconKind&&p.palette!=BeaconPalette) ++shapeDecoys;
            if (p.palette==BeaconPalette&&p.kind!=BeaconKind) ++colorDecoys;
        }
        require(shapeDecoys>=12,"enough spires in other colours to defeat a shape-only scan");
        require(colorDecoys>=12,"enough track-coloured props of other shapes to defeat a colour-only scan");

        // Every prop must sit on the surface, and the scatter must not clump.
        std::map<int,int> bands;
        for (const Prop& p:a.props) {
            float length=std::sqrt(p.normal.x*p.normal.x+p.normal.y*p.normal.y+p.normal.z*p.normal.z);
            require(std::abs(length-1.f)<.02f,"props sit on the unit sphere");
            require(p.scale>.4f&&p.scale<1.5f,"prop scale stays sane");
            ++bands[std::min(7,int((p.normal.y+1.f)*4.f))];   // 8 latitude bands, poles included
        }
        require(bands.size()>=8,"props are spread across the whole globe, not a band");
        for (auto& [band,total]:bands) { (void)band; require(total>30,"no latitude band is left empty"); }

        std::map<int,int> kinds;
        for (const Prop& p:a.props) ++kinds[int(p.kind)];
        require(kinds.size()==PropKindCount,"every prop shape appears");

        Planet small=generatePlanet(0,color,140);
        require(countBeaconMatches(small)==1,"a small planet still has exactly one beacon");

        std::cout<<"PASS: determinism, single beacon, conjunction decoys, even scatter, all shapes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        return 1;
    }
}
