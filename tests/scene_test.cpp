#include "scene.hpp"
#include <iostream>
#include <map>

static void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using namespace orbital;
    try {
        Rgb color{175,149,246};
        Scene a=generateScene(3,color),b=generateScene(3,color);
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
        for (int track=0;track<TrackCount;++track) {
            Scene s=generateScene(track,color);
            require(countOutfitMatches(s)==1,"every world has exactly one target");
            require(!s.towns.empty(),"every world has a town");
            ++places[(long long)(s.towns[0].x)*100000+(long long)(s.towns[0].y)];
        }
        require(places.size()==size_t(TrackCount),"every world has its own layout");

        std::cout<<"PASS: determinism, towns/roads/woods/fields, unique target outfit, all on land, distinct worlds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        return 1;
    }
}
