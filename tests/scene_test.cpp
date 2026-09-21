#include "scene.hpp"
#include <iostream>
#include <map>

static void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using namespace orbital;
    constexpr int TrackLayers = 7;
    try {
        Rgb color{175,149,246};
        Scene a=generateScene(3,color,7),b=generateScene(3,color,7);
        require(a.towns.size()==b.towns.size()&&a.markers.size()==b.markers.size(),"a track always generates the same place");
        require(a.target==b.target,"the target is the same person every run");
        require(!a.towns.empty()&&a.towns[0].x==b.towns[0].x,"town layout is stable");

        require(a.towns.size()>=3,"a world has several towns");
        require(a.woods.size()>=10,"a world has woodland");
        require(!a.fields.empty(),"a world has farmland");
        require(a.roads.size()==a.towns.size()-1,"every town is joined by a road");
        require(a.markers.size()>=12,"the land has waymarks on it");

        // The whole hunt: one person, and nobody else dressed like them.
        require(a.people.size()>300,"a world is properly populated");
        require(a.target>=0&&a.target<int(a.people.size()),"the target is a real person");
        require(countOutfitMatches(a)==1,"nobody else wears the target's outfit");
        require(a.people[size_t(a.target)].height>8,"the target is a normal-sized person");

        // The rule: nobody is in a scene without being part of something.
        require(!a.skits.empty(),"a world has skits");
        std::vector<int> members(a.skits.size(),0);
        for (const PersonSpot& spot:a.people) {
            require(spot.skit>=0&&spot.skit<int(a.skits.size()),"every person belongs to a real skit");
            require(a.skits[size_t(spot.skit)].primary==spot.layer,"a person's skit is in their own layer");
            ++members[size_t(spot.skit)];
        }
        for (size_t i=0;i<a.skits.size();++i) {
            require(members[i]>0,"no skit is empty");
            require(members[i]==a.skits[i].members,"a skit's member count matches its people");
            require(a.skits[i].primary<TrackLayers,"a skit names a real track layer");
            require(a.skits[i].wants!=0,"a skit asks for at least one track");
            require((a.skits[i].wants&a.skits[i].hides)==0,"a skit never wants and hides the same track");
            require((a.skits[i].wants>>a.skits[i].primary)&1u,"a skit always wants its own primary track");
        }
        // Configurations: most skits want one track, some want two, a few need
        // something silent. Without the spread there are no specific configs.
        int single=0,paired=0,needsQuiet=0;
        for (const Skit& skit:a.skits) {
            int bits=0;
            for (int i=0;i<TrackLayers;++i) bits+=(skit.wants>>i)&1u;
            if (bits==1) ++single; else ++paired;
            if (skit.hides) ++needsQuiet;
        }
        require(single>a.skits.size()/2,"most skits still want a single track");
        require(paired>20,"some skits want a pair of tracks");
        require(needsQuiet>5,"some skits only appear with a track muted");

        // Everything shows when everything plays, except what needs silence.
        uint32_t all=(1u<<TrackLayers)-1;
        int showingAll=0;
        for (const Skit& skit:a.skits) if (skit.showing(all)) ++showingAll;
        require(showingAll==int(a.skits.size())-needsQuiet,"a full mix shows everything that does not need silence");

        // The target must be reachable by waking its own world's track alone.
        {
            const Skit& hosting=a.skits[size_t(a.people[size_t(a.target)].skit)];
            require(hosting.hides==0,"the target's skit never needs a track muted");
            uint32_t onlyOwn=1u<<a.people[size_t(a.target)].layer;
            require(hosting.showing(onlyOwn),"the target shows with only its own track playing");
        }

        // A skit belongs to exactly one track layer, so toggling a track adds
        // or removes whole vignettes rather than half a queue.
        std::map<int,int> layerOf;
        for (const PersonSpot& spot:a.people) {
            auto seen=layerOf.find(spot.skit);
            if (seen==layerOf.end()) layerOf[spot.skit]=spot.layer;
            else require(seen->second==spot.layer,"every member of a skit is in the same layer");
        }
        // Every skit has an object: a queue with nothing to queue for is just
        // a line of people.
        require(a.props.size()==a.skits.size(),"one object per skit");
        std::map<int,int> propKinds;
        for (const Skit& skit:a.skits) {
            require(skit.prop>=0&&skit.prop<int(a.props.size()),"a skit points at a real object");
            const Prop& prop=a.props[size_t(skit.prop)];
            require(prop.size>12&&prop.size<45,"an object is a sensible size next to a person");
            require(elevationAt(a.seed,prop.x,prop.y)>SeaLevel,"no object floats in deep water");
            float dx=prop.x-skit.x,dy=prop.y-skit.y;
            require(dx*dx+dy*dy<200.f*200.f,"an object stands with its skit");
            ++propKinds[int(prop.kind)];
        }
        require(propKinds.size()>=8,"a world uses most of the object kinds");

        std::map<int,int> kinds;
        for (const Skit& skit:a.skits) ++kinds[int(skit.kind)];
        require(kinds.size()==size_t(SkitKindCount),"every kind of skit happens somewhere");
        // A skit is a group in one place, not people scattered under one label.
        for (const PersonSpot& spot:a.people) {
            const Skit& skit=a.skits[size_t(spot.skit)];
            float dx=spot.x-skit.x,dy=spot.y-skit.y;
            require(dx*dx+dy*dy<260.f*260.f,"a skit's people stand together");
        }

        // The target must be visible: people are drawn in y order, so anyone
        // just below them hides them and the hunt cannot be won.
        {
            const PersonSpot& mark=a.people[size_t(a.target)];
            for (const PersonSpot& other:a.people) {
                if (&other==&mark) continue;
                float dx=other.x-mark.x,dy=other.y-mark.y,near=mark.height*.75f;
                require(!(dy>-mark.height*.15f&&dy<near&&std::abs(dx)<near),
                        "nobody stands in front of the target");
            }
        }

        // Nothing may sit in the sea, or the place stops reading as a place.
        for (const Town& town:a.towns) {
            require(elevationAt(a.seed,town.x,town.y)>ShoreLevel,"towns stand on land");
            for (const Building& building:town.buildings)
                require(elevationAt(a.seed,building.x,building.y)>ShoreLevel,"buildings stand on land");
        }
        for (const Marker& marker:a.markers)
            require(elevationAt(a.seed,marker.x,marker.y)>ShoreLevel,"waymarks stand on land");
        for (const PersonSpot& spot:a.people)
            require(elevationAt(a.seed,spot.x,spot.y)>SeaLevel,"nobody stands in deep water");
        for (const Patch& patch:a.woods)
            for (const Blob& blob:patch.blobs)
                require(elevationAt(a.seed,blob.x,blob.y)>ShoreLevel,"trees stand on land");

        // Everything must sit inside the image.
        for (const Marker& marker:a.markers)
            require(marker.x>0&&marker.x<SceneWidth&&marker.y>0&&marker.y<SceneHeight,"markers are inside the frame");
        for (const Road& road:a.roads)
            require(road.points.size()>=2,"a road has a path");

        // There must be both sea and land, or it is a puddle or a slab.
        int water=0,land=0,high=0;
        for (int y=0;y<SceneHeight;y+=24)
            for (int x=0;x<SceneWidth;x+=24) {
                float height=elevationAt(a.seed,float(x),float(y));
                if (height<SeaLevel) ++water; else if (height>HighLevel) ++high; else ++land;
            }
        require(water>200,"there is a sea");
        require(land>600,"there is plenty of land");
        require(high>0,"there is high ground");

        // Seven distinct worlds, not one world seven times. Counts all hit the
        // same caps, so compare the actual geography.
        std::map<long long,int> places;
        for (int track=0;track<7;++track) {
            Scene s=generateScene(track,color,7);
            require(countOutfitMatches(s)==1,"every world has exactly one target");
            require(!s.towns.empty(),"every world has a town");
            ++places[(long long)(s.towns[0].x)*100000+(long long)(s.towns[0].y)];
        }
        require(places.size()==size_t(7),"every world has its own layout");

        // Two campaigns must not generate the same island in different colours.
        Scene one=generateScene(0,color,7,1111), two=generateScene(0,color,7,2222);
        require(one.towns[0].x!=two.towns[0].x||one.towns[0].y!=two.towns[0].y,
                "the campaign seed changes the land, not just the palette");

        std::cout<<"PASS: determinism, towns/roads/woods/fields, unique target outfit, all on land, distinct worlds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        return 1;
    }
}
