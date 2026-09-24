#pragma once
#include "listening_audio.hpp"

namespace orbital {
// The magnetic ball follows a continuous rosette whose radius, drift and speed
// come from the current mix. Silence stops the mechanism; the sand remembers.
struct SandMotion {
    float x=.72f,y=0,previousX=.72f,previousY=0;
    double angle=0,drift=0,distance=0;
    float speed=0,drive=1;
    bool paused=false;
    int clears=0;
    void clear(){int next=clears+1;*this=SandMotion{};clears=next;}
    void update(const ListenFeatures& f,float dt,bool hand=false,float handX=0,float handY=0) {
        previousX=x;previousY=y;speed=0;
        dt=std::clamp(dt,0.f,.05f);
        if(paused||dt==0||(!hand&&f.rms<.0001f))return;
        float tx=handX,ty=handY;
        if(!hand) {
            angle+=dt*drive*(.40+f.loudness*1.1+f.pulse*.7);
            drift+=dt*(.08+f.air*.27);
            float radius=.46f+.13f*f.bass+(.17f+.10f*f.body)*std::sin(angle*5+drift);
            tx=radius*std::cos(angle)+.055f*std::cos(angle*3-drift);
            ty=radius*std::sin(angle)+.055f*std::sin(angle*3-drift);
        }
        if(!std::isfinite(tx)||!std::isfinite(ty))return;
        float radius=std::hypot(tx,ty);
        if(radius>.93f){tx*=.93f/radius;ty*=.93f/radius;}
        float dx=tx-x,dy=ty-y,length=std::hypot(dx,dy);
        float step=std::min(length,dt*drive*(hand?1.3f:.45f+f.loudness*.65f+f.pulse*.25f));
        if(length>1e-6f){x+=dx/length*step;y+=dy/length*step;}
        speed=step/dt;distance+=step;
    }
};
} // namespace orbital
