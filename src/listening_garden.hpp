#pragma once
#include "listening_audio.hpp"
#include "scene.hpp"

namespace orbital {
struct ListeningCreature { float x=0,y=0,energy=0; };
struct ListeningGarden {
    std::array<ListeningCreature,3> creatures{};
    int held=-1,selected=-1,moves=0;
    float together=0;
    bool discovered=false;
    uint64_t seed;
    explicit ListeningGarden(uint64_t s):seed(s){reset();}
    void reset() {
        const float desired[3][2]={{960,710},{1370,780},{1670,640}};
        for(int i=0;i<3;++i) {
            float best=1e20f;
            for(float y=260;y<1200;y+=32)for(float x=380;x<2180;x+=32) {
                if(!isLand(seed,x,y))continue;
                float d=std::hypot(x-desired[i][0],y-desired[i][1]);
                if(d<best){best=d;creatures[i]={x,y,0};}
            }
        }
        held=selected=-1;moves=0;together=0;discovered=false;
    }
    bool move(int index,float x,float y) {
        if(index<0||index>=3||!std::isfinite(x)||!std::isfinite(y)
           ||x<0||x>SceneWidth||y<0||y>SceneHeight||!isLand(seed,x,y))return false;
        // Leave enough space to pick each creature up again after gathering them.
        for(int pass=0;pass<4;++pass)for(int i=0;i<3;++i)if(i!=index) {
            float dx=x-creatures[i].x,dy=y-creatures[i].y,d=std::hypot(dx,dy);
            if(d<155) {
                if(d<.01f){dx=(index%2==0?-1.f:1.f);dy=.5f;d=std::hypot(dx,dy);}
                x=creatures[i].x+dx/d*155;y=creatures[i].y+dy/d*155;
            }
        }
        if(x<0||x>SceneWidth||y<0||y>SceneHeight||!isLand(seed,x,y))return false;
        creatures[size_t(index)].x=x;creatures[size_t(index)].y=y;return true;
    }
    void update(const ListenFeatures& f,float dt) {
        const float energies[3]={std::max(f.bass*.65f,f.pulse),f.body,f.air};
        for(int i=0;i<3;++i)creatures[i].energy=energies[i];
        bool close=true;
        for(int i=0;i<3;++i)for(int j=i+1;j<3;++j)
            if(std::hypot(creatures[i].x-creatures[j].x,creatures[i].y-creatures[j].y)>390)close=false;
        if(close&&f.rms>.0001f)together=std::min(1.f,together+dt*.4f);
        else together=std::max(0.f,together-dt*.65f);
        if(together>=1)discovered=true;
    }
};
} // namespace orbital
