#include "listening_audio.hpp"
#include "sand_motion.hpp"
#include "desktop_monitor.hpp"
#include "chladni.hpp"
#include <numeric>
#include <vector>
#include <cassert>
#include <iostream>
#include <limits>

using namespace orbital;
static ListenFeatures tone(float hz) {
    ListenAnalyzer a;
    for(int i=0;i<ListenAnalyzer::Rate;++i)a.push(.1f*std::sin(2*3.141592653589793*hz*i/ListenAnalyzer::Rate));
    return a.value;
}
int main() {
    auto low=tone(70),mid=tone(700),high=tone(7000);
    assert(low.bass>low.air*3&&low.bass>low.body);
    assert(mid.body>mid.bass*2);
    assert(high.air>high.bass*5&&high.air>high.body);
    ListenAnalyzer a;
    for(int i=0;i<48000;++i) {
        bool on=(i%12000)<4000;
        a.push(on?.15f*std::sin(2*3.141592653589793*80*i/24000):0);
    }
    assert(a.value.onsets>=3&&a.value.onsets<12);
    for(int i=0;i<24000;++i)a.push(0);
    assert(a.value.bass<.001&&a.value.body<.001&&a.value.air<.001&&a.value.pulse<.001);
    a.push(std::numeric_limits<float>::quiet_NaN());a.push(std::numeric_limits<float>::infinity());
    for(int i=0;i<1024;++i)a.push(0);
    assert(std::isfinite(a.value.rms));
    SandMotion sand;
    ListenFeatures music;music.rms=.1;music.bass=.4;music.body=.5;music.air=.6;
    music.loudness=.6;
    for(int i=0;i<200;++i)sand.update({},.02f);
    assert(sand.distance==0&&sand.speed==0);
    for(int i=0;i<12000;++i) {
        float x=sand.x,y=sand.y;sand.update(music,.02f);
        assert(std::hypot(sand.x,sand.y)<.95f);
        assert(std::hypot(sand.x-x,sand.y-y)<.03f);
    }
    assert(sand.distance>50);
    double travelled=sand.distance;
    sand.paused=true;sand.update(music,.05f);assert(sand.distance==travelled);
    sand.paused=false;sand.update({},.05f);assert(sand.distance==travelled&&sand.speed==0);
    sand.update({},.05f,true,4,4);assert(std::hypot(sand.x,sand.y)<.95f);
    sand.update({},.05f,true,NAN,0);assert(std::isfinite(sand.x));
    sand.clear();assert(sand.distance==0&&sand.clears==1&&!sand.paused);
    SandMotion bassPath,brightPath;
    ListenFeatures bright=music;bright.bass=0;bright.air=1;
    for(int i=0;i<400;++i){bassPath.update(music,.02f);brightPath.update(bright,.02f);}
    assert(std::hypot(bassPath.x-brightPath.x,bassPath.y-brightPath.y)>.02f);
    // The pitch behind the plate. A bank this coarse is not naming notes, so
    // the tolerance is the bank's own resolution rather than a tuner's.
    auto pitched=[](float hz,bool noise=false) {
        ListenAnalyzer a;unsigned seed=1;
        for(int i=0;i<ListenAnalyzer::Rate*2;++i) {
            seed=seed*1103515245u+12345u;
            float x=noise?(float(seed>>16&0x7fff)/16383.5f-1.f)*.2f
                         :.2f*std::sin(2*3.141592653589793*hz*i/ListenAnalyzer::Rate);
            a.push(x);a.settle();
        }
        return a.value;
    };
    for(float hz:{110.f,220.f,440.f,1760.f}) {
        auto heard=pitched(hz);
        assert(std::abs(heard.toneHz-hz)<hz*.08f);
        assert(heard.clarity>.8f&&heard.tone>0&&heard.tone<1);
    }
    assert(pitched(220).tone<pitched(1760).tone);
    // Noise has no pitch to follow, and saying so is the point of clarity.
    assert(pitched(0,true).clarity<.3f);

    // Bessel functions against published zeros and values: the plate's nodal
    // circles sit exactly where these cross zero, so an approximation that
    // drifts puts the sand in the wrong place.
    assert(std::abs(besselJ(0,0.f)-1.f)<1e-5f);
    assert(std::abs(besselJ(0,2.404826f))<3e-3f);
    assert(std::abs(besselJ(1,3.831706f))<3e-3f);
    assert(std::abs(besselJ(2,5.135622f))<3e-3f);
    assert(std::abs(besselJ(0,8.f)-.171651f)<.02f);
    assert(std::isfinite(besselJ(3,120.f))&&std::abs(besselJ(3,120.f))<1.f);

    ListenFeatures loud{};loud.rms=.1f;loud.loudness=.6f;loud.clarity=.9f;
    ListenFeatures deepTone=loud,brightTone=loud;
    deepTone.tone=.08f;deepTone.toneHz=90;brightTone.tone=.92f;brightTone.toneHz=2400;
    PlateDriver deepPlate,brightPlate;
    for(int i=0;i<600;++i){deepPlate.update(deepTone,1.f/60);brightPlate.update(brightTone,1.f/60);}
    // A higher note breaks the plate into more of everything.
    assert(brightPlate.live.rings>deepPlate.live.rings*2&&brightPlate.live.lobes>deepPlate.live.lobes);
    assert(deepPlate.agitation>.5f&&brightPlate.agitation>.5f);
    PlateDriver hush;for(int i=0;i<600;++i)hush.update({},1.f/60);
    assert(hush.agitation==0); // silence stops the plate rather than fading it
    PlateDriver held=brightPlate;held.paused=true;
    for(int i=0;i<400;++i)held.update(loud,1.f/60);
    assert(held.agitation==0); // pausing settles the plate however loud the room is
    ListenFeatures broken{};broken.tone=NAN;broken.loudness=INFINITY;broken.rms=1;broken.toneHz=NAN;
    PlateDriver survivor;for(int i=0;i<60;++i)survivor.update(broken,NAN);
    assert(std::isfinite(survivor.live.rings)&&std::isfinite(survivor.agitation)&&survivor.live.lobes>=1);
    brightPlate.clear();assert(brightPlate.clears==1&&brightPlate.agitation==0&&brightPlate.live.lobes==PlateDriver{}.live.lobes);

    // The plate must move sand, not make it. Grains are whole things moved
    // between cells in 2x2 blocks, so the tray holds exactly the same number
    // after an hour of shaking -- exactly, not nearly.
    PlateDriver table;table.live.rings=9;table.live.lobes=4;table.agitation=.9f;
    const int side=64;
    std::vector<float> tray(size_t(side)*side,PlateBedGrains);
    double startingGrains=std::accumulate(tray.begin(),tray.end(),0.0);
    for(int i=0;i<900;++i){table.frames=uint64_t(i);plateTransport(tray,side,table,1.f/60);}
    assert(std::accumulate(tray.begin(),tray.end(),0.0)==startingGrains);
    for(float depth:tray)assert(depth>=0&&depth==std::floor(depth)); // whole grains only
    double still=0,stillCells=0,shaken=0,shakenCells=0;
    for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
        float px=2.f*(float(x)+.5f)/side-1,py=2.f*(float(y)+.5f)/side-1;
        if(!onPlate(px,py))continue;
        float here=tray[size_t(y)*side+size_t(x)],shake=table.energy(px,py);
        if(shake<.02f){still+=here;++stillCells;}
        else if(shake>.25f){shaken+=here;++shakenCells;}
    }
    assert(stillCells>0&&shakenCells>0);
    // Sand ends up on the still lines and leaves the shaking ground.
    assert(still/stillCells>PlateBedGrains&&shaken/shakenCells<PlateBedGrains*.5);
    std::vector<float> untouched(size_t(side)*side,PlateBedGrains);
    PlateDriver quietTable=table;quietTable.agitation=0;
    plateTransport(untouched,side,quietTable,1.f/60);
    PlateDriver pausedTable=table;pausedTable.paused=true;
    plateTransport(untouched,side,pausedTable,1.f/60);
    // Silence and a pause both keep the figure exactly: no fade, no timer.
    for(float depth:untouched)assert(depth==PlateBedGrains);
    // The block hash has to be free of the diagonal structure a sine hash has,
    // because a block automaton prints its own noise onto the tray.
    double drawn=0;int samples=0;
    for(int y=0;y<64;++y)for(int x=0;x<64;++x){drawn+=plateHash(x,y,7);++samples;}
    double average=drawn/samples;
    assert(average>.45&&average<.55);
    assert(plateHash(3,5,1)!=plateHash(5,3,1)&&plateHash(3,5,1)!=plateHash(3,5,2));

    assert(DesktopMonitor::route("42\t131\t3\tPipeWire\n","58\tspeakers\tPipeWire\n131\teasyeffects_sink\tPipeWire\n")=="easyeffects_sink.monitor");
    assert(DesktopMonitor::route("","58\tspeakers\tPipeWire\n")=="@DEFAULT_MONITOR@");
    DesktopMonitor monitor;monitor.requestedSource="@DEFAULT_SOURCE@";
    assert(!monitor.start()&&!monitor.error.empty()); // microphone fallback is forbidden
    std::cout<<"PASS: measured bands, pulses, silence, tracked pitch, continuous bounded sand motion, plate modes from pitch, sand conserved and sorted onto the nodal lines, pause, clear, output-only capture\n";
}
