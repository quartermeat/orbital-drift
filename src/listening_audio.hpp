#pragma once
// Measured features of the desktop mix, not instrument labels or stem separation.
// No audio is saved or played back. Analysis is bounded and stays on the UI thread.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace orbital {
struct ListenFeatures {
    float bass=0,body=0,air=0,loudness=0,pulse=0,rms=0;
    // The strongest pitch in the mix: hz as measured, tone as its position across
    // the measured span, clarity as how much of the spectrum stands behind it.
    float toneHz=0,tone=0,clarity=0;
    uint64_t frames=0,onsets=0;
};

// A log-spaced Goertzel bank over the recent window. Four bins per octave is
// coarse for naming a note and ample for choosing a plate mode.
class ToneEstimator {
public:
    static constexpr int Rate=24000,Window=2048,Bins=25;
    static constexpr float Lowest=55.f,PerOctave=4.f;
    static float binHz(int bin) { return Lowest*std::pow(2.f,float(bin)/PerOctave); }
    float hz=0,tone=0,clarity=0;
    void push(float x) { ring[cursor]=x;if(++cursor==Window){cursor=0;filled=true;} }
    bool ready() const { return filled; }
    void measure() {
        if(!filled)return;
        const auto& window=hann();
        std::array<float,Window> samples{};
        for(int i=0;i<Window;++i)samples[size_t(i)]=ring[size_t((cursor+i)%Window)]*window[size_t(i)];
        std::array<float,Bins> magnitude{};float total=0;int peak=0;
        for(int bin=0;bin<Bins;++bin) {
            float coefficient=2*std::cos(2*Pi*binHz(bin)/Rate),s1=0,s2=0;
            for(int i=0;i<Window;++i){float s0=samples[size_t(i)]+coefficient*s1-s2;s2=s1;s1=s0;}
            float power=s1*s1+s2*s2-coefficient*s1*s2;
            // A mix is bottom-heavy by nature: without a tilt the peak is the
            // bass drum in every piece of music ever recorded.
            magnitude[size_t(bin)]=std::sqrt(std::max(0.f,power))*std::pow(2.f,float(bin)/PerOctave*.35f);
            total+=magnitude[size_t(bin)];
            if(magnitude[size_t(bin)]>magnitude[size_t(peak)])peak=bin;
        }
        if(total<1e-9f){clarity=0;return;}
        // Interpolating between neighbours recovers pitch the bank cannot resolve.
        float offset=0;
        if(peak>0&&peak<Bins-1) {
            float a=magnitude[size_t(peak-1)],b=magnitude[size_t(peak)],c=magnitude[size_t(peak+1)];
            float curve=a-2*b+c;
            if(std::abs(curve)>1e-12f)offset=std::clamp(.5f*(a-c)/curve,-1.f,1.f);
        }
        float index=std::clamp(float(peak)+offset,0.f,float(Bins-1));
        float shoulder=magnitude[size_t(peak)];
        if(peak>0)shoulder+=magnitude[size_t(peak-1)];
        if(peak<Bins-1)shoulder+=magnitude[size_t(peak+1)];
        hz=Lowest*std::pow(2.f,index/PerOctave);
        tone=std::clamp(index/float(Bins-1),0.f,1.f);
        clarity=std::clamp(shoulder/total*1.2f,0.f,1.f);
    }
private:
    static constexpr float Pi=3.14159265358979f;
    std::array<float,Window> ring{};
    int cursor=0;
    bool filled=false;
    static const std::array<float,Window>& hann() {
        static const std::array<float,Window> table=[] {
            std::array<float,Window> t{};
            for(int i=0;i<Window;++i)t[size_t(i)]=.5f-.5f*std::cos(2*Pi*float(i)/(Window-1));
            return t;
        }();
        return table;
    }
};

class ListenAnalyzer {
    static constexpr int Block=512;
    float low=0,low2=0,upper=0,upper2=0;
    double lowEnergy=0,midEnergy=0,highEnergy=0,energy=0;
    float baseline=.005f;
    int count=0,cooldown=0;
    bool toneDue=false;
    ToneEstimator pitch;
    static float smooth(float old,float next) { return old+(next-old)*(next>old?.55f:.12f); }
public:
    static constexpr int Rate=24000;
    ListenFeatures value;
    void push(float input) {
        float x=std::isfinite(input)?std::clamp(input,-1.f,1.f):0.f;
        pitch.push(x);
        // Cascaded low-pass filters at 180 Hz and 1800 Hz form three broad bands.
        constexpr float a=.0460309f,b=.3757716f;
        low+=a*(x-low);low2+=a*(low-low2);
        upper+=b*(x-upper);upper2+=b*(upper-upper2);
        float mid=upper2-low2,high=x-upper2;
        lowEnergy+=low2*low2;midEnergy+=mid*mid;highEnergy+=high*high;energy+=x*x;
        ++value.frames;
        if(++count!=Block)return;
        float bass=std::sqrt(lowEnergy/Block),body=std::sqrt(midEnergy/Block),air=std::sqrt(highEnergy/Block);
        value.rms=std::sqrt(energy/Block);
        auto scale=[](float v,float gain){return std::clamp(std::sqrt(v*gain),0.f,1.f);};
        value.bass=smooth(value.bass,scale(bass,8));
        value.body=smooth(value.body,scale(body,6));
        value.air=smooth(value.air,scale(air,5));
        value.loudness=smooth(value.loudness,scale(value.rms,6));
        value.pulse*=.83f;
        if(cooldown>0)--cooldown;
        if(bass>.007f&&bass>baseline*1.5f&&cooldown==0) {
            value.pulse=1;cooldown=7;++value.onsets;
        }
        baseline+=(bass-baseline)*.035f;
        if(value.rms<.00005f) {
            value.bass*=.7f;value.body*=.7f;value.air*=.7f;value.loudness*=.7f;
        }
        for(float* v:{&value.bass,&value.body,&value.air,&value.loudness,&value.pulse,&low,&low2,&upper,&upper2})
            if(std::abs(*v)<1e-12f)*v=0;
        count=0;lowEnergy=midEnergy=highEnergy=energy=0;
        toneDue=true;
    }
    // Pitch costs more than the bands, so it is measured once per batch of
    // samples rather than once per block: a caller that reads a backlog in one
    // frame pays for one bank, not sixty.
    void settle() {
        if(!toneDue)return;
        toneDue=false;
        pitch.measure();
        // A pitch is only followed while one is actually standing out. Silence
        // and noise hold the last tone instead of dragging it to the floor.
        value.clarity+=(pitch.clarity-value.clarity)*.2f;
        if(pitch.clarity>.30f&&value.rms>.0004f) {
            value.toneHz+=(pitch.hz-value.toneHz)*.25f;
            value.tone+=(pitch.tone-value.tone)*.3f;
        }
        for(float* v:{&value.clarity,&value.tone,&value.toneHz})
            if(std::abs(*v)<1e-12f)*v=0;
    }
};
} // namespace orbital
