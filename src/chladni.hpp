#pragma once
// A Chladni plate: sand on a disc that is being vibrated by whatever is playing.
// The pattern is not drawn. A standing wave decides where the plate is still,
// and the grains walk off the shaking parts until only the quiet lines are left.
// Everything here has a twin in assets/chladni-update.fs; that shader moves the
// real 1536-square field and this side exists so the rule can be tested and so
// the pattern can be reported without reading the GPU back.
#include "listening_audio.hpp"
#include <cstdint>
#include <vector>

namespace orbital {
constexpr float PlatePi=3.14159265358979f;

// Bessel functions of the first kind: the series near the middle of the plate,
// the standing-wave approximation further out, blended where they meet.
inline float besselJ(int order,float x) {
    x=std::abs(x);
    int m=std::clamp(order,0,12);
    // Past the blend the series is dead weight that would overflow on its way
    // to being multiplied by zero, so it is simply not walked out there.
    float series=0;
    if(x<7.5f) {
        float term=1;
        for(int i=1;i<=m;++i)term*=x*.5f/float(i);
        series=term;
        float quarter=x*x*.25f;
        for(int k=1;k<=18;++k){term*=-quarter/(float(k)*float(k+m));series+=term;}
    }
    float wave=x>1e-4f?std::sqrt(2.f/(PlatePi*x))*std::cos(x-float(m)*PlatePi*.5f-PlatePi*.25f):0.f;
    float t=std::clamp((x-5.f)*.5f,0.f,1.f);
    float blend=t*t*(3-2*t);
    return series*(1-blend)+wave*blend;
}

// One standing wave on the disc. rings is the radial wavenumber, lobes the
// number of diameters, spin where those diameters currently point.
struct PlateMode {
    float rings=6,spin=0;
    int lobes=3;
    // Multiplying by the square root of the argument flattens the Bessel
    // envelope so the rim shakes as hard as the middle. It cannot move a zero,
    // so every nodal line stays exactly where the physics puts it.
    float displacement(float x,float y) const {
        float r=std::hypot(x,y),argument=rings*r;
        float radial=besselJ(lobes,argument)*std::sqrt(std::max(1.f,argument));
        float angle=lobes?std::atan2(y,x):0.f;
        return radial*std::cos(float(lobes)*angle+spin);
    }
};

// The plate as it is being driven right now.
struct PlateDriver {
    PlateMode live,harmonic;
    float harmonicWeight=0,agitation=0,tuning=1,toneHz=0,clarity=0;
    uint64_t frames=0;
    int reconfigures=0,clears=0;
    bool paused=false;
    // How hard the plate is shaken, and how far the pattern has settled.
    float displacement(float x,float y) const {
        float value=live.displacement(x,y);
        if(harmonicWeight>.001f)value+=harmonicWeight*harmonic.displacement(x,y);
        return value/(1+harmonicWeight);
    }
    float energy(float x,float y) const {
        float d=displacement(x,y);
        return std::clamp(d*d*.55f,0.f,1.f);
    }
    void clear() { int next=clears+1;*this=PlateDriver{};clears=next; }
    void update(const ListenFeatures& features,float dt) {
        ++frames;
        dt=std::clamp(std::isfinite(dt)?dt:0.f,0.f,.05f);
        auto safe=[](float v){return std::isfinite(v)?std::clamp(v,0.f,1.f):0.f;};
        float tone=safe(features.tone),loudness=safe(features.loudness);
        float pulse=safe(features.pulse),air=safe(features.air),body=safe(features.body);
        clarity=safe(features.clarity);
        if(std::isfinite(features.toneHz))toneHz=std::clamp(features.toneHz,0.f,20000.f);
        float drive=features.rms>.0001f?std::clamp(loudness*1.7f+pulse*.5f,0.f,1.f):0.f;
        if(paused)drive=0;
        agitation+=(drive-agitation)*std::min(1.f,dt*4.f);
        if(agitation<1e-6f)agitation=0;
        if(dt==0)return;
        // Pitch chooses the mode: low notes ring the plate in a few wide bands,
        // high notes break it into many.
        float wanted=(3.2f+21.f*std::pow(tone,1.15f))*std::clamp(tuning,.25f,3.f);
        live.rings+=(wanted-live.rings)*std::min(1.f,dt*2.2f);
        harmonic.rings=live.rings*1.53f;
        harmonicWeight+=(air*.35f-harmonicWeight)*std::min(1.f,dt*1.5f);
        float wantedLobes=1.f+7.f*std::pow(tone,1.3f);
        int candidate=std::abs(wantedLobes-float(live.lobes))>.65f
            ?std::max(1,int(std::lround(wantedLobes))):live.lobes;
        if(candidate!=pending){pending=candidate;hold=0;}
        else hold+=dt;
        // Lobes are whole numbers, so the figure has to jump. It waits for a
        // beat when one is coming, and for a settled pitch when one is not.
        if(pending!=live.lobes&&(hold>.35f||(pulse>.5f&&hold>.08f))) {
            live.lobes=pending;harmonic.lobes=std::min(12,pending+3);++reconfigures;hold=0;
        }
        // A standing wave stands still. The figure is allowed the barest drift so
        // the tray is not a photograph, but anything more and the sand can never
        // settle onto the diameters -- it only ever finds the rings.
        if(agitation>0) {
            live.spin+=dt*(.004f+.020f*body);
            harmonic.spin-=dt*(.003f+.014f*body);
            auto wrap=[](float& a){if(a>2*PlatePi)a-=2*PlatePi;if(a<-2*PlatePi)a+=2*PlatePi;};
            wrap(live.spin);wrap(harmonic.spin);
        }
    }
private:
    int pending=3;
    float hold=0;
};

// Grains, not depth. A cell holds a whole number of them and the tray is a
// grid of matter rather than a height map -- sand is simply the only kind of
// matter in it so far. Everything below is mirrored by
// assets/chladni-update.fs; change them together.
constexpr float PlateBedGrains=6;
constexpr float PlateReposeGap=22;  // grains of difference a slope will hold
constexpr float PlateSettleChance=.5f;

inline bool onPlate(float x,float y) { return x*x+y*y<.999f*.999f; }

// One hash, agreed on by both sides. Seeded per block rather than per cell, so
// the four cells of a block draw the same number and reach the same decision.
inline float plateHash(int x,int y,int salt) {
    uint32_t h=uint32_t(x)*73856093u^uint32_t(y)*19349663u^uint32_t(salt)*83492791u;
    h=(h^61u)^(h>>16);h*=9u;h=h^(h>>4);h*=0x27d4eb2du;h=h^(h>>15);
    return float(h&0xffffffu)/float(0x1000000u);
}

// A 2x2 block decides for itself which single grain moves and where. Every cell
// in the block runs this and reads off its own answer, so no grain is ever
// duplicated or dropped between two cells that each thought they had it: the
// count is conserved by construction rather than by careful arithmetic.
struct PlateBlock {
    float count[4]{};
    float energy[4]{};
    bool inside[4]{};
    void settle(float agitation,float push,float draw,float settleDraw) {
        // Sand leaves the most violent cell it can and looks for the stillest.
        int from=-1,to=-1;
        for(int i=0;i<4;++i) {
            if(!inside[i])continue;
            if(count[i]>0&&(from<0||energy[i]>energy[from]))from=i;
            if(to<0||energy[i]<energy[to])to=i;
        }
        if(from>=0&&to>=0&&from!=to) {
            float chance=std::clamp(push*(energy[from]-energy[to])*agitation,0.f,.95f);
            if(draw<chance) {
                // A harder shake throws more of the cell at once. Moving a share
                // of what is there, rather than a fixed grain or two, is what
                // lets sand actually cross the tray: one block passes one grain
                // per sweep, so a fixed rate leaves the far side untouched.
                float grains=std::max(1.f,std::floor(count[from]*chance*.8f));
                count[from]-=grains;count[to]+=grains;
            }
        }
        // A pile too steep for itself slumps, shaking or not.
        int tall=-1,low=-1;
        for(int i=0;i<4;++i) {
            if(!inside[i])continue;
            if(tall<0||count[i]>count[tall])tall=i;
            if(low<0||count[i]<count[low])low=i;
        }
        if(tall>=0&&low>=0&&tall!=low&&count[tall]-count[low]>=PlateReposeGap
           &&settleDraw<PlateSettleChance) {
            // One grain at a time: a slump that moves half the pile back is a
            // slump that erases the figure the shaking just built.
            count[tall]-=1;count[low]+=1;
        }
    }
};

// The grid of the tray, used by the tests. The shader walks the same blocks;
// this walks each block once, which is the clearest way to show that nothing is
// created or destroyed.
inline void plateTransport(std::vector<float>& grains,int size,const PlateDriver& plate,float dt) {
    if(plate.paused||plate.agitation<=0||dt<=0||size<2)return;
    float push=.055f*(float(size)*.5f)*std::clamp(dt,0.f,.05f)*60.f;
    auto position=[size](int i){return 2.f*(float(i)+.5f)/float(size)-1.f;};
    // Alternating the block offset is what lets a grain cross a block boundary;
    // on a fixed grid it would rattle inside its own four cells forever.
    int offset=int(plate.frames)&1;
    for(int by=-offset;by<size;by+=2)for(int bx=-offset;bx<size;bx+=2) {
        PlateBlock block;
        int cellX[4]={bx,bx+1,bx,bx+1},cellY[4]={by,by,by+1,by+1};
        for(int i=0;i<4;++i) {
            int x=cellX[i],y=cellY[i];
            if(x<0||y<0||x>=size||y>=size)continue;
            float px=position(x),py=position(y);
            if(!onPlate(px,py))continue;
            block.inside[i]=true;
            block.count[i]=grains[size_t(y)*size_t(size)+size_t(x)];
            block.energy[i]=plate.energy(px,py);
        }
        block.settle(plate.agitation,push,
                     plateHash(bx,by,int(plate.frames)),
                     plateHash(bx,by,int(plate.frames)+37));
        for(int i=0;i<4;++i) {
            if(!block.inside[i])continue;
            grains[size_t(cellY[i])*size_t(size)+size_t(cellX[i])]=block.count[i];
        }
    }
}
} // namespace orbital
