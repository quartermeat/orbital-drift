#include "listening_audio.hpp"
#include "desktop_monitor.hpp"
#include "chladni.hpp"
#include "particle_sand.hpp"
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

    // The rectangular resonances have stationary lines at fixed fractions of
    // each edge. Widening the plate lowers only the x contribution to pitch.
    PlateMode grid{3,1,1},stripe{2,0,1};
    assert(std::abs(grid.displacement(.5f,.1f))<1e-6f);
    assert(std::abs(grid.displacement(.1f,.5f))<1e-6f);
    assert(std::abs(stripe.displacement(.25f,.25f))<1e-6f);
    assert(std::abs(stripe.displacement(0,0)-1)<1e-6f);
    assert(grid.frequency(2)==3.25f&&grid.frequency(1)==10);
    assert(stripe.frequency(2)==1&&stripe.frequency(1)==4);
    assert(plateMode(10,1)!=plateMode(10,2)); // window proportions select the mode

    ListenFeatures loud{};loud.rms=.1f;loud.loudness=.6f;loud.clarity=.9f;
    ListenFeatures deepTone=loud,brightTone=loud;
    deepTone.tone=.08f;deepTone.toneHz=90;brightTone.tone=.92f;brightTone.toneHz=2400;
    PlateDriver deepPlate,brightPlate;
    for(int i=0;i<600;++i){deepPlate.update(deepTone,1.f/60);brightPlate.update(brightTone,1.f/60);}
    // A higher note breaks the plate into more of everything.
    assert(brightPlate.live.frequency(1)>deepPlate.live.frequency(1)*2);
    assert(deepPlate.agitation>.5f&&brightPlate.agitation>.5f);
    PlateDriver hush;for(int i=0;i<600;++i)hush.update({},1.f/60);
    assert(hush.agitation==0); // silence stops the plate rather than fading it
    PlateDriver held=brightPlate;held.paused=true;
    for(int i=0;i<400;++i)held.update(loud,1.f/60);
    assert(held.agitation==0); // pausing settles the plate however loud the room is
    ListenFeatures broken{};broken.tone=NAN;broken.loudness=INFINITY;broken.rms=1;broken.toneHz=NAN;
    PlateDriver survivor;for(int i=0;i<60;++i)survivor.update(broken,NAN);
    assert(std::isfinite(survivor.agitation)&&survivor.live.n>=1);
    auto steady=brightPlate.live;
    for(int i=0;i<600;++i)brightPlate.update(brightTone,1.f/60);
    assert(brightPlate.live==steady); // standing waves never rotate
    PlateDriver melody=deepPlate;
    for(int i=0;i<600;++i)melody.update((i/15)%2?brightTone:deepTone,1.f/60,2.5f);
    assert(melody.reconfigures<5); // short notes cannot continually redraw the plate
    auto prior=melody.live;
    for(int i=0;i<240;++i)melody.update(brightTone,1.f/60,2.5f);
    assert(melody.live!=prior); // a sustained new pitch eventually forms a new figure
    brightPlate.clear();assert(brightPlate.clears==1&&brightPlate.agitation==0&&brightPlate.live==PlateDriver{}.live);

    // Every visible grain has its own persistent motion state. A force lookup
    // grid does not replace individual grains with counts or block exchanges.
    PlateDriver table;table.live={4,2,1};table.agitation=.9f;
    ParticleSand tray;tray.reset(97,63);
    const size_t grainCount=97*63;
    assert(tray.grains.empty()&&tray.grainUpdates==0);
    tray.measure();assert(tray.spread==0);
    assert(tray.add(10,20)&&tray.grains.size()==1);
    assert(tray.grains[0].x==10&&tray.grains[0].y==20);
    tray.reset(97,63);assert(tray.grains.empty());
    for(int y=0;y<tray.height;++y)for(int x=0;x<tray.width;++x)
        assert(tray.add(x+.5f,y+.5f));
    assert(!tray.add(10,20)&&tray.grains.size()==grainCount);
    auto original=tray.grains;
    for(int i=0;i<300;++i)tray.update(table,1.f/60);
    assert(tray.grains.size()==grainCount);
    assert(tray.grainUpdates==grainCount*300&&tray.sweeps==300);
    size_t changed=0;
    for(size_t i=0;i<grainCount;++i) {
        const auto& grain=tray.grains[i];
        assert(std::isfinite(grain.x)&&std::isfinite(grain.y)&&std::isfinite(grain.activity));
        assert(grain.x>=.5f&&grain.x<=tray.width-.5f&&grain.y>=.5f&&grain.y<=tray.height-.5f);
        if(grain.x!=original[i].x||grain.y!=original[i].y)++changed;
    }
    assert(changed>grainCount*9/10);
    tray.measure();assert(tray.spread>.3f); // individual motion forms a visible pile
    auto heldGrains=tray.grains;
    PlateDriver quietTable=table;quietTable.agitation=0;
    tray.update(quietTable,1.f/60);
    PlateDriver pausedTable=table;pausedTable.paused=true;
    tray.update(pausedTable,1.f/60);
    assert(tray.grains==heldGrains&&tray.grainUpdates==grainCount*300);
    tray.reset(97,63);
    tray.measure();assert(tray.grains.empty()&&tray.spread==0);

    // Routing follows what is sounding now, not what opened a stream first.
    const char* sinks="58\tspeakers\tPipeWire\n131\teasyeffects_sink\tPipeWire\n";
    const char* playing=
        "Sink Input #42\n\tDriver: PipeWire\n\tSink: 131\n\tMute: no\n\tCorked: no\n";
    const char* paused=
        "Sink Input #7\n\tSink: 58\n\tMute: no\n\tCorked: yes\n"
        "Sink Input #42\n\tSink: 131\n\tMute: no\n\tCorked: no\n";
    const char* muted=
        "Sink Input #7\n\tSink: 58\n\tMute: yes\n\tCorked: no\n"
        "Sink Input #42\n\tSink: 131\n\tMute: no\n\tCorked: no\n";
    assert(DesktopMonitor::route(playing,sinks)=="easyeffects_sink.monitor");
    // A paused player held the first place in the list and used to win it.
    assert(DesktopMonitor::route(paused,sinks)=="easyeffects_sink.monitor");
    assert(DesktopMonitor::route(muted,sinks)=="easyeffects_sink.monitor");
    assert(DesktopMonitor::route("",sinks)=="@DEFAULT_MONITOR@");
    assert(DesktopMonitor::route("Sink Input #7\n\tSink: 99\n\tCorked: no\n",sinks)=="@DEFAULT_MONITOR@");
    // The desktop's own output is the answer until it proves silent.
    assert(DesktopMonitor::musicSource(false)=="@DEFAULT_MONITOR@");
    auto choices=DesktopMonitor::parseSources(
        "Source #1\n\tName: speakers.monitor\n\tDescription: Monitor of Speakers\n\tMonitor of Sink: speakers\n"
        "Source #2\n\tName: webcam.mic\n\tDescription: Brio Webcam Mono\n\tMonitor of Sink: n/a\n"
        "Source #3\n\tName: analog.mic\n\tDescription: Analog Stereo\n\tMonitor of Sink: n/a\n"
        "\tPorts:\n\t\tanalog-input-rear-mic: Rear Microphone (type: Mic)\n"
        "\tActive Port: analog-input-rear-mic\n"
        "Source #4\n\tName: virtual.mic\n\tDescription: Effects Source\n\tMonitor of Sink: n/a\n"
        "\t\tnode.virtual = \"true\"\n");
    assert(choices.size()==2&&choices[0].name=="webcam.mic"&&choices[0].input);
    assert(choices[0].label=="MIC: Brio Webcam Mono");
    assert(choices[1].name=="analog.mic"&&choices[1].input);
    assert(choices[1].label=="MIC: Rear Microphone (Analog Stereo)");
    std::cout<<"PASS: measured bands, pulses, silence, tracked pitch, plate modes, placed stateful grains, source discovery, pause and clear\n";
}
