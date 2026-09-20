#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace orbital {
constexpr int TrackCount = 7;
constexpr int SampleRate = 48000;
constexpr uint32_t AllTracks = (1u << TrackCount) - 1;
inline const std::array<const char*, TrackCount> Files = {
    "01 Nebula Pad.wav", "02 Starlight Echoes.wav", "03 Cosmic Dust.wav",
    "04 Warm Sub.wav", "05 Soft Kick.wav", "06 Distant Snare.wav", "07 Orbit Hats.wav"};
inline const std::array<const char*, TrackCount> Names = {
    "Nebula Pad", "Starlight Echoes", "Cosmic Dust", "Warm Sub", "Soft Kick", "Distant Snare", "Orbit Hats"};

inline uint32_t little(const unsigned char* p, int n) {
    uint32_t v = 0;
    for (int i=0; i<n; ++i) v |= uint32_t(p[i]) << (8*i);
    return v;
}

inline std::vector<float> loadWav(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Missing track: " + path.string());
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    auto tag = [&](size_t offset, const char* value) {
        return offset+4 <= bytes.size() && std::equal(bytes.begin()+offset,bytes.begin()+offset+4,value);
    };
    if (!tag(0,"RIFF") || !tag(8,"WAVE")) throw std::runtime_error("Not a RIFF WAV: " + path.string());
    size_t data = 0, length = 0;
    int format=0, channels=0, rate=0, bits=0, align=0;
    for (size_t offset=12; offset+8<=bytes.size();) {
        size_t size=little(&bytes[offset+4],4), start=offset+8;
        if (size > bytes.size()-start) throw std::runtime_error("Truncated WAV: " + path.string());
        if (tag(offset,"fmt ") && size>=16) {
            format=little(&bytes[start],2); channels=little(&bytes[start+2],2);
            rate=little(&bytes[start+4],4); align=little(&bytes[start+12],2); bits=little(&bytes[start+14],2);
        }
        if (tag(offset,"data")) { data=start; length=size; }
        offset=start+size+(size&1);
    }
    if (format!=1 || channels!=2 || rate!=SampleRate || bits!=24 || align!=6 || !length || length%6)
        throw std::runtime_error("Expected stereo 48 kHz / 24-bit PCM: " + path.string());
    std::vector<float> result(length/3);
    for (size_t i=0; i<result.size(); ++i) {
        uint32_t packed=little(&bytes[data+i*3],3);
        int32_t signedValue=packed&0x800000 ? int32_t(packed)-0x1000000 : int32_t(packed);
        result[i]=float(signedValue)/8388608.f;
    }
    return result;
}

struct Mixer {
    std::array<std::vector<float>, TrackCount> tracks;
    std::atomic<uint32_t> enabled{AllTracks};
    std::atomic<bool> playing{true};
    std::atomic<float> volume{.8f};
    std::atomic<uint64_t> position{0}, renderedFrames{0};
    std::array<std::atomic<float>, TrackCount> levels{};
    std::atomic<float> outputRms{0};
    std::array<float, TrackCount> gains{};
    size_t frames=0, cursor=0;
    float transportGain=0, masterGain=.8f;

    void load(const std::filesystem::path& directory) {
        for (int i=0; i<TrackCount; ++i) {
            tracks[i]=loadWav(directory/Files[i]);
            if (!i) frames=tracks[i].size()/2;
            if (tracks[i].size()!=frames*2) throw std::runtime_error("Tracks have different lengths; cannot synchronize");
        }
    }
    void toggle(int index) { enabled.fetch_xor(1u<<index); }
    static float approach(float value, float target) {
        constexpr float step=1.f/(SampleRate*.02f);
        return value<target ? std::min(value+step,target) : std::max(value-step,target);
    }
    // This is the only function called on the audio thread: no allocation,
    // files, locks, UI work, or independent per-track playback clocks.
    void render(float* output, unsigned count) {
        uint32_t mask=enabled.load(std::memory_order_relaxed);
        bool run=playing.load(std::memory_order_relaxed);
        float targetVolume=volume.load(std::memory_order_relaxed);
        std::array<double, TrackCount> energy{};
        double total=0;
        for (unsigned f=0; f<count; ++f) {
            transportGain=approach(transportGain,run?1.f:0.f);
            masterGain=approach(masterGain,targetVolume);
            for (int i=0; i<TrackCount; ++i) gains[i]=approach(gains[i],mask&(1u<<i)?1.f:0.f);
            float l=0,r=0;
            if (frames && transportGain>0) {
                for (int i=0; i<TrackCount; ++i) {
                    float left=tracks[i][cursor*2]*gains[i],right=tracks[i][cursor*2+1]*gains[i];
                    l+=left; r+=right;
                    energy[i]+=(left*left+right*right)*.5;
                }
                cursor=(cursor+1)%frames;
            }
            l=std::clamp(l*masterGain*transportGain,-1.f,1.f);
            r=std::clamp(r*masterGain*transportGain,-1.f,1.f);
            output[f*2]=l; output[f*2+1]=r;
            total+=(l*l+r*r)*.5;
        }
        for (int i=0; i<TrackCount; ++i) levels[i].store(float(std::sqrt(energy[i]/std::max(1u,count))),std::memory_order_relaxed);
        outputRms.store(float(std::sqrt(total/std::max(1u,count))),std::memory_order_relaxed);
        position.store(cursor,std::memory_order_relaxed);
        renderedFrames.fetch_add(count,std::memory_order_relaxed);
    }
};
}
