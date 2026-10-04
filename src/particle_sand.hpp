#pragma once
// Every visible grain owns a position, velocity, and local vibration state.
// The grid below is only a lookup table for the plate's force; it does not hold
// or exchange sand. Grains are created by the user and never merge or split.
#include "chladni.hpp"

namespace orbital {
struct SandGrain {
    float x=0,y=0,vx=0,vy=0,activity=0;
    bool operator==(const SandGrain&) const = default;
};

class ParticleSand {
    struct Force {float x=0,y=0,energy=0;};
    std::vector<Force> force;
    std::vector<unsigned> occupancy;
    PlateMode lastMode{},lastCompanion{};
    float lastWeight=-1;
    static uint32_t scramble(uint32_t x) {
        x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;
    }
    static float unit(uint32_t x) {return float(scramble(x)&0xffffffu)/float(0x1000000u);}
    void rebuildForce(const PlateDriver& plate) {
        if(plate.live==lastMode&&plate.harmonic==lastCompanion
           &&std::abs(plate.harmonicWeight-lastWeight)<.02f)return;
        lastMode=plate.live;lastCompanion=plate.harmonic;lastWeight=plate.harmonicWeight;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)
            force[size_t(y)*width+x].energy=plate.energy((x+.5f)/width,(y+.5f)/height);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            int left=std::max(0,x-1),right=std::min(width-1,x+1);
            int up=std::max(0,y-1),down=std::min(height-1,y+1);
            auto& at=force[size_t(y)*width+x];
            at.x=(force[size_t(y)*width+right].energy-force[size_t(y)*width+left].energy)/float(right-left);
            at.y=(force[size_t(down)*width+x].energy-force[size_t(up)*width+x].energy)/float(down-up);
        }
    }
public:
    int width=0,height=0;
    std::vector<SandGrain> grains;
    uint64_t sweeps=0,grainUpdates=0;
    size_t moving=0;
    float spread=0;
    void reset(int w,int h) {
        width=w;height=h;grains.clear();grains.reserve(size_t(w)*h);
        force.assign(size_t(w)*h,{});occupancy.assign(size_t(w)*h,0);
        lastWeight=-1;sweeps=grainUpdates=0;moving=0;spread=0;
    }
    bool add(float x,float y) {
        if(width<1||height<1||grains.size()>=size_t(width)*height)return false;
        grains.push_back({std::clamp(x,.5f,width-.5f),std::clamp(y,.5f,height-.5f),0,0,0});
        return true;
    }
    void update(const PlateDriver& plate,float dt) {
        if(plate.paused||plate.agitation<=0||dt<=0)return;
        rebuildForce(plate);
        float step=std::clamp(dt*60.f,0.f,2.f);
        moving=0;
        std::fill(occupancy.begin(),occupancy.end(),0u);
        for(const auto& grain:grains) {
            int x=std::clamp(int(grain.x),0,width-1),y=std::clamp(int(grain.y),0,height-1);
            ++occupancy[size_t(y)*width+x];
        }
        for(size_t i=0;i<grains.size();++i) {
            auto& grain=grains[i];
            int x=std::clamp(int(grain.x),0,width-1),y=std::clamp(int(grain.y),0,height-1);
            const Force& here=force[size_t(y)*width+x];
            uint32_t seed=uint32_t(i)^uint32_t(sweeps*0x9e3779b9u);
            float noiseX=unit(seed*2+1)*2-1,noiseY=unit(seed*2+2)*2-1;
            float shake=(.025f+here.energy*.55f)*plate.agitation;
            unsigned packed=occupancy[size_t(y)*width+x];
            float pressure=std::min(.8f,std::max(0.f,float(packed)-2.f)*.045f)*plate.agitation;
            float px=float(occupancy[size_t(y)*width+std::max(0,x-1)])
                    -float(occupancy[size_t(y)*width+std::min(width-1,x+1)]);
            float py=float(occupancy[size_t(std::max(0,y-1))*width+x])
                    -float(occupancy[size_t(std::min(height-1,y+1))*width+x]);
            grain.vx=(grain.vx-here.x*7.f*plate.agitation*step
                      +(noiseX*(shake+pressure)+std::clamp(px*.025f,-.5f,.5f))*step)*.88f;
            grain.vy=(grain.vy-here.y*7.f*plate.agitation*step
                      +(noiseY*(shake+pressure)+std::clamp(py*.025f,-.5f,.5f))*step)*.88f;
            grain.x+=grain.vx*step;grain.y+=grain.vy*step;
            if(grain.x<.5f){grain.x=.5f;grain.vx=std::abs(grain.vx)*.35f;}
            if(grain.x>width-.5f){grain.x=width-.5f;grain.vx=-std::abs(grain.vx)*.35f;}
            if(grain.y<.5f){grain.y=.5f;grain.vy=std::abs(grain.vy)*.35f;}
            if(grain.y>height-.5f){grain.y=height-.5f;grain.vy=-std::abs(grain.vy)*.35f;}
            grain.activity=here.energy*plate.agitation;
            if(std::abs(grain.vx)+std::abs(grain.vy)>.015f)++moving;
        }
        ++sweeps;grainUpdates+=grains.size();
    }
    void measure() {
        if(occupancy.empty()){spread=0;return;}
        std::fill(occupancy.begin(),occupancy.end(),0u);
        for(const auto& grain:grains) {
            int x=std::clamp(int(grain.x),0,width-1),y=std::clamp(int(grain.y),0,height-1);
            ++occupancy[size_t(y)*width+x];
        }
        double squares=0;
        for(unsigned n:occupancy)squares+=double(n)*n;
        double mean=double(grains.size())/occupancy.size();
        spread=float(std::sqrt(std::max(0.0,squares/occupancy.size()-mean*mean)));
    }
    void vertices(std::vector<float>& out,float pixelsX,float pixelsY) const {
        out.resize(grains.size()*3);
        float sx=pixelsX/width,sy=pixelsY/height;
        for(size_t i=0;i<grains.size();++i) {
            out[i*3]=grains[i].x*sx;out[i*3+1]=grains[i].y*sy;
            out[i*3+2]=grains[i].activity;
        }
    }
};
} // namespace orbital
