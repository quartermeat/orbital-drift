#pragma once
// A Chladni plate: sand across the window, vibrated by whatever is playing.
// The pattern is not drawn. A standing wave decides where the plate is still,
// and the grains walk off the shaking parts until only the quiet lines are left.
// The plate supplies the force sampled by each grain in particle_sand.hpp.
// The standing wave can also be inspected without reading the GPU back.
#include "listening_audio.hpp"
#include <cstdint>
#include <vector>

namespace orbital {
constexpr float PlatePi=3.14159265358979f;

// A separable free-edge approximation in window coordinates [0,1]. The two
// directions have different physical lengths, so swapping n and m is not the
// same resonance on a rectangular plate.
struct PlateMode {
    int n=3,m=1,sign=1;
    bool operator==(const PlateMode&) const = default;
    float frequency(float aspect) const {return float(n*n)/(aspect*aspect)+float(m*m);}
    float displacement(float x,float y) const {
        return sign*std::cos(n*PlatePi*x)*std::cos(m*PlatePi*y);
    }
};

inline PlateMode plateMode(float frequency,float aspect,const PlateMode* exclude=nullptr) {
    PlateMode best;float error=1e20f;
    for(int n=0;n<=32;++n)for(int m=0;m<=16;++m) {
        if(n==0&&m==0)continue;
        if(exclude&&n==exclude->n&&m==exclude->m)continue;
        PlateMode candidate{n,m,1};
        float distance=std::abs(candidate.frequency(aspect)-frequency);
        if(distance<error){best=candidate;error=distance;}
    }
    return best;
}

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
    void update(const ListenFeatures& features,float dt,float aspect=1) {
        ++frames;
        dt=std::clamp(std::isfinite(dt)?dt:0.f,0.f,.05f);
        auto safe=[](float v){return std::isfinite(v)?std::clamp(v,0.f,1.f):0.f;};
        float tone=safe(features.tone),loudness=safe(features.loudness);
        float pulse=safe(features.pulse);
        clarity=safe(features.clarity);
        if(std::isfinite(features.toneHz))toneHz=std::clamp(features.toneHz,0.f,20000.f);
        float drive=features.rms>.0001f?std::clamp(loudness*1.7f+pulse*.5f,0.f,1.f):0.f;
        if(paused)drive=0;
        agitation+=(drive-agitation)*std::min(1.f,dt*4.f);
        if(agitation<1e-6f)agitation=0;
        if(dt==0||paused||drive==0)return;
        aspect=std::isfinite(aspect)?std::clamp(aspect,.1f,10.f):1.f;
        // Frequency is proportional to (n/W)^2 + (m/H)^2, with height as
        // the unit length. Pitch and tuning select a mode for this window.
        float wanted=(4.f+96.f*std::pow(tone,1.15f))*std::pow(std::clamp(tuning,.25f,3.f),2.f);
        if(clarity>.2f){pitchSum+=wanted*clarity*dt;pitchWeight+=clarity*dt;}
        modeAge+=dt;
        // Two nearly coincident rectangular resonances can bend a nodal line.
        // Distant modes are not mixed in merely because the music is bright.
        float near=std::abs(harmonic.frequency(aspect)-live.frequency(aspect));
        float companion=near<live.frequency(aspect)*.045f?.32f:0.f;
        harmonicWeight+=(companion-harmonicWeight)*std::min(1.f,dt*2.f);
        // Music has many short pitches; choose a resonance from a few seconds
        // of evidence, then let the sand settle before considering another.
        if(modeAge<3.f)return;
        modeAge=0;
        float average=pitchWeight>.7f?pitchSum/pitchWeight:0.f;
        pitchSum=pitchWeight=0;
        if(average==0||std::abs(live.frequency(aspect)-average)<average*.12f)return;
        PlateMode candidate=plateMode(average,aspect);
        if(candidate!=live) {
            live=candidate;harmonic=plateMode(average,aspect,&live);
            harmonic.sign=(live.n+harmonic.m)%2?-1:1;
            ++reconfigures;
        }
    }
private:
    float modeAge=0,pitchSum=0,pitchWeight=0;
};

} // namespace orbital
