#include "canvas.hpp"
#include <iostream>
#include <map>

static void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using namespace orbital;
    try {
        Rgb color{175,149,246};
        Canvas a=makeCanvas(3,color,4), b=makeCanvas(3,color,4);
        require(a.world==b.world&&a.beaconId.ix==b.beaconId.ix&&a.beaconId.iy==b.beaconId.iy,
                "the same track always yields the same world");
        require(a.beacon.x==b.beacon.x&&a.beacon.y==b.beacon.y,"the beacon lands in the same place");
        require(makeCanvas(4,color,4).world!=a.world,"different tracks are different worlds");

        require(a.beacon.beacon,"the beacon is flagged");
        require(a.beacon.motif==BeaconMotif,"the beacon is the beacon shape");
        require(a.beacon.palette==BeaconPalette,"the beacon wears the track colour");
        require(a.beacon.x>0&&a.beacon.x<1&&a.beacon.y>0&&a.beacon.y<1,"the beacon is inside the canvas");
        double span=nodeSpan(a.beaconDepth);
        require(a.beacon.x>=double(a.beaconId.ix)*span&&a.beacon.x<=double(a.beaconId.ix+1)*span,
                "the beacon sits inside its own node");

        // The whole point: exactly one motif anywhere pairs both features.
        require(countBeaconMatches(a,5)==1,"exactly one beacon down to depth 5");
        Canvas shallow=makeCanvas(1,color,2);
        require(countBeaconMatches(shallow,4)==1,"exactly one beacon at a shallower target depth too");

        // Decoys on both axes, or the beacon pops out instead of being searched for.
        std::vector<Element> elements;
        int spires=0,trackColoured=0,total=0;
        std::map<int,int> motifs;
        long long side=1;
        for (int d=0;d<=3;++d) {
            for (long long iy=0;iy<side;++iy)
                for (long long ix=0;ix<side;++ix) {
                    NodeId id{d,ix,iy};
                    nodeElements(a.world,id,a.isBeaconNode(id)?a.beaconSlot:-1,elements);
                    for (const Element& e:elements) {
                        ++total;++motifs[int(e.motif)];
                        if (e.motif==BeaconMotif&&e.palette!=BeaconPalette) ++spires;
                        if (e.palette==BeaconPalette&&e.motif!=BeaconMotif) ++trackColoured;
                    }
                }
            side*=Branch;
        }
        require(total>2000,"the canvas is dense by depth 3");
        require(spires>100,"plenty of spires in other colours");
        require(trackColoured>100,"plenty of track-coloured motifs of other shapes");
        require(motifs.size()==MotifCount,"every motif shape appears");

        // Elements must stay inside their node, or zooming reveals gaps and overlap.
        side=1;
        for (int d=0;d<=3;++d) {
            double nodeWidth=nodeSpan(d);
            for (long long iy=0;iy<side;++iy)
                for (long long ix=0;ix<side;++ix) {
                    NodeId id{d,ix,iy};
                    nodeElements(a.world,id,-1,elements);
                    for (const Element& e:elements) {
                        require(e.size>0&&e.size<nodeWidth,"an element fits inside its node");
                        require(e.x>=double(ix)*nodeWidth-1e-9&&e.x<=double(ix+1)*nodeWidth+1e-9,"element x stays in its node");
                        require(e.y>=double(iy)*nodeWidth-1e-9&&e.y<=double(iy+1)*nodeWidth+1e-9,"element y stays in its node");
                    }
                }
            side*=Branch;
        }

        // Deep zoom must keep producing detail rather than running dry.
        NodeId deep{14,0,0};
        for (int d=0;d<14;++d) {deep.ix=deep.ix*Branch+1;deep.iy=deep.iy*Branch+2;}
        deep.depth=14;
        nodeElements(a.world,deep,-1,elements);
        require(elements.size()>=7,"a depth-14 node still has motifs");
        require(nodeSpan(14)>0,"depth 14 is still a representable span");

        std::cout<<"PASS: determinism, unique beacon, conjunction decoys, containment, deep zoom\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        return 1;
    }
}
