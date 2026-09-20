#include "mixer.hpp"
#include "hotreload.hpp"
#include "progress.hpp"
#include "figure.hpp"   // brings in scene.hpp
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
static std::array<Color,TrackCount> Colors{};
static int configReloads=0,shaderReloads=0;
static std::string reloadError;
static Progress progress;
static bool sigilVisible=false;
static float sigilX=0,sigilY=0,mouseX=0,mouseY=0;
static bool sigilHot=false;
enum class View { System, Planet };
static View view=View::System;
static int planetTrack=-1;
static bool beaconOnScreen=false,beaconFound=false;
static float beaconX=0,beaconY=0;
static double zoomLevel=1,viewCenterX=0,viewCenterY=0,beaconWorldX=0,beaconWorldY=0;

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
    for(int i=0;i<TrackCount;++i)Colors[i]=Color{layers.colors[i].r,layers.colors[i].g,layers.colors[i].b,255};
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
    fs::path progressFile=fs::canonical("/proc/self/exe").parent_path().parent_path()/"artifacts"/"progress.json";
    bool windowed=false,check=false,resume=false,gallery=false;
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
    out<<"{\n  \"app\":\"orbital-drift\",\"version\":\"0.9.0\",\"running\":"<<(running?"true":"false")
       <<",\"renderer\":"<<quote(gpu)<<",\"vendor\":"<<quote(vendor)<<",\"hardware_accelerated\":true"
       <<",\"fullscreen\":"<<(IsWindowFullscreen()?"true":"false")
       <<",\"width\":"<<GetScreenWidth()<<",\"height\":"<<GetScreenHeight()<<",\"fps\":"<<GetFPS()
       <<",\"audio_ready\":"<<((running&&IsAudioDeviceReady())?"true":"false")<<",\"playing\":"<<(mixer.playing?"true":"false")
       <<",\"sample_rate\":48000,\"loop_frames\":"<<mixer.frames<<",\"position_frames\":"<<mixer.position.load()
       <<",\"rendered_frames\":"<<mixer.renderedFrames.load()<<",\"output_rms\":"<<mixer.outputRms.load()
       <<",\"volume\":"<<mixer.volume.load()
       <<",\"hot_reload\":{\"config_reloads\":"<<configReloads<<",\"shader_reloads\":"<<shaderReloads
       <<",\"last_error\":"<<quote(reloadError)<<"}"
       <<",\"progress\":{\"unlocked\":"<<progress.unlocked<<",\"frontier\":"<<quote(Names[progress.frontier()])
       <<",\"next_locked\":"<<(progress.nextLocked()>=0?quote(Names[progress.nextLocked()]):std::string("null"))
       <<",\"sigil_visible\":"<<(sigilVisible?"true":"false")
       <<",\"sigil_x\":"<<int(sigilX)<<",\"sigil_y\":"<<int(sigilY)
       <<",\"sigil_hot\":"<<(sigilHot?"true":"false")<<",\"mouse_x\":"<<int(mouseX)<<",\"mouse_y\":"<<int(mouseY)
       <<"},\"planet\":{\"view\":"<<quote(view==View::Planet?"planet":"system")
       <<",\"track\":"<<(planetTrack>=0?quote(Names[planetTrack]):std::string("null"))
       <<",\"beacon_on_screen\":"<<(beaconOnScreen?"true":"false")<<",\"beacon_found\":"<<(beaconFound?"true":"false")
       <<",\"beacon_x\":"<<int(beaconX)<<",\"beacon_y\":"<<int(beaconY)<<",\"zoom\":"<<zoomLevel
       <<",\"view_x\":"<<viewCenterX<<",\"view_y\":"<<viewCenterY
       <<",\"beacon_world_x\":"<<beaconWorldX<<",\"beacon_world_y\":"<<beaconWorldY<<",\"complete\":"<<(progress.complete()?"true":"false")<<"}"
       <<",\"tracks\":[";
    uint32_t mask=mixer.enabled;
    for(int i=0;i<TrackCount;++i) {
        if(i)out<<',';
        out<<"{\"name\":"<<quote(Names[i])<<",\"enabled\":"<<((mask&(1u<<i))?"true":"false")
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
            else if(arg=="--world")options.world=std::stoi(value());
            else if(arg=="--capture-after")options.captureAfter=std::stod(value());
            else if(arg=="--assets")options.assets=fs::absolute(value());
            else if(arg=="--state")options.state=fs::absolute(value());
            else if(arg=="--capture")options.capture=fs::absolute(value());
            else if(arg=="--seconds")options.seconds=std::stod(value());
            else if(arg=="--help") {
                std::cout<<"Orbital Drift 0.9.0\nDefault: fullscreen, silent, one track unsealed.\nLeft-click cards/orbs or 1-7 toggle; right-click a sigil to unseal the next track.\nSpace pause; M all off/on; A all on; +/- volume; F11 fullscreen; Esc exit.\n"
                         <<"Options: --windowed --seconds N --capture file.png --state file.json --assets directory --check-assets --resume --gallery --world N --capture-after SECONDS\n";return 0;
            } else throw std::runtime_error("Unknown argument: "+arg);
        }
        mixer.load(options.assets/"audio");
        if(options.check) {
            double energy=0;float peak=0;
            for(size_t sample=0;sample<mixer.frames*2;++sample) {
                float sum=0;for(auto& track:mixer.tracks)sum+=track[sample];
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
        InitWindow(1440,900,"Orbital Drift");windowReady=true;
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
        progress=options.resume?loadProgress(options.progressFile):Progress{};
        saveProgress(options.progressFile,progress);
        mixer.enabled=0;   // the drift begins in silence
        std::cout<<"[progress] "<<progress.unlocked<<" of "<<TrackCount<<" unlocked; frontier "<<Names[progress.frontier()]
                 <<(options.resume?" (resumed)":" (fresh run)")<<"; starting silent"<<std::endl;
        layers=loadLayerConfig(options.assets/"layers.conf");applyLayers();
        if(!layers.note.empty())std::cout<<"[config] "<<layers.note<<std::endl;
        if(options.gallery||options.world>=0){progress.unlocked=TrackCount;mixer.enabled=AllTracks;}
        Font font=LoadFontEx((options.assets/"font.ttf").c_str(),72,nullptr,0);
        if(!IsFontValid(font))throw std::runtime_error("Cannot load UI font");
        GenTextureMipmaps(&font.texture);
        SetTextureFilter(font.texture,TEXTURE_FILTER_TRILINEAR);
        Shader shader=LoadShader(nullptr,(options.assets/"space.fs").c_str());
        if(!IsShaderValid(shader)||shader.id==rlGetShaderIdDefault())throw std::runtime_error("Space shader failed to compile");
        int resLoc=GetShaderLocation(shader,"resolution"),timeLoc=GetShaderLocation(shader,"time"),energyLoc=GetShaderLocation(shader,"energy");
        std::array<Scene,TrackCount> scenes{};
        std::array<Texture2D,TrackCount> sheets{};
        std::array<bool,TrackCount> baked{};
        auto worldSheet=[&](int track)->Texture2D&{
            if(!baked[size_t(track)]) {
                scenes[size_t(track)]=generateScene(track,layers.colors[size_t(track)]);
                sheets[size_t(track)]=bakeTerrain(scenes[size_t(track)]);
                baked[size_t(track)]=true;
                std::cout<<"[world] baked "<<Names[track]<<std::endl;
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
        std::cout<<"[audio] 7 synchronized stems, 48000 Hz stereo, "<<mixer.frames<<" frames per loop"<<std::endl;
        std::array<std::array<float,80>,TrackCount> waves{};
        for(int i=0;i<TrackCount;++i)for(int j=0;j<80;++j) {
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
        std::array<float,TrackCount> visibility{},meter{};
        double unlockedAt=-9;std::string unlockedName;
        // The view into a world: where we are looking, and how many pixels one
        // canvas unit covers. Doubles, because deep zoom runs out of float fast.
        double viewX=.5,viewY=.5,viewScale=1;
        double enteredAt=-9;
        float sigilPulse=0;
        if(options.gallery||options.world>=0) {
            view=View::Planet;planetTrack=std::clamp(options.world,0,TrackCount-1);
            viewScale=std::min(GetScreenWidth()/double(SceneWidth),GetScreenHeight()/double(SceneHeight));
            viewX=SceneWidth*.5;viewY=SceneHeight*.5;
        }
        double started=GetTime(),lastState=-1,toastAt=-9;
        bool captured=false;
        long frame=0;
        std::string toast;
        while(!WindowShouldClose()&&!interrupted) {
            double elapsed=GetTime()-started;
            if(options.seconds>0&&elapsed>=options.seconds)break;
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
                    for(int i=0;i<TrackCount;++i)if(baked[i]){UnloadTexture(sheets[i]);baked[i]=false;}
                    ++configReloads;reloadError=layers.note;
                    toast=layers.note.empty()?"layers.conf reloaded":"layers.conf: "+layers.note;
                    toastAt=elapsed;std::cout<<"[reload] "<<toast<<std::endl;
                }
            }
            float w=float(GetScreenWidth()),h=float(GetScreenHeight()),u=std::min(w/1600.f,h/900.f);
            // ---------- world view: pan and zoom the planet's background image ----------
            if(view==View::Planet) {
                Scene& scene=scenes[size_t(planetTrack)];
                Texture2D& sheet=worldSheet(planetTrack);
                float step=std::min(GetFrameTime(),.1f);
                bool leaving=IsKeyPressed(KEY_ESCAPE)||IsKeyPressed(KEY_BACKSPACE);
                if(options.gallery)
                    for(int i=0;i<TrackCount;++i)
                        if(IsKeyPressed(KEY_ONE+i)&&i!=planetTrack) {
                            planetTrack=i;worldSheet(i);
                            viewScale=std::min(w/float(SceneWidth),h/float(SceneHeight));
                            viewX=SceneWidth*.5;viewY=SceneHeight*.5;
                            std::cout<<"[gallery] "<<Names[i]<<std::endl;
                        }
                Vector2 pointer=GetMousePosition();
                double halfW=w*.5,halfH=h*.5;
                double fit=std::min(w/double(SceneWidth),h/double(SceneHeight));
                auto screenX=[&](double ix){return (ix-viewX)*viewScale+halfW;};
                auto screenY=[&](double iy){return (iy-viewY)*viewScale+halfH;};
                if(IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
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
                zoomLevel=viewScale/fit;viewCenterX=viewX;viewCenterY=viewY;

                const Marker& target=scene.markers[size_t(scene.beacon)];
                beaconWorldX=target.x;beaconWorldY=target.y;
                double beaconScreenX=screenX(target.x),beaconScreenY=screenY(target.y);
                double beaconPixels=target.size*viewScale;
                beaconOnScreen=beaconScreenX>0&&beaconScreenY>0&&beaconScreenX<w&&beaconScreenY<h;
                beaconX=float(beaconScreenX);beaconY=float(beaconScreenY);beaconFound=scene.found;
                bool overBeacon=beaconOnScreen
                    &&Vector2Distance(pointer,{float(beaconScreenX),float(beaconScreenY)})<float(std::max(15.0,beaconPixels));
                if(IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)&&!scene.found) {
                    if(overBeacon) {
                        scene.found=beaconFound=true;
                        toast="Found it";toastAt=elapsed;reloadError.clear();
                        if(!progress.complete()&&planetTrack==progress.frontier()) {
                            unlockedName=Names[progress.nextLocked()];
                            progress.advance();saveProgress(options.progressFile,progress);
                            mixer.enabled|=1u<<progress.frontier();unlockedAt=elapsed;
                            std::cout<<"[unlock] "<<unlockedName<<" ("<<progress.unlocked<<'/'<<TrackCount<<')'<<std::endl;
                        }
                    } else {toast="Not that one";toastAt=elapsed;reloadError.clear();}
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
                int drawnPeople=0;
                for(const PersonSpot& spot:scene.people) {
                    float px=float(spot.height*viewScale);
                    if(px<1.1f)continue;
                    double sx=screenX(spot.x),sy=screenY(spot.y);
                    if(sx<-px*2||sy<-px*2||sx>w+px*2||sy>h+px*2)continue;
                    ++drawnPeople;
                    if(px<9.f) {    // below this a body is unreadable anyway: a mark will do
                        Rng quick(spot.seed);
                        Color tint=ClothTones[quick.below(ClothCount)];
                        DrawRectangleRec({float(sx-px*.22),float(sy-px*.75),
                                          std::max(1.f,px*.44f),std::max(1.f,px*.8f)},tint);
                        continue;
                    }
                    Rng roll(spot.seed);
                    drawFigure(rollFigure(roll),{float(sx),float(sy)},px);
                }

                float pad=38*u;
                Rgb wantedRgb=scene.palette[BeaconPalette];
                Color want{wantedRgb.r,wantedRgb.g,wantedRgb.b,255};
                if(scene.found) {
                    float ring=float(std::max(20.0,beaconPixels*1.8))+4*std::sin(float(elapsed)*3);
                    DrawCircleLinesV({float(beaconScreenX),float(beaconScreenY)},ring,Fade({208,244,228,255},.9f));
                    centered(font,"FOUND",float(beaconScreenX),float(beaconScreenY)+ring+7*u,11*u,{208,244,228,255});
                } else if(overBeacon) {
                    DrawCircleLinesV({float(beaconScreenX),float(beaconScreenY)},
                                     float(std::max(16.0,beaconPixels*1.6)),Fade(want,.55f));
                }
                DrawRectangleRounded({pad-16*u,pad-24*u,430*u,98*u},.08f,8,Fade(edge,.72f));
                text(font,options.gallery?"GALLERY":"WORLD",pad,pad-10*u,12*u,{118,150,172,255});
                text(font,Names[planetTrack],pad,pad+8*u,34*u,{231,238,244,255});
                text(font,scene.found?"This world has given up its secret"
                                     :(options.gallery?"1-7 switch worlds. Drag to pan, wheel to zoom."
                                                      :"One marker wears this colour. Zoom in and look."),
                     pad,pad+50*u,13*u,{136,162,182,255});
                float cardW=196*u,cardH=112*u,cardX=w-pad-cardW,cardTop=pad-14*u;
                DrawRectangleRounded({cardX,cardTop,cardW,cardH},.1f,8,{10,17,27,232});
                DrawRectangleRoundedLinesEx({cardX,cardTop,cardW,cardH},.1f,8,u,Fade(want,.45f));
                text(font,"FIND",cardX+14*u,cardTop+11*u,11*u,{132,160,180,255});
                DrawPoly({cardX+cardW*.5f,cardTop+62*u},3,25*u,-90,want);
                centered(font,"A MARKER, THIS COLOUR",cardX+cardW*.5f,cardTop+88*u,10*u,Fade(want,.85f));
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
                        <<"    "<<scene.towns.size()<<" TOWNS    "<<scene.people.size()<<" PEOPLE    "
                        <<drawnPeople<<" IN VIEW";
                text(font,zoomText.str(),pad,h-pad-4*u,11*u,{112,142,162,255});
                if(elapsed-toastAt<2.6) {
                    float age=float(elapsed-toastAt),alpha=std::min(1.f,(2.6f-age)*2.2f);
                    centered(font,toast,w*.5f,pad-8*u,13*u,
                             Fade(reloadError.empty()?Color{124,235,210,255}:Color{240,172,138,255},alpha));
                }
                DrawRectangle(0,int(h-46*u),int(w),int(46*u),Fade(edge,.8f));
                centered(font,options.gallery
                         ?"1-7  SWITCH WORLD     DRAG  PAN     WHEEL / W S  ZOOM     ESC  BACK"
                         :"DRAG  PAN     WHEEL / W S  ZOOM     RIGHT-CLICK  MARK IT     ESC  BACK",
                         w*.5f,h-30*u,10*u,{128,158,176,255});
                EndDrawing();
                if(elapsed-lastState>=.2) {writeState(options,mixer,gpu,vendor,true);lastState=elapsed;}
                if(!options.capture.empty()&&!captured&&elapsed>options.captureAfter) {
                    fs::create_directories(options.capture.parent_path());
                    Image shot=LoadImageFromScreen();
                    captured=ExportImage(shot,options.capture.c_str());UnloadImage(shot);
                }
                if(leaving) {
                    view=View::System;planetTrack=-1;beaconOnScreen=false;
                    std::cout<<"[world] left"<<std::endl;
                }
                continue;
            }
            float margin=52*u, gap=12*u,cardWidth=(w-margin*2-gap*6)/7,cardY=h-169*u,cardH=112*u;
            Vector2 center{w*.5f,h*.435f};float radius=std::min(w*.31f,h*.34f);
            Vector2 mouse=GetMousePosition();
            uint32_t mask=mixer.enabled;
            float dt=std::min(GetFrameTime(),.1f),motion=float(elapsed);
            float loopPos=float(mixer.position.load())/float(mixer.frames);
            float beat=loopPos*64;
            std::array<Vector2,TrackCount> nodes{};
            std::array<Rectangle,TrackCount> cards{};
            int hovered=-1,hoveredNode=-1;
            for(int i=0;i<TrackCount;++i) {
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
                        mixer.toggle(i);std::cout<<"[track] "<<(i+1)<<' '<<Names[i]<<' '<<((mixer.enabled&(1u<<i))?"on":"off")<<std::endl;
                    }
                }
            }
            auto enterPlanet=[&](int track){
                view=View::Planet;planetTrack=track;
                viewScale=std::min(w/double(SceneWidth),h/double(SceneHeight));
                viewX=SceneWidth*.5;viewY=SceneHeight*.5;
                enteredAt=elapsed;(void)enteredAt;
                std::cout<<"[world] entered "<<Names[track]<<std::endl;
            };
            // Read the mixer live, not the mask snapshotted at the top of the
            // frame: a track woken earlier this frame is already on.
            uint32_t live=mixer.enabled;
            if(hoveredNode>=0&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if(!progress.isUnlocked(hoveredNode))     {toast="That signal is still sealed";toastAt=elapsed;reloadError.clear();}
                else if(!(live&(1u<<hoveredNode)))        {toast="Wake the signal to reach its world";toastAt=elapsed;reloadError.clear();}
                else enterPlanet(hoveredNode);
            }
            // Z drops into the frontier world without hunting a moving node.
            if(IsKeyPressed(KEY_Z)&&progress.isUnlocked(progress.frontier())&&(live&(1u<<progress.frontier())))
                enterPlanet(progress.frontier());
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
            for(int i=0;i<TrackCount;++i) {
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
            text(font,"ORBITAL / 001",margin,30*u,13*u,{123,157,177,255});
            text(font,"Orbital Drift",margin,53*u,46*u,{232,239,244,255});
            text(font,"Build a world out of sound.",margin,109*u,16*u,{144,161,183,255});
            int active=0;for(int i=0;i<7;++i)if(mask&(1u<<i))++active;
            text(font,"72 BPM   /   A MINOR",w-margin-208*u,40*u,16*u,{188,207,218,255});
            text(font,std::to_string(active)+" OF "+std::to_string(progress.unlocked)+" SIGNALS ACTIVE",w-margin-208*u,69*u,12*u,{113,154,166,255});
            if(!progress.complete())
                text(font,std::to_string(TrackCount-progress.unlocked)+" STILL SEALED",w-margin-208*u,88*u,11*u,{126,110,150,255});
            for(int i=0;i<4;++i)DrawCircleV({w-margin-196*u+i*22*u,106*u},3*u,Fade({124,235,210,255},int(beat)%4==i&&mixer.playing?.95f:.18f));
            // Every track has a visible orbit, even while silent, so re-entry is discoverable.
            for(int i=0;i<TrackCount;++i) {
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
                    centered(font,Names[i],nodes[i].x,nodes[i].y+27*u,14*u,Colors[i]);
                    centered(font,(mask&(1u<<i))?"CLICK TO DESCEND":"WAKE IT FIRST",nodes[i].x,nodes[i].y+44*u,10*u,
                             Fade(c,(mask&(1u<<i))?.9f:.45f));
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
            for(int i=0;i<=16;++i) {
                float x=margin+(w-margin*2)*i/16;
                DrawLineEx({x,rulerY-3*u},{x,rulerY+(i%4?3:6)*u},u,{69,91,103,255});
            }
            text(font,"16-BAR ORBIT",margin,rulerY-23*u,11*u,{113,145,164,255});
            std::string bar="BAR "+std::to_string(std::min(16,int(loopPos*16)+1))+" / 16";
            text(font,bar,w-margin-89*u,rulerY-23*u,11*u,{144,178,191,255});
            for(int i=0;i<TrackCount;++i) {
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
                while(MeasureTextEx(font,Names[i],size,.5f).x>r.width-26*u)size-=u;
                text(font,Names[i],r.x+13*u,r.y+36*u,size,Fade({226,235,241,255},.4f+.6f*v));
                text(font,layers.roles[i],r.x+13*u,r.y+62*u,9*u,Fade(c,.35f+.45f*v));
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
            if(!options.capture.empty()&&!captured&&elapsed>options.captureAfter) {
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
        for(int i=0;i<TrackCount;++i)if(baked[i])UnloadTexture(sheets[i]);
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
