#include "mixer.hpp"
#include "hotreload.hpp"
#include "campaign.hpp"
#include "progress.hpp"
#include "scene.hpp"
#include "figure.hpp"
#include "raylib.h"
#include "raymath.h"   // must follow raylib.h: it uses raylib's vector types
#include "rlgl.h"
#include <GL/gl.h>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace orbital;
namespace fs=std::filesystem;
static Mixer* audioMixer=nullptr;
static volatile std::sig_atomic_t interrupted=0;
static void audioCallback(void* output,unsigned frames) { audioMixer->render(static_cast<float*>(output),frames); }
static void onSignal(int) {interrupted=1;}
// Colours and role labels live in assets/layers.conf and reload while running.
static LayerConfig layers;
static Campaign campaign;
static std::array<Color,MaxTracks> Colors{};
static int configReloads=0,shaderReloads=0;
static std::string reloadError;
static Progress progress;
static bool sigilVisible=false;
static float sigilX=0,sigilY=0,mouseX=0,mouseY=0;
static bool sigilHot=false;
enum class View { System, Planet, Finale };
static View view=View::System;
static int planetTrack=-1;
static bool beaconOnScreen=false,beaconFound=false;
static float beaconX=0,beaconY=0;
static double zoomLevel=1,viewCenterX=0,viewCenterY=0,beaconWorldX=0,beaconWorldY=0;
static int skitsShowing=0,skitsTotal=0;

// Only the terrain is baked, and per pixel rather than in cells so it stays
// soft when magnified. Everything with a hard edge -- buildings, trees, fields,
// roads, markers, people -- is drawn live at the current zoom, which is the
// only way it stays sharp when you zoom in.
inline constexpr int TerrainWidth = 1280, TerrainHeight = 720;

static Texture2D bakeTerrain(const Scene& scene) {
    Image canvas = GenImageColor(TerrainWidth, TerrainHeight, BLACK);
    auto rgb = [](Rgb c) { return Color{c.r, c.g, c.b, 255}; };
    for (int y = 0; y < TerrainHeight; ++y)
        for (int x = 0; x < TerrainWidth; ++x) {
            float sx = (float(x) + .5f) * SceneWidth / TerrainWidth;
            float sy = (float(y) + .5f) * SceneHeight / TerrainHeight;
            float height = elevationAt(scene.seed, sx, sy), damp = moistureAt(scene.seed, sx, sy);
            Color tone;
            if (height < SeaLevel)             tone = rgb(scene.sea);
            else if (height < ShoreLevel)      tone = rgb(scene.shallow);
            else if (height < ShoreLevel + .03f) tone = rgb(scene.sand);
            else if (height > HighLevel)       tone = rgb(scene.highland);
            else tone = rgb(shade(scene.grass, 1.f, std::clamp((damp - .42f) * 1.5f, 0.f, .55f), scene.forest));
            float grain = .94f + .12f * hashNoise(x, y, scene.seed);
            ImageDrawPixel(&canvas, x, y,
                           Color{(unsigned char)std::clamp(tone.r * grain, 0.f, 255.f),
                                 (unsigned char)std::clamp(tone.g * grain, 0.f, 255.f),
                                 (unsigned char)std::clamp(tone.b * grain, 0.f, 255.f), 255});
        }
    Texture2D texture = LoadTextureFromImage(canvas);
    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    UnloadImage(canvas);
    return texture;
}

static void applyLayers() {
    for(int i=0;i<campaign.count();++i) {
        Rgb c=campaign.tracks[size_t(i)].colour;
        Colors[i]=Color{c.r,c.g,c.b,255};
    }
}
static const char* trackName(int i) { return campaign.tracks[size_t(i)].name.c_str(); }
// A person's clickable box: from the head down past the feet, never smaller
// than a comfortable click. The dev overlay draws this same rectangle, so what
// it shows is what the game actually tests against.
static Rectangle personHitBox(double screenX,double screenY,double pixels) {
    float width=std::max(18.f,float(pixels)*.62f),height=std::max(20.f,float(pixels));
    return {float(screenX)-width*.5f,float(screenY)-height,width,height*1.18f};
}
// Keeps the previous shader when a save does not compile, so a typo in a live
// session cannot take down the window or the audio with it.
static bool reloadShader(Shader& shader,int& resLoc,int& timeLoc,int& energyLoc,const fs::path& path,std::string& note) {
    Shader next=LoadShader(nullptr,path.c_str());
    if(!IsShaderValid(next)||next.id==rlGetShaderIdDefault()) {
        UnloadShader(next);   // raylib guards the default program; this only frees locs
        note="space.fs did not compile - keeping the previous shader";
        return false;
    }
    UnloadShader(shader);shader=next;
    resLoc=GetShaderLocation(shader,"resolution");
    timeLoc=GetShaderLocation(shader,"time");
    energyLoc=GetShaderLocation(shader,"energy");
    note.clear();return true;
}

struct Options {
    fs::path assets=fs::canonical("/proc/self/exe").parent_path().parent_path()/"assets";
    fs::path state=fs::canonical("/proc/self/exe").parent_path().parent_path()/"artifacts"/"state.json";
    fs::path capture;
    fs::path campaignFile=fs::canonical("/proc/self/exe").parent_path().parent_path()/"campaigns"/"orbital-drift.conf";
    fs::path progressFile;   // set once the campaign is known: one save per campaign
    bool windowed=false,check=false,resume=false,gallery=false,dev=false;
    int world=-1;
    double captureAfter=2;
    double seconds=0;
};
static std::string quote(const std::string& s) {
    std::ostringstream out;out<<'"';
    for(unsigned char c:s) {
        if(c=='"'||c=='\\') out<<'\\'<<c;
        else if(c<32) out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c);
        else out<<c;
    }
    out<<'"';return out.str();
}
static void text(Font font,const std::string& value,float x,float y,float size,Color color) {
    DrawTextEx(font,value.c_str(),{x,y},size,.5f,color);
}
static void centered(Font font,const std::string& value,float x,float y,float size,Color color) {
    text(font,value,x-MeasureTextEx(font,value.c_str(),size,.5f).x/2,y,size,color);
}
static void glow(Vector2 p,float radius,Color color,float alpha) {
    for(int j=8;j>=1;--j) DrawCircleV(p,radius*(1+j*.32f),Fade(color,alpha*.028f*(9-j)));
    DrawCircleV(p,radius,Fade(color,alpha));
}
static void writeState(const Options& options,const Mixer& mixer,const std::string& gpu,const std::string& vendor,bool running) {
    fs::create_directories(options.state.parent_path());
    auto temp=options.state;temp+=".tmp";
    std::ofstream out(temp);
    out<<"{\n  \"app\":\"orbital-drift\",\"version\":\"0.18.0\",\"running\":"<<(running?"true":"false")
       <<",\"renderer\":"<<quote(gpu)<<",\"vendor\":"<<quote(vendor)<<",\"hardware_accelerated\":true"
       <<",\"fullscreen\":"<<(IsWindowFullscreen()?"true":"false")
       <<",\"width\":"<<GetScreenWidth()<<",\"height\":"<<GetScreenHeight()<<",\"fps\":"<<GetFPS()
       <<",\"audio_ready\":"<<((running&&IsAudioDeviceReady())?"true":"false")<<",\"playing\":"<<(mixer.playing?"true":"false")
       <<",\"sample_rate\":48000,\"loop_frames\":"<<mixer.frames<<",\"position_frames\":"<<mixer.position.load()
       <<",\"rendered_frames\":"<<mixer.renderedFrames.load()<<",\"output_rms\":"<<mixer.outputRms.load()
       <<",\"volume\":"<<mixer.volume.load()
       <<",\"hot_reload\":{\"config_reloads\":"<<configReloads<<",\"shader_reloads\":"<<shaderReloads
       <<",\"last_error\":"<<quote(reloadError)<<"}"
       <<",\"progress\":{\"unlocked\":"<<progress.unlocked<<",\"frontier\":"<<quote(trackName(progress.frontier()))
       <<",\"next_locked\":"<<(progress.nextLocked()>=0?quote(trackName(progress.nextLocked())):std::string("null"))
       <<",\"sigil_visible\":"<<(sigilVisible?"true":"false")
       <<",\"sigil_x\":"<<int(sigilX)<<",\"sigil_y\":"<<int(sigilY)
       <<",\"complete\":"<<(progress.complete()?"true":"false")
       <<",\"sigil_hot\":"<<(sigilHot?"true":"false")<<",\"mouse_x\":"<<int(mouseX)<<",\"mouse_y\":"<<int(mouseY)
       <<"},\"planet\":{\"view\":"<<quote(view==View::Planet?"planet":(view==View::Finale?"finale":"system"))
       <<",\"track\":"<<(planetTrack>=0?quote(trackName(planetTrack)):std::string("null"))
       <<",\"beacon_on_screen\":"<<(beaconOnScreen?"true":"false")<<",\"beacon_found\":"<<(beaconFound?"true":"false")
       <<",\"beacon_x\":"<<int(beaconX)<<",\"beacon_y\":"<<int(beaconY)<<",\"zoom\":"<<zoomLevel<<",\"skits_showing\":"<<skitsShowing<<",\"skits_total\":"<<skitsTotal
       <<",\"view_x\":"<<viewCenterX<<",\"view_y\":"<<viewCenterY
       <<",\"beacon_world_x\":"<<beaconWorldX<<",\"beacon_world_y\":"<<beaconWorldY<<",\"complete\":"<<(progress.complete()?"true":"false")<<"}"
       <<",\"tracks\":[";
    uint32_t mask=mixer.enabled;
    for(int i=0;i<campaign.count();++i) {
        if(i)out<<',';
        out<<"{\"name\":"<<quote(trackName(i))<<",\"enabled\":"<<((mask&(1u<<i))?"true":"false")
           <<",\"unlocked\":"<<(progress.isUnlocked(i)?"true":"false")<<",\"level\":"<<mixer.levels[i].load()<<'}';
    }
    out<<"]\n}\n";out.close();
    if(!out) throw std::runtime_error("Cannot write status: "+options.state.string());
    fs::rename(temp,options.state);
}

int main(int argc,char** argv) {
    bool windowReady=false,audioReady=false,streamReady=false;
    AudioStream stream{};
    Mixer mixer;
    try {
        Options options;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            auto value=[&](){if(i+1>=argc)throw std::runtime_error("Missing value for "+arg);return std::string(argv[++i]);};
            if(arg=="--windowed")options.windowed=true;
            else if(arg=="--check-assets")options.check=true;
            else if(arg=="--resume")options.resume=true;
            else if(arg=="--gallery")options.gallery=true;
            else if(arg=="--dev")options.dev=true;
            else if(arg=="--campaign")options.campaignFile=fs::absolute(value());
            else if(arg=="--world")options.world=std::stoi(value());
            else if(arg=="--capture-after")options.captureAfter=std::stod(value());
            else if(arg=="--assets")options.assets=fs::absolute(value());
            else if(arg=="--state")options.state=fs::absolute(value());
            else if(arg=="--capture")options.capture=fs::absolute(value());
            else if(arg=="--seconds")options.seconds=std::stod(value());
            else if(arg=="--help") {
                std::cout<<"Orbital Drift 0.18.0\nDefault: fullscreen, silent, one track unsealed.\n--dev adds G: jump straight to the target.\nLeft-click cards/orbs or 1-7 toggle; right-click a sigil to unseal the next track.\nSpace pause; M all off/on; A all on; +/- volume; F11 fullscreen; Esc exit.\n"
                         <<"Options: --windowed --seconds N --capture file.png --state file.json --assets directory --check-assets --resume --gallery --dev --world N --campaign file.conf --capture-after SECONDS\n";return 0;
            } else throw std::runtime_error("Unknown argument: "+arg);
        }
        campaign=loadCampaign(options.campaignFile);
        if(!campaign.note.empty())std::cout<<"[campaign] "<<campaign.note<<std::endl;
        if(campaign.tracks.empty())throw std::runtime_error("Campaign has no tracks: "+options.campaignFile.string());
        std::vector<std::string> stemFiles;
        for(const CampaignTrack& track:campaign.tracks)stemFiles.push_back(track.file);
        mixer.load(options.assets/campaign.stems,stemFiles);
        std::cout<<"[campaign] "<<campaign.title<<": "<<campaign.count()<<" tracks, "
                 <<campaign.tempo<<" BPM, unlock "<<(campaign.rightToLeft?"right to left":"left to right")<<std::endl;
        if(options.check) {
            double energy=0;float peak=0;
            for(size_t sample=0;sample<mixer.frames*2;++sample) {
                float sum=0;for(int t=0;t<mixer.trackCount;++t)sum+=mixer.tracks[size_t(t)][sample];   // only loaded tracks; the rest of the array is empty
                if(!std::isfinite(sum))throw std::runtime_error("Non-finite audio sample");
                peak=std::max(peak,std::abs(sum));energy+=sum*sum;
            }
            if(peak>=1)throw std::runtime_error("Source mix would clip");
            std::cout<<"{\"tracks\":7,\"sample_rate\":48000,\"frames\":"<<mixer.frames<<",\"peak\":"<<peak
                     <<",\"rms\":"<<std::sqrt(energy/(mixer.frames*2))<<",\"valid\":true}\n";
            return 0;
        }
        std::signal(SIGINT,onSignal);std::signal(SIGTERM,onSignal);
        SetTraceLogLevel(LOG_INFO);
        SetConfigFlags(FLAG_VSYNC_HINT|FLAG_MSAA_4X_HINT|FLAG_WINDOW_RESIZABLE);
        InitWindow(1440,900,campaign.title.c_str());windowReady=true;
        SetWindowMinSize(1000,650);
        SetExitKey(KEY_NULL);   // Esc leaves the planet first; quitting is handled by hand
        if(!options.windowed) {
            int monitor=GetCurrentMonitor();
            SetWindowSize(GetMonitorWidth(monitor),GetMonitorHeight(monitor));ToggleFullscreen();
        }
        SetTargetFPS(60);
        auto glString=[](GLenum key){auto value=glGetString(key);return value?std::string(reinterpret_cast<const char*>(value)):std::string("unknown");};
        std::string gpu=glString(GL_RENDERER),vendor=glString(GL_VENDOR),lower=gpu;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return std::tolower(c);});
        if(lower.find("llvmpipe")!=std::string::npos||lower.find("softpipe")!=std::string::npos||lower.find("software")!=std::string::npos||gpu=="unknown")
            throw std::runtime_error("Hardware OpenGL required; detected "+gpu);
        std::cout<<"[graphics] "<<gpu<<" / "<<glString(GL_VERSION)<<std::endl;
        if(!fs::exists(options.assets/"font.ttf")||!fs::exists(options.assets/"space.fs"))
            throw std::runtime_error("Missing graphics assets; run python3 scripts/setup.py");
        // State is saved every run, but a launch starts over unless --resume
        // asks for the previous one; resuming matters later, not yet.
        // One save per campaign: resuming a seven-track run into a three-track
        // campaign used to clamp straight to complete.
        options.progressFile=fs::canonical("/proc/self/exe").parent_path().parent_path()/"artifacts"
                             /("progress-"+options.campaignFile.stem().string()+".json");
        progress=options.resume?loadProgress(options.progressFile,campaign.count(),campaign.rightToLeft):Progress{};
        progress.count=campaign.count();progress.rightToLeft=campaign.rightToLeft;
        saveProgress(options.progressFile,progress);
        mixer.enabled=0;   // the drift begins in silence
        std::cout<<"[progress] "<<progress.unlocked<<" of "<<campaign.count()<<" unlocked; frontier "<<trackName(progress.frontier())
                 <<(options.resume?" (resumed)":" (fresh run)")<<"; starting silent"<<std::endl;
        layers=loadLayerConfig(options.assets/"layers.conf");applyLayers();
        if(!layers.note.empty())std::cout<<"[config] "<<layers.note<<std::endl;
        if(options.gallery||options.world>=0){progress.unlocked=campaign.count();mixer.enabled=mixer.allMask();}
        Font font=LoadFontEx((options.assets/"font.ttf").c_str(),72,nullptr,0);
        if(!IsFontValid(font))throw std::runtime_error("Cannot load UI font");
        GenTextureMipmaps(&font.texture);
        SetTextureFilter(font.texture,TEXTURE_FILTER_TRILINEAR);
        Shader shader=LoadShader(nullptr,(options.assets/"space.fs").c_str());
        if(!IsShaderValid(shader)||shader.id==rlGetShaderIdDefault())throw std::runtime_error("Space shader failed to compile");
        int resLoc=GetShaderLocation(shader,"resolution"),timeLoc=GetShaderLocation(shader,"time"),energyLoc=GetShaderLocation(shader,"energy");
        std::array<Scene,MaxTracks> scenes{};
        std::array<Texture2D,MaxTracks> sheets{};
        std::array<bool,MaxTracks> baked{};
        auto worldSheet=[&](int track)->Texture2D&{
            if(!baked[size_t(track)]) {
                scenes[size_t(track)]=generateScene(track,campaign.tracks[size_t(track)].colour,campaign.count(),campaign.seed);
                sheets[size_t(track)]=bakeTerrain(scenes[size_t(track)]);
                baked[size_t(track)]=true;
                std::cout<<"[world] baked "<<trackName(track)<<std::endl;
            }
            return sheets[size_t(track)];
        };
        Watched shaderWatch{options.assets/"space.fs"},configWatch{options.assets/"layers.conf"};
        shaderWatch.prime();configWatch.prime();
        std::cout<<"[reload] watching space.fs and layers.conf; saves apply live"<<std::endl;
        RenderTexture2D background=LoadRenderTexture(960,540);
        SetTextureFilter(background.texture,TEXTURE_FILTER_BILINEAR);
        InitAudioDevice();audioReady=true;
        if(!IsAudioDeviceReady())throw std::runtime_error("Cannot open speaker output");
        SetAudioStreamBufferSizeDefault(1024);
        stream=LoadAudioStream(SampleRate,32,2);streamReady=IsAudioStreamValid(stream);
        if(!streamReady)throw std::runtime_error("Cannot create stereo audio stream");
        audioMixer=&mixer;SetAudioStreamCallback(stream,audioCallback);PlayAudioStream(stream);
        std::cout<<"[audio] "<<mixer.trackCount<<" synchronized stems, 48000 Hz stereo, "<<mixer.frames<<" frames per loop"<<std::endl;
        std::array<std::array<float,80>,MaxTracks> waves{};
        for(int i=0;i<campaign.count();++i)for(int j=0;j<80;++j) {
            size_t offset=size_t(j)*mixer.frames/80;
            double sum=0;for(size_t k=0;k<512;++k) {float v=mixer.tracks[i][((offset+k)%mixer.frames)*2];sum+=v*v;}
            waves[i][j]=std::min(1.f,float(std::sqrt(sum/512))*9);
        }
        struct Star {float x,y,r,phase;};std::vector<Star> stars;
        auto makeStars=[&]{
            stars.clear();SetRandomSeed(7201);
            for(int i=0;i<layers.starCount;++i)stars.push_back({GetRandomValue(0,10000)/10000.f,GetRandomValue(0,10000)/10000.f,
                GetRandomValue(3,14)/10.f,GetRandomValue(0,100)/10.f});
        };
        makeStars();
        std::array<float,MaxTracks> visibility{},meter{};
        double unlockedAt=-9;std::string unlockedName;
        // The view into a world: where we are looking, and how many pixels one
        // canvas unit covers. Doubles, because deep zoom runs out of float fast.
        double viewX=.5,viewY=.5,viewScale=1;
        std::vector<bool> showing;   // which skits this mix brings out
        double enteredAt=-9;
        float sigilPulse=0;
        if(options.gallery||options.world>=0) {
            view=View::Planet;planetTrack=std::clamp(options.world,0,campaign.count()-1);
            viewScale=std::min(GetScreenWidth()/double(SceneWidth),GetScreenHeight()/double(SceneHeight));
            viewX=SceneWidth*.5;viewY=SceneHeight*.5;
        }
        double started=GetTime(),lastState=-1,toastAt=-9,finaleAt=-9;
        bool finishing=false,panBlocked=false;
        bool captured=false;
        long frame=0;
        bool captureNow=false;
        std::string toast;
        while(!WindowShouldClose()&&!interrupted) {
            double elapsed=GetTime()-started;
            if(options.seconds>0&&elapsed>=options.seconds)break;
            // F2 captures on demand: a screenshot driver needs to choose the
            // moment, not race a fixed delay.
            if(IsKeyPressed(KEY_F2)&&!options.capture.empty())captureNow=true;
            // Data reload only. The mixer, the stems and the playhead are never
            // rebuilt here, so playback continues straight through a reload.
            if(++frame%8==0) {
                if(shaderWatch.changed()) {
                    std::string note;
                    if(reloadShader(shader,resLoc,timeLoc,energyLoc,options.assets/"space.fs",note)) {
                        ++shaderReloads;toast="space.fs reloaded";reloadError.clear();
                    } else {toast=note;reloadError=note;}
                    toastAt=elapsed;std::cout<<"[reload] "<<toast<<std::endl;
                }
                if(configWatch.changed()) {
                    int previousStars=layers.starCount;
                    layers=loadLayerConfig(options.assets/"layers.conf");applyLayers();
                    if(layers.starCount!=previousStars)makeStars();
                    for(int i=0;i<campaign.count();++i)if(baked[i]){UnloadTexture(sheets[i]);baked[i]=false;}
                    ++configReloads;reloadError=layers.note;
                    toast=layers.note.empty()?"layers.conf reloaded":"layers.conf: "+layers.note;
                    toastAt=elapsed;std::cout<<"[reload] "<<toast<<std::endl;
                }
            }
            float w=float(GetScreenWidth()),h=float(GetScreenHeight()),u=std::min(w/1600.f,h/900.f);
            // ---------- finale: the whole mix, drawn ----------
            if(view==View::Finale) {
                float age=float(elapsed-finaleAt),now=float(elapsed);
                if(IsKeyPressed(KEY_ESCAPE)||IsKeyPressed(KEY_BACKSPACE)) {
                    view=View::System;std::cout<<"[campaign] finale closed"<<std::endl;
                }
                float loop=float(mixer.position.load())/float(std::max<size_t>(1,mixer.frames));
                float beat=loop*float(campaign.bars*4);
                float energy=std::min(1.f,mixer.outputRms.load()*layers.energyGain);
                Vector2 middle{w*.5f,h*.5f};
                float span=std::min(w,h);
                BeginDrawing();ClearBackground({3,5,11,255});
                for(auto star:stars)
                    DrawCircleV({star.x*w,star.y*h},star.r*u*(.6f+energy*.5f),
                                Fade({198,220,240,255},.10f+.24f*std::sin(now*.4f+star.phase)));
                // One ring per track, each breathing on its own level. Together
                // they are the mix, seen instead of heard.
                for(int i=campaign.count()-1;i>=0;--i) {
                    float level=std::min(1.f,mixer.levels[i].load()*7.f);
                    float radius=span*(.10f+.041f*float(i))*(1.f+level*.13f);
                    Color c=Colors[i];
                    int points=120;
                    for(int j=0;j<points;++j) {
                        float a=float(j)*2*PI/float(points),b=float(j+1)*2*PI/float(points);
                        float wobbleA=1.f+level*.22f*std::sin(a*float(3+i)+now*(.7f+.11f*float(i)));
                        float wobbleB=1.f+level*.22f*std::sin(b*float(3+i)+now*(.7f+.11f*float(i)));
                        DrawLineEx({middle.x+std::cos(a)*radius*wobbleA,middle.y+std::sin(a)*radius*wobbleA},
                                   {middle.x+std::cos(b)*radius*wobbleB,middle.y+std::sin(b)*radius*wobbleB},
                                   (1.1f+level*2.6f)*u,Fade(c,.20f+level*.68f));
                    }
                    float angle=now*(.16f+.035f*float(i));
                    glow({middle.x+std::cos(angle)*radius,middle.y+std::sin(angle)*radius},
                         (3.5f+level*11.f)*u,c,.35f+level*.55f);
                }
                float pulse=std::exp(-(beat-std::floor(beat))*5.f)*energy;
                glow(middle,(26+energy*22+pulse*16)*u,{150,236,226,255},.26f+energy*.4f);
                DrawCircleLinesV(middle,span*(.072f+pulse*.006f),Fade({170,240,232,255},.35f+energy*.3f));
                float reveal=std::min(1.f,age*.55f);
                centered(font,campaign.title,middle.x,h*.16f,52*u,Fade({236,244,248,255},reveal));
                centered(font,"EVERY SIGNAL IS YOURS",middle.x,h*.16f+62*u,15*u,Fade({150,206,196,255},reveal*.95f));
                centered(font,std::to_string(campaign.count())+" OF "+std::to_string(campaign.count())+" TRACKS PLAYING",
                         middle.x,h-96*u,12*u,Fade({132,166,186,255},reveal*.9f));
                centered(font,"ESC  RETURN TO THE GALAXY",middle.x,h-64*u,11*u,
                         Fade({110,142,162,255},reveal*(.55f+.45f*std::sin(now*1.6f))));
                EndDrawing();
                if(elapsed-lastState>=.2) {writeState(options,mixer,gpu,vendor,true);lastState=elapsed;}
                if(!options.capture.empty()&&!captured&&(captureNow||elapsed>options.captureAfter)) {
                    fs::create_directories(options.capture.parent_path());
                    Image shot=LoadImageFromScreen();
                    captured=ExportImage(shot,options.capture.c_str());UnloadImage(shot);
                }
                continue;
            }
            // ---------- world view: pan and zoom the planet's background image ----------
            if(view==View::Planet) {
                Scene& scene=scenes[size_t(planetTrack)];
                Texture2D& sheet=worldSheet(planetTrack);
                float step=std::min(GetFrameTime(),.1f);
                bool leaving=IsKeyPressed(KEY_ESCAPE)||IsKeyPressed(KEY_BACKSPACE);
                if(options.gallery)
                    for(int i=0;i<campaign.count();++i)
                        if(IsKeyPressed(KEY_ONE+i)&&i!=planetTrack) {
                            planetTrack=i;worldSheet(i);
                            viewScale=std::min(w/float(SceneWidth),h/float(SceneHeight));
                            viewX=SceneWidth*.5;viewY=SceneHeight*.5;
                            std::cout<<"[gallery] "<<trackName(i)<<std::endl;
                        }
                Vector2 pointer=GetMousePosition();
                double halfW=w*.5,halfH=h*.5;
                double fit=std::min(w/double(SceneWidth),h/double(SceneHeight));
                auto screenX=[&](double ix){return (ix-viewX)*viewScale+halfW;};
                auto screenY=[&](double iy){return (iy-viewY)*viewScale+halfH;};
                if(IsMouseButtonReleased(MOUSE_BUTTON_LEFT))panBlocked=false;
                if(IsMouseButtonDown(MOUSE_BUTTON_LEFT)&&!panBlocked) {
                    Vector2 drag=GetMouseDelta();
                    viewX-=drag.x/viewScale;viewY-=drag.y/viewScale;
                }
                double pan=step*900.0/viewScale;
                if(IsKeyDown(KEY_LEFT))viewX-=pan;
                if(IsKeyDown(KEY_RIGHT))viewX+=pan;
                if(IsKeyDown(KEY_UP))viewY-=pan;
                if(IsKeyDown(KEY_DOWN))viewY+=pan;
                // Zoom toward the cursor, bounded: fit the whole image at one
                // end, close enough to read a marker at the other.
                auto zoomAt=[&](double factor,double sx,double sy){
                    double ix=(sx-halfW)/viewScale+viewX,iy=(sy-halfH)/viewScale+viewY;
                    viewScale=std::clamp(viewScale*factor,fit,fit*9.0);
                    viewX=ix-(sx-halfW)/viewScale;viewY=iy-(sy-halfH)/viewScale;
                };
                if(float wheel=GetMouseWheelMove();wheel!=0)zoomAt(std::pow(1.22,wheel),pointer.x,pointer.y);
                if(IsKeyDown(KEY_W))zoomAt(std::pow(2.4,step),pointer.x,pointer.y);
                if(IsKeyDown(KEY_S))zoomAt(std::pow(2.4,-step),pointer.x,pointer.y);
                // Keep the image in frame rather than letting it drift into the void.
                double marginX=std::max(0.0,(w/viewScale)*.5-SceneWidth*.5);
                double marginY=std::max(0.0,(h/viewScale)*.5-SceneHeight*.5);
                viewX=std::clamp(viewX,-marginX,SceneWidth+marginX);
                viewY=std::clamp(viewY,-marginY,SceneHeight+marginY);
                if(options.dev&&IsKeyPressed(KEY_G)) {
                    viewX=scene.people[size_t(scene.target)].x;
                    viewY=scene.people[size_t(scene.target)].y-scene.people[size_t(scene.target)].height*.5;
                    viewScale=fit*9.0;
                    toast="dev: jumped to the target";toastAt=elapsed;reloadError.clear();
                    std::cout<<"[dev] jumped to target"<<std::endl;
                }
                zoomLevel=viewScale/fit;viewCenterX=viewX;viewCenterY=viewY;

                uint32_t playing=mixer.enabled;
                // Which vignettes this mix brings out. Resolved once per frame
                // rather than per person: a world has hundreds of each.
                showing.assign(scene.skits.size(),false);
                skitsShowing=0;skitsTotal=int(scene.skits.size());
                for(size_t i=0;i<scene.skits.size();++i)
                    if(scene.skits[i].showing(playing)) {showing[i]=true;++skitsShowing;}
                const PersonSpot& target=scene.people[size_t(scene.target)];
                beaconWorldX=target.x;beaconWorldY=target.y;
                double beaconScreenX=screenX(target.x),beaconScreenY=screenY(target.y);
                double beaconPixels=target.height*viewScale;
                bool targetShowing=showing[size_t(target.skit)];
                beaconOnScreen=targetShowing&&beaconScreenX>0&&beaconScreenY>0&&beaconScreenX<w&&beaconScreenY<h;
                beaconX=float(beaconScreenX);beaconY=float(beaconScreenY);beaconFound=scene.found;
                // An invisible box around the target, drawn feet-up and never
                // smaller than a comfortable click.
                Rectangle hitBox=personHitBox(beaconScreenX,beaconScreenY,beaconPixels);
                bool overBeacon=beaconOnScreen&&CheckCollisionPointRec(pointer,hitBox);
                bool guessed=(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)||IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
                             &&!overBeacon&&!scene.found;
                if(overBeacon&&!scene.found
                   &&(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)||IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
                    scene.found=beaconFound=true;
                    // The claim click must not also drag the map. Blocked until
                    // the button comes back up, not for the life of the world:
                    // a found world still has to be explorable.
                    panBlocked=true;
                    bool wasComplete=progress.complete();
                    toast="Found them";toastAt=elapsed;reloadError.clear();
                    if(!progress.complete()&&planetTrack==progress.frontier()) {
                        unlockedName=trackName(progress.nextLocked());
                        progress.advance();saveProgress(options.progressFile,progress);
                        // The new track starts playing, which lights its layer
                        // in every world including this one.
                        mixer.enabled|=1u<<progress.frontier();unlockedAt=elapsed;
                        std::cout<<"[unlock] "<<unlockedName<<" ("<<progress.unlocked<<'/'<<campaign.count()<<')'<<std::endl;
                    }
                    // Only the run that *completes* the campaign opens the
                    // finale. Finding someone in the bonus world afterwards must
                    // not reopen it.
                    finishing=!wasComplete&&progress.complete();
                    leaving=true;
                }
                SetMouseCursor(overBeacon?MOUSE_CURSOR_POINTING_HAND:MOUSE_CURSOR_DEFAULT);

                Color edge{6,10,17,255};
                BeginDrawing();ClearBackground(edge);
                Rectangle source{0,0,float(TerrainWidth),float(TerrainHeight)};
                Rectangle dest{float(screenX(0)),float(screenY(0)),
                               float(SceneWidth*viewScale),float(SceneHeight*viewScale)};
                DrawTexturePro(sheet,source,dest,{0,0},0,WHITE);
                // Everything with a hard edge is drawn at the current zoom so it
                // stays sharp however far in you go.
                auto tint=[&](Rgb c,float alpha=1.f){return Fade(Color{c.r,c.g,c.b,255},alpha);};
                auto onScreen=[&](double x,double y,double pad){
                    double sx=screenX(x),sy=screenY(y);
                    return sx>-pad&&sy>-pad&&sx<w+pad&&sy<h+pad;
                };
                float z=float(viewScale);
                for(const Field& field:scene.fields) {
                    if(!onScreen(field.x,field.y,(field.w+field.h)*viewScale))continue;
                    Rgb shadeOf=scene.palette[field.tone];
                    float degrees=field.spin*RAD2DEG;
                    Vector2 at{float(screenX(field.x)),float(screenY(field.y))};
                    DrawRectanglePro({at.x,at.y,field.w*z,field.h*z},{field.w*z*.5f,field.h*z*.5f},
                                     degrees,tint(shadeOf,.42f));
                    for(int i=1;i<field.furrows;++i) {
                        float t=float(i)/float(field.furrows);
                        DrawRectanglePro({at.x,at.y-field.h*z*.5f+field.h*z*t,field.w*z,std::max(1.f,1.6f*z)},
                                         {field.w*z*.5f,0},degrees,tint(shade(shadeOf,.7f,.2f,{20,26,20}),.4f));
                    }
                }
                for(const Patch& patch:scene.woods)
                    for(const Blob& blob:patch.blobs) {
                        float radius=blob.r*z;
                        if(!onScreen(blob.x,blob.y,radius+12))continue;
                        Rgb tone=shade(scene.forest,.78f+.16f*float(blob.tone),.06f,{16,30,22});
                        Vector2 at{float(screenX(blob.x)),float(screenY(blob.y))};
                        // A small tree is a few pixels wide: its shadow is invisible
                        // and a 36-sided circle is wasted. Both cost real frames.
                        if(radius<6.f) { DrawPoly(at,7,std::max(1.f,radius),0,tint(tone)); continue; }
                        DrawCircleV({at.x,at.y+radius*.22f},radius,tint(shade(tone,.55f,.2f,{10,18,14}),.5f));
                        DrawCircleV(at,radius,tint(tone));
                    }
                for(const Road& road:scene.roads)
                    for(size_t i=1;i<road.points.size();++i) {
                        Vector2 a{float(screenX(road.points[i-1].first)),float(screenY(road.points[i-1].second))};
                        Vector2 b{float(screenX(road.points[i].first)),float(screenY(road.points[i].second))};
                        DrawLineEx(a,b,std::max(1.f,11*z),tint(shade(scene.sand,.62f,.3f,{60,52,40}),.85f));
                        DrawLineEx(a,b,std::max(1.f,6.5f*z),tint(shade(scene.sand,1.08f,.1f,{240,230,205}),.95f));
                    }
                for(const Town& town:scene.towns) {
                    if(!onScreen(town.x,town.y,town.radius*viewScale*1.6+40))continue;
                    DrawCircleV({float(screenX(town.x)),float(screenY(town.y))},town.radius*1.25f*z,
                                tint(shade(scene.sand,.9f,.35f,{170,160,140}),.20f));
                    for(const Building& building:town.buildings) {
                        Vector2 at{float(screenX(building.x)),float(screenY(building.y))};
                        float bw=building.w*z,bh=building.h*z,degrees=building.spin*RAD2DEG;
                        DrawRectanglePro({at.x+3*z,at.y+4*z,bw,bh},{bw*.5f,bh*.5f},degrees,tint({12,16,22},.28f));
                        DrawRectanglePro({at.x,at.y,bw,bh},{bw*.5f,bh*.5f},degrees,
                                         tint(shade(scene.palette[building.roof],1.f,.34f,{238,236,228})));
                        DrawRectanglePro({at.x,at.y,bw,bh*.55f},{bw*.5f,bh*.5f},degrees,
                                         tint(scene.palette[building.roof]));
                    }
                }
                for(const Marker& marker:scene.markers) {
                    if(!onScreen(marker.x,marker.y,marker.size*viewScale+24))continue;
                    Vector2 at{float(screenX(marker.x)),float(screenY(marker.y))};
                    DrawCircleV({at.x,at.y+marker.size*z*.34f},marker.size*z*.66f,tint({10,14,20},.30f));
                    DrawPoly(at,3,marker.size*z,-90,tint(scene.palette[marker.palette]));
                    DrawPolyLines(at,3,marker.size*z,-90,tint(shade(scene.palette[marker.palette],.5f,.25f,{12,18,26}),.8f));
                }
                int drawnPeople=0,layersOn=0;
                for(int i=0;i<campaign.count();++i)layersOn+=(mixer.enabled>>i)&1u;
                for(const PersonSpot& spot:scene.people) {
                    // A vignette exists only while its configuration is met.
                    if(!showing[size_t(spot.skit)])continue;
                    float px=float(spot.height*viewScale);
                    if(px<1.1f)continue;
                    double sx=screenX(spot.x),sy=screenY(spot.y);
                    if(sx<-px*2||sy<-px*2||sx>w+px*2||sy>h+px*2)continue;
                    ++drawnPeople;
                    if(px<9.f) {    // below this a body is unreadable anyway: a mark will do
                        DrawRectangleRec({float(sx-px*.22),float(sy-px*.75),
                                          std::max(1.f,px*.44f),std::max(1.f,px*.8f)},
                                         toColor(ClothTones[spot.figure.shirt]));
                        if(options.dev) {
                            // DrawRectangleLines batches as lines; the Ex form is
                            // four quads per box and costs real frames at a
                            // thousand people.
                            Rectangle box=personHitBox(sx,sy,px);
                            DrawRectangleLines(int(box.x),int(box.y),int(box.width),int(box.height),
                                               Fade(Colors[spot.layer],.55f));
                        }
                        continue;
                    }
                    drawFigure(spot.figure,{float(sx),float(sy)},px);
                    // A click that lands on the wrong person must say so. With no
                    // answer at all, a near miss is indistinguishable from a
                    // broken click.
                    if(guessed&&CheckCollisionPointRec(pointer,personHitBox(sx,sy,px))) {
                        toast="Not them";toastAt=elapsed;reloadError.clear();guessed=false;
                    }
                    // Dev: every person boxed in the colour of the track whose
                    // layer they belong to, matching that track's card.
                    if(options.dev) {
                        Rectangle box=personHitBox(sx,sy,px);
                        DrawRectangleLines(int(box.x),int(box.y),int(box.width),int(box.height),
                                           Fade(Colors[spot.layer],.7f));
                    }
                }

                float pad=38*u;
                Color want=toColor(scene.palette[0]);   // the track's own colour, used for the find box trim
                if(scene.found) {
                    float ring=float(std::max(20.0,beaconPixels*1.8))+4*std::sin(float(elapsed)*3);
                    DrawCircleLinesV({float(beaconScreenX),float(beaconScreenY)},ring,Fade({208,244,228,255},.9f));
                    centered(font,"FOUND",float(beaconScreenX),float(beaconScreenY)+ring+7*u,11*u,{208,244,228,255});
                } else if(overBeacon) {
                    DrawCircleLinesV({float(beaconScreenX),float(beaconScreenY)},
                                     float(std::max(16.0,beaconPixels*1.6)),Fade(want,.55f));
                }
                if(options.dev&&!scene.found&&beaconOnScreen) {
                    DrawRectangleLinesEx(hitBox,std::max(2.f,u*2.2f),{255,255,255,255});
                    DrawRectangleLinesEx(hitBox,std::max(1.f,u),Fade(Color{255,90,90,255},.95f));
                    centered(font,"TARGET",hitBox.x+hitBox.width*.5f,hitBox.y-13*u,10*u,{255,150,150,255});
                }
                DrawRectangleRounded({pad-16*u,pad-24*u,470*u,98*u},.08f,8,Fade(edge,.94f));
                text(font,options.gallery?"GALLERY":(options.dev?"WORLD  /  DEV":"WORLD"),pad,pad-10*u,12*u,
                     options.dev?Color{226,142,142,255}:Color{118,150,172,255});
                text(font,trackName(planetTrack),pad,pad+8*u,34*u,{231,238,244,255});
                const char* lead = scene.found ? "This world has given up its secret"
                    : options.gallery ? "1-7 switch worlds. Drag to pan, wheel to zoom."
                    : targetShowing ? "Someone down there is dressed like this. Zoom in and look."
                                    : "This world is quiet. Wake its own signal to bring them out.";
                text(font,lead,pad,pad+50*u,13*u,
                     targetShowing||scene.found?Color{136,162,182,255}:Color{196,158,126,255});
                // The find box shows the target at the size it reaches at full
                // zoom, so what you are hunting for is exactly what you will see.
                float portrait=std::max(96*u,float(target.height*fit*9.0)*1.45f);
                float cardW=std::max(188*u,portrait*1.5f),cardH=portrait+74*u;
                float cardX=w-pad-cardW,cardTop=pad-14*u;
                DrawRectangleRounded({cardX,cardTop,cardW,cardH},.1f,8,{10,17,27,236});
                DrawRectangleRoundedLinesEx({cardX,cardTop,cardW,cardH},.1f,8,u,Fade(want,.45f));
                text(font,"FIND",cardX+14*u,cardTop+11*u,11*u,{132,160,180,255});
                DrawRectangleRounded({cardX+cardW*.5f-portrait*.42f,cardTop+30*u,portrait*.84f,portrait+6*u},
                                     .08f,6,Fade(Color{scene.grass.r,scene.grass.g,scene.grass.b,255},.30f));
                drawFigure(target.figure,{cardX+cardW*.5f,cardTop+30*u+portrait},portrait);
                centered(font,scene.found?"FOUND":(targetShowing?"THIS PERSON":"NOT HERE YET"),
                         cardX+cardW*.5f,cardTop+cardH-24*u,11*u,
                         scene.found?Color{170,232,200,255}
                                   :(targetShowing?Fade(want,.9f):Color{196,158,126,255}));
                float mapSize=118*u,mapX=w-pad-mapSize,mapY=h-pad-mapSize;
                DrawRectangleRec({mapX,mapY,mapSize,mapSize*float(SceneHeight)/SceneWidth},Fade({8,13,21,255},.86f));
                DrawRectangleLinesEx({mapX,mapY,mapSize,mapSize*float(SceneHeight)/SceneWidth},u,Fade(want,.3f));
                float boxW=std::min(mapSize,float(w/viewScale/SceneWidth*mapSize));
                float boxH=std::min(mapSize*float(SceneHeight)/SceneWidth,
                                    float(h/viewScale/SceneHeight*mapSize*float(SceneHeight)/SceneWidth));
                float boxX=mapX+std::clamp(float(viewX/SceneWidth*mapSize)-boxW*.5f,0.f,mapSize-boxW);
                float boxY=mapY+std::clamp(float(viewY/SceneHeight*mapSize*float(SceneHeight)/SceneWidth)-boxH*.5f,
                                           0.f,mapSize*float(SceneHeight)/SceneWidth-boxH);
                DrawRectangleLinesEx({boxX,boxY,boxW,boxH},std::max(1.f,u),Fade({214,240,232,255},.85f));
                std::ostringstream zoomText;
                zoomText<<"ZOOM  x"<<std::fixed<<std::setprecision(1)<<zoomLevel
                        <<"    "<<layersOn<<"/"<<campaign.count()<<" TRACKS    "
                        <<skitsShowing<<"/"<<scene.skits.size()<<" SKITS    "
                        <<drawnPeople<<" IN VIEW";
                if(options.dev)zoomText<<"    BOXES = LAYER COLOUR";
                text(font,zoomText.str(),pad,h-pad-4*u,11*u,{112,142,162,255});
                if(elapsed-toastAt<2.6) {
                    float age=float(elapsed-toastAt),alpha=std::min(1.f,(2.6f-age)*2.2f);
                    centered(font,toast,w*.5f,pad-8*u,13*u,
                             Fade(reloadError.empty()?Color{124,235,210,255}:Color{240,172,138,255},alpha));
                }
                DrawRectangle(0,int(h-46*u),int(w),int(46*u),Fade(edge,.8f));
                centered(font,options.gallery
                         ?"1-7  SWITCH WORLD     DRAG  PAN     WHEEL / W S  ZOOM     ESC  BACK"
                         :(options.dev?"DRAG  PAN     WHEEL / W S  ZOOM     G  JUMP TO TARGET     CLICK THEM  TO CLAIM     ESC  BACK"
                                      :"DRAG  PAN     WHEEL / W S  ZOOM     CLICK THEM  TO CLAIM     ESC  BACK"),
                         w*.5f,h-30*u,10*u,{128,158,176,255});
                EndDrawing();
                if(elapsed-lastState>=.2) {writeState(options,mixer,gpu,vendor,true);lastState=elapsed;}
                if(!options.capture.empty()&&!captured&&(captureNow||elapsed>options.captureAfter)) {
                    fs::create_directories(options.capture.parent_path());
                    Image shot=LoadImageFromScreen();
                    captured=ExportImage(shot,options.capture.c_str());UnloadImage(shot);
                }
                if(leaving) {
                    view=finishing?View::Finale:View::System;
                    planetTrack=-1;beaconOnScreen=false;
                    if(finishing){finaleAt=elapsed;std::cout<<"[campaign] complete"<<std::endl;}
                    else std::cout<<"[world] left"<<std::endl;
                    finishing=false;
                }
                continue;
            }
            float margin=52*u, gap=12*u,cardWidth=(w-margin*2-gap*6)/7,cardY=h-169*u,cardH=112*u;
            Vector2 center{w*.5f,h*.435f};float radius=std::min(w*.31f,h*.34f);
            Vector2 mouse=GetMousePosition();
            uint32_t mask=mixer.enabled;
            float dt=std::min(GetFrameTime(),.1f),motion=float(elapsed);
            float loopPos=float(mixer.position.load())/float(mixer.frames);
            float beat=loopPos*float(campaign.bars*4);
            std::array<Vector2,MaxTracks> nodes{};
            std::array<Rectangle,MaxTracks> cards{};
            int hovered=-1,hoveredNode=-1;
            for(int i=0;i<campaign.count();++i) {
                float orbit=radius*(layers.orbitBase+i*layers.orbitStep);
                float angle=motion*(.055f+i*.009f)+float(i)*2.39996f;
                nodes[i]={center.x+std::cos(angle)*orbit,center.y+std::sin(angle)*orbit*.56f};
                cards[i]={margin+i*(cardWidth+gap),cardY,cardWidth,cardH};
                if(CheckCollisionPointRec(mouse,cards[i]))hovered=i;              // the card is the switch
                if(CheckCollisionPointCircle(mouse,nodes[i],22*u))hoveredNode=i;   // the node is the world
                if(IsKeyPressed(KEY_ONE+i)||(hovered==i&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
                    if(!progress.isUnlocked(i)) {
                        toast="That signal is still sealed";toastAt=elapsed;reloadError.clear();
                    } else {
                        mixer.toggle(i);std::cout<<"[track] "<<(i+1)<<' '<<trackName(i)<<' '<<((mixer.enabled&(1u<<i))?"on":"off")<<std::endl;
                    }
                }
            }
            auto enterPlanet=[&](int track){
                view=View::Planet;planetTrack=track;panBlocked=false;
                viewScale=std::min(w/double(SceneWidth),h/double(SceneHeight));
                viewX=SceneWidth*.5;viewY=SceneHeight*.5;
                enteredAt=elapsed;(void)enteredAt;
                std::cout<<"[world] entered "<<trackName(track)<<std::endl;
            };
            // Read the mixer live, not the mask snapshotted at the top of the
            // frame: a track woken earlier this frame is already on.
            // An unlocked world can always be visited, playing or not; you
            // simply see the crowds of whichever tracks are sounding.
            if(hoveredNode>=0&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if(!progress.isUnlocked(hoveredNode)) {toast="That signal is still sealed";toastAt=elapsed;reloadError.clear();}
                else enterPlanet(hoveredNode);
            }
            // Z drops into the frontier world without hunting a moving node.
            if(IsKeyPressed(KEY_Z)&&progress.isUnlocked(progress.frontier()))
                enterPlanet(progress.frontier());
            if(IsKeyPressed(KEY_V)&&progress.complete()) {view=View::Finale;finaleAt=elapsed;}
            if(IsKeyPressed(KEY_ESCAPE))break;
            Rectangle pauseButton{margin, h-39*u,82*u,26*u};
            Rectangle allButton{margin+97*u,h-39*u,82*u,26*u};
            Rectangle silenceButton{margin+194*u,h-39*u,82*u,26*u};
            Rectangle volumeBar{w-margin-130*u,h-28*u,130*u,4*u};
            Rectangle volumeHit{volumeBar.x,volumeBar.y-12*u,volumeBar.width,28*u};
            if(IsKeyPressed(KEY_SPACE)||(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(mouse,pauseButton)))mixer.playing=!mixer.playing;
            if(IsKeyPressed(KEY_A)||(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(mouse,allButton)))mixer.enabled=progress.mask();
            if(IsKeyPressed(KEY_M))mixer.enabled=mixer.enabled?0:progress.mask();
            if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(mouse,silenceButton))mixer.enabled=0;
            if(IsKeyPressed(KEY_EQUAL)||IsKeyPressed(KEY_KP_ADD))mixer.volume=std::min(1.f,mixer.volume+.05f);
            if(IsKeyPressed(KEY_MINUS)||IsKeyPressed(KEY_KP_SUBTRACT))mixer.volume=std::max(0.f,mixer.volume-.05f);
            if(IsMouseButtonDown(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(mouse,volumeHit))mixer.volume=std::clamp((mouse.x-volumeBar.x)/volumeBar.width,0.f,1.f);
            if(IsKeyPressed(KEY_F11)) {
                if(IsWindowFullscreen()){ToggleFullscreen();SetWindowSize(1440,900);}
                else{int mon=GetCurrentMonitor();SetWindowSize(GetMonitorWidth(mon),GetMonitorHeight(mon));ToggleFullscreen();}
            }
            mask=mixer.enabled;
            // The sigil marks which world still holds a key. It is a doorway:
            // the unseal itself is earned down there, by finding the beacon.
            int frontier=progress.frontier();
            float sigilOrbit=radius*(layers.orbitBase+frontier*layers.orbitStep);
            Vector2 sigil{center.x-sigilOrbit,center.y};
            sigilX=sigil.x;sigilY=sigil.y;mouseX=mouse.x;mouseY=mouse.y;
            bool frontierOn=(mask>>frontier)&1u;
            // The layer is exposed whenever its track is on, and the sigil is
            // part of that layer. No timing window: switch the track on, and
            // the clue is there to be found.
            sigilVisible=!progress.complete()&&frontierOn;
            sigilPulse+=((sigilVisible?1.f:0.f)-sigilPulse)*(1-std::exp(-dt*9));
            bool onSigil=sigilVisible&&CheckCollisionPointCircle(mouse,sigil,26*u);
            sigilHot=onSigil;
            if(onSigil&&(IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)||IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
                enterPlanet(frontier);
            bool hot=onSigil||hoveredNode>=0||hovered>=0||CheckCollisionPointRec(mouse,pauseButton)||CheckCollisionPointRec(mouse,allButton)||CheckCollisionPointRec(mouse,silenceButton)||CheckCollisionPointRec(mouse,volumeHit);
            SetMouseCursor(hot?MOUSE_CURSOR_POINTING_HAND:MOUSE_CURSOR_DEFAULT);
            for(int i=0;i<campaign.count();++i) {
                visibility[i]+=(float(bool(mask&(1u<<i)))-visibility[i])*(1-std::exp(-dt*7));
                meter[i]+=(mixer.levels[i].load()*6-meter[i])*(1-std::exp(-dt*13));
            }
            float energy=std::min(1.f,mixer.outputRms.load()*layers.energyGain);
            Vector2 resolution{float(background.texture.width),float(background.texture.height)};
            SetShaderValue(shader,resLoc,&resolution,SHADER_UNIFORM_VEC2);
            SetShaderValue(shader,timeLoc,&motion,SHADER_UNIFORM_FLOAT);
            SetShaderValue(shader,energyLoc,&energy,SHADER_UNIFORM_FLOAT);
            BeginTextureMode(background);ClearBackground(BLACK);BeginShaderMode(shader);
            DrawRectangle(0,0,background.texture.width,background.texture.height,WHITE);
            EndShaderMode();EndTextureMode();
            BeginDrawing();ClearBackground({4,8,17,255});
            DrawTexturePro(background.texture,{0,0,resolution.x,-resolution.y},{0,0,w,h},{0,0},0,WHITE);
            for(auto star:stars) {
                float alpha=.18f+.22f*(.5f+.5f*std::sin(motion*.25f+star.phase));
                DrawCircleV({star.x*w+(mouse.x/w-.5f)*star.r*7,star.y*h+(mouse.y/h-.5f)*star.r*7},star.r*u,Fade({187,213,231,255},alpha));
            }
            text(font,"CAMPAIGN",margin,30*u,13*u,{123,157,177,255});
            text(font,campaign.title,margin,53*u,46*u,{232,239,244,255});
            text(font,campaign.subtitle,margin,109*u,16*u,{144,161,183,255});
            int active=0;for(int i=0;i<7;++i)if(mask&(1u<<i))++active;
            text(font,std::to_string(campaign.tempo)+" BPM   /   "+campaign.musicalKey,w-margin-208*u,40*u,16*u,{188,207,218,255});
            text(font,std::to_string(active)+" OF "+std::to_string(progress.unlocked)+" SIGNALS ACTIVE",w-margin-208*u,69*u,12*u,{113,154,166,255});
            if(!progress.complete())
                text(font,std::to_string(campaign.count()-progress.unlocked)+" STILL SEALED",w-margin-208*u,88*u,11*u,{126,110,150,255});
            for(int i=0;i<4;++i)DrawCircleV({w-margin-196*u+i*22*u,106*u},3*u,Fade({124,235,210,255},int(beat)%4==i&&mixer.playing?.95f:.18f));
            // Every track has a visible orbit, even while silent, so re-entry is discoverable.
            for(int i=0;i<campaign.count();++i) {
                float orbit=radius*(layers.orbitBase+i*layers.orbitStep);
                Color c=Colors[i];
                if(!progress.isUnlocked(i)) {
                    for(int j=0;j<160;j+=2) {
                        float a=j*2*PI/160,b=(j+1)*2*PI/160;
                        DrawLineEx({center.x+std::cos(a)*orbit,center.y+std::sin(a)*orbit*.56f},
                                   {center.x+std::cos(b)*orbit,center.y+std::sin(b)*orbit*.56f},u,Fade({70,88,104,255},.16f));
                    }
                    continue;
                }
                for(int j=0;j<160;++j) {
                    float a=j*2*PI/160,b=(j+1)*2*PI/160;
                    DrawLineEx({center.x+std::cos(a)*orbit,center.y+std::sin(a)*orbit*.56f},
                               {center.x+std::cos(b)*orbit,center.y+std::sin(b)*orbit*.56f},u,Fade(c,.055f+visibility[i]*.11f));
                }
                float strength=.2f+visibility[i]*.8f;
                DrawLineEx(center,nodes[i],u,Fade(c,(.015f+meter[i]*.045f)*visibility[i]));
                glow(nodes[i],(6+meter[i]*13)*u*layers.glowScale,c,strength*.8f);
                DrawCircleLinesV(nodes[i],(15+(hoveredNode==i?4:0))*u,Fade(c,.2f+visibility[i]*.5f));
                centered(font,std::to_string(i+1),nodes[i].x,nodes[i].y-5*u,10*u,{235,245,255,255});
                if(hoveredNode==i) {
                    centered(font,trackName(i),nodes[i].x,nodes[i].y+27*u,14*u,Colors[i]);
                    centered(font,"CLICK TO DESCEND",nodes[i].x,nodes[i].y+44*u,10*u,Fade(c,(mask&(1u<<i))?.9f:.6f));
                }
            }
            if(sigilPulse>.012f) {
                Color sc=Colors[frontier];float a=sigilPulse,rr=(13+2.5f*std::sin(motion*4))*u;
                glow(sigil,rr*.5f,sc,a*.45f);
                DrawCircleLinesV(sigil,rr,Fade(sc,a*.85f));
                DrawCircleLinesV(sigil,rr*(1.5f+.25f*std::sin(motion*2.2f)),Fade(sc,a*.28f));
                DrawLineEx({sigil.x+6*u,sigil.y-7*u},{sigil.x-3*u,sigil.y},2*u,Fade(sc,a));
                DrawLineEx({sigil.x-3*u,sigil.y},{sigil.x+6*u,sigil.y+7*u},2*u,Fade(sc,a));
                centered(font,onSigil?"CLICK TO DESCEND":"A KEY LIES BELOW",sigil.x,sigil.y+rr+7*u,9*u,Fade(sc,a*.85f));
            }
            float pulse=std::exp(-(beat-std::floor(beat))*5)*energy;
            glow(center,(30+energy*7+pulse*3)*u,{100,222,226,255},.22f+energy*.25f);
            DrawCircleLinesV(center,49*u,Fade({140,222,234,255},.18f+energy*.25f));
            DrawCircleLinesV(center,55*u,Fade({140,222,234,255},.1f));
            centered(font,"OD",center.x,center.y-12*u,24*u,{222,250,249,255});
            if(elapsed-unlockedAt<3.4) {
                float age=float(elapsed-unlockedAt),a=std::min(1.f,(3.4f-age)*1.6f);
                centered(font,unlockedName+" ANSWERS",center.x,center.y+radius*.69f,14*u,Fade({198,236,225,255},a));
                centered(font,"a new signal joins the drift",center.x,center.y+radius*.69f+24*u,13*u,Fade({134,180,178,255},a*.8f));
            } else {
                centered(font,active?"THE SIGNAL IS YOURS":"SPACE TO BREATHE",center.x,center.y+radius*.69f,12*u,{143,173,185,255});
                centered(font,progress.complete()?"Every signal is yours":(active?"A sigil marks the world that holds the next key":"Wake a signal to see its layer"),
                         center.x,center.y+radius*.69f+24*u,14*u,{102,129,149,255});
            }
            float rulerY=cardY-39*u;
            DrawLineEx({margin,rulerY},{w-margin,rulerY},u,{38,54,69,255});
            DrawLineEx({margin,rulerY},{margin+(w-2*margin)*loopPos,rulerY},2*u,{117,193,188,255});
            for(int i=0;i<=campaign.bars;++i) {
                float x=margin+(w-margin*2)*float(i)/float(campaign.bars);
                DrawLineEx({x,rulerY-3*u},{x,rulerY+(i%4?3:6)*u},u,{69,91,103,255});
            }
            text(font,std::to_string(campaign.bars)+"-BAR ORBIT",margin,rulerY-23*u,11*u,{113,145,164,255});
            std::string bar="BAR "+std::to_string(std::min(campaign.bars,int(loopPos*campaign.bars)+1))+" / "+std::to_string(campaign.bars);
            text(font,bar,w-margin-89*u,rulerY-23*u,11*u,{144,178,191,255});
            for(int i=0;i<campaign.count();++i) {
                Rectangle r=cards[i];bool on=mask&(1u<<i);Color c=Colors[i];float v=visibility[i];
                if(!progress.isUnlocked(i)) {
                    DrawRectangleRounded(r,.12f,8,{9,15,23,235});
                    DrawRectangleRoundedLinesEx(r,.12f,8,u,{33,45,58,255});
                    text(font,std::to_string(i+1),r.x+14*u,r.y+12*u,12*u,{60,76,92,255});
                    text(font,"SEALED",r.x+r.width-54*u,r.y+12*u,11*u,{72,90,106,255});
                    centered(font,"* * * * *",r.x+r.width*.5f,r.y+54*u,15*u,{47,62,77,255});
                    continue;
                }
                DrawRectangleRounded(r,.12f,8,{12,21,34,240});
                DrawRectangleRounded(r,.12f,8,Fade(c,(hovered==i?.12f:.045f)*v));
                DrawRectangleRoundedLinesEx(r,.12f,8,u,Fade(c,(hovered==i?.7f:.25f)*v+.09f));
                text(font,std::to_string(i+1),r.x+14*u,r.y+12*u,12*u,Fade(c,.4f+.6f*v));
                text(font,on?"ON":"OFF",r.x+r.width-42*u,r.y+12*u,11*u,on?c:Color{102,121,140,255});
                float size=17*u;
                while(MeasureTextEx(font,trackName(i),size,.5f).x>r.width-26*u)size-=u;
                text(font,trackName(i),r.x+13*u,r.y+36*u,size,Fade({226,235,241,255},.4f+.6f*v));
                text(font,campaign.tracks[size_t(i)].role,r.x+13*u,r.y+62*u,9*u,Fade(c,.35f+.45f*v));
                for(int j=0;j<80;++j) {
                    float x=r.x+13*u+j*(r.width-26*u)/80;
                    float a=(2+waves[i][j]*12)*u*(.28f+.72f*v);
                    DrawLineEx({x,r.y+91*u-a*.5f},{x,r.y+91*u+a*.5f},u,Fade(c,.14f+.5f*v));
                }
                float playX=r.x+13*u+loopPos*(r.width-26*u);
                DrawCircleV({playX,r.y+91*u},2*u,Fade(c,.3f+.7f*v));
            }
            auto button=[&](Rectangle r,const char* label){
                if(CheckCollisionPointRec(mouse,r))DrawRectangleRounded(r,.2f,6,{31,47,59,220});
                text(font,label,r.x+7*u,r.y+5*u,12*u,{166,191,202,255});
            };
            button(pauseButton,mixer.playing?"II  PAUSE":">  PLAY");button(allButton,"ALL ON");button(silenceButton,"ALL OFF");
            if(elapsed-toastAt<2.6) {
                float age=float(elapsed-toastAt),alpha=std::min(1.f,(2.6f-age)*2.2f);
                centered(font,toast,w*.5f,30*u,12*u,Fade(reloadError.empty()?Color{124,235,210,255}:Color{240,172,138,255},alpha));
            }
            centered(font,"1-7  TRACKS    CLICK AN ORBIT  DESCEND    Z  FRONTIER WORLD    SPACE  PAUSE    ESC  EXIT",w*.5f,h-32*u,10*u,{106,137,155,255});
            text(font,"VOLUME",volumeBar.x-66*u,h-32*u,10*u,{143,168,183,255});
            DrawRectangleRec(volumeBar,{46,67,80,255});
            DrawRectangleRec({volumeBar.x,volumeBar.y,volumeBar.width*mixer.volume,volumeBar.height},{126,195,191,255});
            DrawCircleV({volumeBar.x+volumeBar.width*mixer.volume,volumeBar.y+2*u},4*u,{196,235,225,255});
            EndDrawing();
            if(elapsed-lastState>=.2) {writeState(options,mixer,gpu,vendor,true);lastState=elapsed;}
            if(!options.capture.empty()&&!captured&&(captureNow||elapsed>options.captureAfter)) {
                fs::create_directories(options.capture.parent_path());
                Image screenshot=LoadImageFromScreen();
                bool saved=ExportImage(screenshot,options.capture.c_str());UnloadImage(screenshot);
                if(!saved)throw std::runtime_error("Cannot save screenshot: "+options.capture.string());
                captured=true;
            }
        }
        writeState(options,mixer,gpu,vendor,false);
        StopAudioStream(stream);UnloadAudioStream(stream);streamReady=false;
        CloseAudioDevice();audioReady=false;audioMixer=nullptr;
        for(int i=0;i<campaign.count();++i)if(baked[i])UnloadTexture(sheets[i]);
        UnloadRenderTexture(background);UnloadShader(shader);UnloadFont(font);CloseWindow();windowReady=false;
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"[fatal] "<<error.what()<<'\n';
        if(streamReady){StopAudioStream(stream);UnloadAudioStream(stream);}
        if(audioReady)CloseAudioDevice();
        if(windowReady)CloseWindow();
        return 1;
    }
}
