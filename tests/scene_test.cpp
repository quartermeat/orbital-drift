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
        require(a.beacon==b.beacon,"the beacon lands in the same spot");
        require(!a.towns.empty()&&a.towns[0].x==b.towns[0].x,"town layout is stable");

        require(a.towns.size()>=3,"a world has several towns");
        require(a.woods.size()>=10,"a world has woodland");
        require(!a.fields.empty(),"a world has farmland");
        require(a.roads.size()==a.towns.size()-1,"every town is joined by a road");
        require(a.markers.size()>=12,"enough markers for the beacon to hide among");

        // The whole hunt: exactly one marker wears the track's own colour.
        require(countBeaconMatches(a)==1,"exactly one marker is the beacon");
        require(a.beacon>=0&&a.markers[size_t(a.beacon)].beacon,"the beacon is flagged");
        require(a.markers[size_t(a.beacon)].palette==BeaconPalette,"the beacon wears the track colour");

        // Nothing may sit in the sea, or the place stops reading as a place.
        for (const Town& town:a.towns) {
            require(elevationAt(a.seed,town.x,town.y)>ShoreLevel,"towns stand on land");
            for (const Building& building:town.buildings)
                require(elevationAt(a.seed,building.x,building.y)>ShoreLevel,"buildings stand on land");
        }
        for (const Marker& marker:a.markers)
            require(elevationAt(a.seed,marker.x,marker.y)>ShoreLevel,"markers stand on land");
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
            require(countBeaconMatches(s)==1,"every world has exactly one beacon");
            require(!s.towns.empty(),"every world has a town");
            ++places[(long long)(s.towns[0].x)*100000+(long long)(s.towns[0].y)];
        }
        require(places.size()==size_t(TrackCount),"every world has its own layout");

        std::cout<<"PASS: determinism, towns/roads/woods/fields, one beacon, all on land, sea and land, distinct worlds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        return 1;
    }
}
