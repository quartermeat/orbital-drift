#include "mixer.hpp"
#include "hotreload.hpp"
#include "raylib.h"
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
    bool windowed=false,check=false;
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
    out<<"{\n  \"app\":\"orbital-drift\",\"version\":\"0.2.0\",\"running\":"<<(running?"true":"false")
       <<",\"renderer\":"<<quote(gpu)<<",\"vendor\":"<<quote(vendor)<<",\"hardware_accelerated\":true"
       <<",\"fullscreen\":"<<(IsWindowFullscreen()?"true":"false")
       <<",\"width\":"<<GetScreenWidth()<<",\"height\":"<<GetScreenHeight()<<",\"fps\":"<<GetFPS()
       <<",\"audio_ready\":"<<((running&&IsAudioDeviceReady())?"true":"false")<<",\"playing\":"<<(mixer.playing?"true":"false")
       <<",\"sample_rate\":48000,\"loop_frames\":"<<mixer.frames<<",\"position_frames\":"<<mixer.position.load()
       <<",\"rendered_frames\":"<<mixer.renderedFrames.load()<<",\"output_rms\":"<<mixer.outputRms.load()
       <<",\"volume\":"<<mixer.volume.load()
       <<",\"hot_reload\":{\"config_reloads\":"<<configReloads<<",\"shader_reloads\":"<<shaderReloads
       <<",\"last_error\":"<<quote(reloadError)<<"}"
       <<",\"tracks\":[";
    uint32_t mask=mixer.enabled;
    for(int i=0;i<TrackCount;++i) {
        if(i)out<<',';
        out<<"{\"name\":"<<quote(Names[i])<<",\"enabled\":"<<((mask&(1u<<i))?"true":"false")<<",\"level\":"<<mixer.levels[i].load()<<'}';
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
            else if(arg=="--assets")options.assets=fs::absolute(value());
            else if(arg=="--state")options.state=fs::absolute(value());
            else if(arg=="--capture")options.capture=fs::absolute(value());
            else if(arg=="--seconds")options.seconds=std::stod(value());
            else if(arg=="--help") {
                std::cout<<"Orbital Drift 0.2.0\nDefault: fullscreen. Click cards/orbs or 1-7 toggle tracks.\nSpace pause; M all off/on; A all on; +/- volume; F11 fullscreen; Esc exit.\n"
                         <<"Options: --windowed --seconds N --capture file.png --state file.json --assets directory --check-assets\n";return 0;
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
        SetExitKey(KEY_ESCAPE);
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
        layers=loadLayerConfig(options.assets/"layers.conf");applyLayers();
        if(!layers.note.empty())std::cout<<"[config] "<<layers.note<<std::endl;
        Font font=LoadFontEx((options.assets/"font.ttf").c_str(),72,nullptr,0);
        if(!IsFontValid(font))throw std::runtime_error("Cannot load UI font");
        GenTextureMipmaps(&font.texture);
        SetTextureFilter(font.texture,TEXTURE_FILTER_TRILINEAR);
        Shader shader=LoadShader(nullptr,(options.assets/"space.fs").c_str());
        if(!IsShaderValid(shader)||shader.id==rlGetShaderIdDefault())throw std::runtime_error("Space shader failed to compile");
        int resLoc=GetShaderLocation(shader,"resolution"),timeLoc=GetShaderLocation(shader,"time"),energyLoc=GetShaderLocation(shader,"energy");
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
        std::array<float,TrackCount> visibility{},meter{};visibility.fill(1);
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
                    ++configReloads;reloadError=layers.note;
                    toast=layers.note.empty()?"layers.conf reloaded":"layers.conf: "+layers.note;
                    toastAt=elapsed;std::cout<<"[reload] "<<toast<<std::endl;
                }
            }
            float w=float(GetScreenWidth()),h=float(GetScreenHeight()),u=std::min(w/1600.f,h/900.f);
            float margin=52*u, gap=12*u,cardWidth=(w-margin*2-gap*6)/7,cardY=h-169*u,cardH=112*u;
            Vector2 center{w*.5f,h*.435f};float radius=std::min(w*.31f,h*.34f);
            Vector2 mouse=GetMousePosition();
            uint32_t mask=mixer.enabled;
            float dt=std::min(GetFrameTime(),.1f),motion=float(elapsed);
            float progress=float(mixer.position.load())/float(mixer.frames);
            float beat=progress*64;
            std::array<Vector2,TrackCount> nodes{};
            std::array<Rectangle,TrackCount> cards{};
            int hovered=-1;
            for(int i=0;i<TrackCount;++i) {
                float orbit=radius*(layers.orbitBase+i*layers.orbitStep);
                float angle=motion*(.055f+i*.009f)+float(i)*2.39996f;
                nodes[i]={center.x+std::cos(angle)*orbit,center.y+std::sin(angle)*orbit*.56f};
                cards[i]={margin+i*(cardWidth+gap),cardY,cardWidth,cardH};
                if(CheckCollisionPointRec(mouse,cards[i])||CheckCollisionPointCircle(mouse,nodes[i],22*u))hovered=i;
                if(IsKeyPressed(KEY_ONE+i)||(hovered==i&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
                    mixer.toggle(i);std::cout<<"[track] "<<(i+1)<<' '<<Names[i]<<' '<<((mixer.enabled&(1u<<i))?"on":"off")<<std::endl;
                }
            }
            Rectangle pauseButton{margin, h-39*u,82*u,26*u};
            Rectangle allButton{margin+97*u,h-39*u,82*u,26*u};
            Rectangle silenceButton{margin+194*u,h-39*u,82*u,26*u};
            Rectangle volumeBar{w-margin-130*u,h-28*u,130*u,4*u};
            Rectangle volumeHit{volumeBar.x,volumeBar.y-12*u,volumeBar.width,28*u};
            if(IsKeyPressed(KEY_SPACE)||(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(mouse,pauseButton)))mixer.playing=!mixer.playing;
            if(IsKeyPressed(KEY_A)||(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(mouse,allButton)))mixer.enabled=AllTracks;
            if(IsKeyPressed(KEY_M))mixer.enabled=mixer.enabled?0:AllTracks;
            if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(mouse,silenceButton))mixer.enabled=0;
            if(IsKeyPressed(KEY_EQUAL)||IsKeyPressed(KEY_KP_ADD))mixer.volume=std::min(1.f,mixer.volume+.05f);
            if(IsKeyPressed(KEY_MINUS)||IsKeyPressed(KEY_KP_SUBTRACT))mixer.volume=std::max(0.f,mixer.volume-.05f);
            if(IsMouseButtonDown(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(mouse,volumeHit))mixer.volume=std::clamp((mouse.x-volumeBar.x)/volumeBar.width,0.f,1.f);
            if(IsKeyPressed(KEY_F11)) {
                if(IsWindowFullscreen()){ToggleFullscreen();SetWindowSize(1440,900);}
                else{int mon=GetCurrentMonitor();SetWindowSize(GetMonitorWidth(mon),GetMonitorHeight(mon));ToggleFullscreen();}
            }
            mask=mixer.enabled;
            bool hot=hovered>=0||CheckCollisionPointRec(mouse,pauseButton)||CheckCollisionPointRec(mouse,allButton)||CheckCollisionPointRec(mouse,silenceButton)||CheckCollisionPointRec(mouse,volumeHit);
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
            text(font,std::to_string(active)+" OF 7 SIGNALS ACTIVE",w-margin-208*u,69*u,12*u,{113,154,166,255});
            for(int i=0;i<4;++i)DrawCircleV({w-margin-196*u+i*22*u,106*u},3*u,Fade({124,235,210,255},int(beat)%4==i&&mixer.playing?.95f:.18f));
            // Every track has a visible orbit, even while silent, so re-entry is discoverable.
            for(int i=0;i<TrackCount;++i) {
                float orbit=radius*(layers.orbitBase+i*layers.orbitStep);
                Color c=Colors[i];
                for(int j=0;j<160;++j) {
                    float a=j*2*PI/160,b=(j+1)*2*PI/160;
                    DrawLineEx({center.x+std::cos(a)*orbit,center.y+std::sin(a)*orbit*.56f},
                               {center.x+std::cos(b)*orbit,center.y+std::sin(b)*orbit*.56f},u,Fade(c,.055f+visibility[i]*.11f));
                }
                float strength=.2f+visibility[i]*.8f;
                DrawLineEx(center,nodes[i],u,Fade(c,(.015f+meter[i]*.045f)*visibility[i]));
                glow(nodes[i],(6+meter[i]*13)*u*layers.glowScale,c,strength*.8f);
                DrawCircleLinesV(nodes[i],(15+(hovered==i?4:0))*u,Fade(c,.2f+visibility[i]*.5f));
                centered(font,std::to_string(i+1),nodes[i].x,nodes[i].y-5*u,10*u,{235,245,255,255});
                if(hovered==i)centered(font,Names[i],nodes[i].x,nodes[i].y+27*u,14*u,Colors[i]);
            }
            float pulse=std::exp(-(beat-std::floor(beat))*5)*energy;
            glow(center,(30+energy*7+pulse*3)*u,{100,222,226,255},.22f+energy*.25f);
            DrawCircleLinesV(center,49*u,Fade({140,222,234,255},.18f+energy*.25f));
            DrawCircleLinesV(center,55*u,Fade({140,222,234,255},.1f));
            centered(font,"OD",center.x,center.y-12*u,24*u,{222,250,249,255});
            centered(font,active?"THE SIGNAL IS YOURS":"SPACE TO BREATHE",center.x,center.y+radius*.69f,12*u,{143,173,185,255});
            centered(font,"Click an orbit or a track below",center.x,center.y+radius*.69f+24*u,14*u,{102,129,149,255});
            float rulerY=cardY-39*u;
            DrawLineEx({margin,rulerY},{w-margin,rulerY},u,{38,54,69,255});
            DrawLineEx({margin,rulerY},{margin+(w-2*margin)*progress,rulerY},2*u,{117,193,188,255});
            for(int i=0;i<=16;++i) {
                float x=margin+(w-margin*2)*i/16;
                DrawLineEx({x,rulerY-3*u},{x,rulerY+(i%4?3:6)*u},u,{69,91,103,255});
            }
            text(font,"16-BAR ORBIT",margin,rulerY-23*u,11*u,{113,145,164,255});
            std::string bar="BAR "+std::to_string(std::min(16,int(progress*16)+1))+" / 16";
            text(font,bar,w-margin-89*u,rulerY-23*u,11*u,{144,178,191,255});
            for(int i=0;i<TrackCount;++i) {
                Rectangle r=cards[i];bool on=mask&(1u<<i);Color c=Colors[i];float v=visibility[i];
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
                float playX=r.x+13*u+progress*(r.width-26*u);
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
            centered(font,"1-7  TRACKS    SPACE  PAUSE    F11  FULLSCREEN    ESC  EXIT",w*.5f,h-32*u,10*u,{106,137,155,255});
            text(font,"VOLUME",volumeBar.x-66*u,h-32*u,10*u,{143,168,183,255});
            DrawRectangleRec(volumeBar,{46,67,80,255});
            DrawRectangleRec({volumeBar.x,volumeBar.y,volumeBar.width*mixer.volume,volumeBar.height},{126,195,191,255});
            DrawCircleV({volumeBar.x+volumeBar.width*mixer.volume,volumeBar.y+2*u},4*u,{196,235,225,255});
            EndDrawing();
            if(elapsed-lastState>=.2) {writeState(options,mixer,gpu,vendor,true);lastState=elapsed;}
            if(!options.capture.empty()&&!captured&&elapsed>2) {
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
