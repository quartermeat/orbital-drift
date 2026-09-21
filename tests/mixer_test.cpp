#include "mixer.hpp"
#include <iostream>

void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using namespace orbital;
    Mixer m;
    m.trackCount=7;
    m.frames=10000;
    m.volume=1;
    for (int t=0;t<m.trackCount;++t) {
        auto& track=m.tracks[t];
        track.resize(m.frames*2);
        for (size_t i=0;i<m.frames;++i) {
            track[i*2]=.01f+float(i)*.000001f;
            track[i*2+1]=-track[i*2];
        }
    }
    std::vector<float> out(4096*2);
    m.render(out.data(),4096);
    require(m.position==4096,"one shared cursor advances");
    require(out.front()==0 || std::abs(out.front())<.0001,"startup fades in");
    for (int track=0; track<m.trackCount; ++track) {
        m.enabled=m.allMask()^(1u<<track);
        m.render(out.data(),2048);
        auto expected=(.01f+float((m.cursor+m.frames-1)%m.frames)*.000001f)*6;
        require(std::abs(out[2047*2]-expected)<.00001,"each toggle removes exactly one stem");
        require(std::abs(out[2047*2+1]+expected)<.00001,"stereo polarity is preserved");
        m.toggle(track);
        auto before=m.cursor;
        m.render(out.data(),2048);
        require(m.cursor==(before+2048)%m.frames,"unmuting never restarts playback");
    }
    m.enabled=0;
    m.render(out.data(),2048);
    require(out[4094]==0 && out[4095]==0,"all tracks fade to exact silence");
    auto before=m.cursor;
    m.render(out.data(),2048);
    require(m.cursor==(before+2048)%m.frames,"muted tracks keep moving");
    m.playing=false;
    m.render(out.data(),2048);
    before=m.cursor;
    m.render(out.data(),2048);
    require(m.cursor==before,"pause freezes the shared cursor after its fade");
    m.playing=true;
    m.enabled=m.allMask();
    m.render(out.data(),2048);
    require(m.cursor==(before+2048)%m.frames,"resume continues from paused position");
    require(m.outputRms>0,"resume produces sound");
    auto temp=std::filesystem::temp_directory_path()/"orbital-invalid-wave.wav";
    {std::ofstream f(temp);f<<"not a WAV";}
    bool rejected=false;
    try {loadWav(temp);} catch(const std::runtime_error&) {rejected=true;}
    std::filesystem::remove(temp);
    require(rejected,"bad assets fail visibly");
    std::cout<<"PASS: seven toggles, phase continuity, looping, fades, pause, stereo, malformed asset\n";
}
