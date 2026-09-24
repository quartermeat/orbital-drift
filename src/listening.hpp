#pragma once
// GPU sand relief with live desktop audio. The original campaign mixer is not opened.
// Two surfaces share one tray of sand: a magnetic ball that rakes it, and a
// Chladni plate that shakes it into the still lines of whatever is playing.
#include "desktop_monitor.hpp"
#include "sand_motion.hpp"
#include "chladni.hpp"
#include <future>

static int runListening(const Options& options) {
    constexpr int GaugeSize=32;
    // How coarse the sand is, as the size of a cell. This is the real thing
    // rather than a filter over it: a coarse grain is a bigger cell holding its
    // own grains, so the tray is rebuilt when it changes.
    constexpr int GrainSteps[]={1536,1024,768,512,384,256};
    constexpr int GrainChoices=int(sizeof(GrainSteps)/sizeof(GrainSteps[0]));
    // The tray is a grid of matter, one cell to the pixel, each holding a whole
    // number of grains. The plate starts from a shallow bed because sand off
    // nine tenths of the tray has to stand somewhere; the ball wants a deep one
    // to carve into.
    constexpr float RakeBedGrains=40;
    // A cellular automaton runs in sweeps, not seconds. One sweep moves grains
    // between 2x2 blocks only, which is a fraction of what the old continuous
    // exchange did, so a frame is worth several of them.
    constexpr int PlateSweeps=4;
    bool windowReady=false;
    RenderTexture2D field[2]{},gauge{};Shader updateShader{},plateShader{},renderShader{},reduceShader{},levelShader{};Font font{};
    try {
        std::signal(SIGINT,onSignal);std::signal(SIGTERM,onSignal);
        SetConfigFlags(FLAG_VSYNC_HINT|FLAG_MSAA_4X_HINT|FLAG_WINDOW_RESIZABLE);
        InitWindow(1280,900,"Orbital Drift");windowReady=true;SetWindowMinSize(800,600);
        SetExitKey(KEY_NULL);SetTargetFPS(60);
        if(!options.windowed) {
            int m=GetCurrentMonitor();SetWindowSize(GetMonitorWidth(m),GetMonitorHeight(m));ToggleFullscreen();
        }
        const char* renderer=reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        std::string gpu=renderer?renderer:"unknown",lower=gpu;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return std::tolower(c);});
        if(lower.find("llvmpipe")!=std::string::npos||lower.find("softpipe")!=std::string::npos
           ||lower.find("software")!=std::string::npos||gpu=="unknown")
            throw std::runtime_error("Hardware OpenGL required; detected "+gpu);
        font=LoadFontEx((options.assets/"font.ttf").c_str(),48,nullptr,0);
        if(!IsFontValid(font))throw std::runtime_error("Missing font; run python3 scripts/setup.py");
        SetTextureFilter(font.texture,TEXTURE_FILTER_BILINEAR);
        auto shader=[&](const char* name) {
            Shader s=LoadShader(nullptr,(options.assets/name).c_str());
            if(!IsShaderValid(s)||s.id==rlGetShaderIdDefault()) {
                UnloadShader(s);throw std::runtime_error(std::string("Cannot compile ")+name);
            }
            return s;
        };
        updateShader=shader("sand-update.fs");plateShader=shader("chladni-update.fs");
        renderShader=shader("sand-render.fs");reduceShader=shader("sand-reduce.fs");
        levelShader=shader("sand-level.fs");
        // raylib's own render textures are eight bits a channel, which is far too
        // coarse for sand that moves a thousandth of a tray at a time: every
        // exchange would round back to where it started and the plate would sit
        // there looking broken. These are float attachments instead.
        auto floatField=[&](int size,int format) {
            RenderTexture2D target{};
            target.id=rlLoadFramebuffer();
            if(!target.id)throw std::runtime_error("Cannot allocate a GPU sand field");
            rlEnableFramebuffer(target.id);
            target.texture.id=rlLoadTexture(nullptr,size,size,format,1);
            target.texture.width=target.texture.height=size;
            target.texture.mipmaps=1;target.texture.format=format;
            if(!target.texture.id) {
                rlDisableFramebuffer();rlUnloadFramebuffer(target.id);
                throw std::runtime_error("Floating point textures required for the sand table");
            }
            rlFramebufferAttach(target.id,target.texture.id,RL_ATTACHMENT_COLOR_CHANNEL0,RL_ATTACHMENT_TEXTURE2D,0);
            bool complete=rlFramebufferComplete(target.id);
            rlDisableFramebuffer();
            if(!complete)throw std::runtime_error("This GPU cannot draw into a floating point sand field");
            return target;
        };
        gauge=floatField(GaugeSize,RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
        bool onPlateSurface=options.chladni;
        // Open on the coarse end. A grain a pixel is the finest the tray goes
        // and it reads as dust; a grain a few pixels across is where the figure
        // actually looks like sand, so that is what you get handed.
        int grainChoice=GrainChoices-1,fieldSize=GrainSteps[grainChoice];
        auto bedOf=[](bool plate){return plate?PlateBedGrains:RakeBedGrains;};
        int levelLoc=GetShaderLocation(levelShader,"level");
        auto clear=[&](){
            float level=bedOf(onPlateSurface);
            SetShaderValue(levelShader,levelLoc,&level,SHADER_UNIFORM_FLOAT);
            for(auto& f:field) {
                BeginTextureMode(f);ClearBackground(BLACK);BeginShaderMode(levelShader);
                DrawRectangle(0,0,fieldSize,fieldSize,WHITE);
                EndShaderMode();EndTextureMode();
            }
        };
        int current=0;uint64_t strokes=0;int clears=0;
        int fromLoc=GetShaderLocation(updateShader,"ballFrom"),toLoc=GetShaderLocation(updateShader,"ballTo"),carveLoc=GetShaderLocation(updateShader,"carve");
        int modeLoc=GetShaderLocation(plateShader,"mode"),
            harmonicLoc=GetShaderLocation(plateShader,"harmonic"),weightLoc=GetShaderLocation(plateShader,"harmonicWeight"),
            agitationLoc=GetShaderLocation(plateShader,"agitation"),pushLoc=GetShaderLocation(plateShader,"push"),
            phaseLoc=GetShaderLocation(plateShader,"phase"),saltLoc=GetShaderLocation(plateShader,"salt"),
            plateFieldLoc=GetShaderLocation(plateShader,"field"),reposeLoc=GetShaderLocation(plateShader,"reposeGap");
        int carveBedLoc=GetShaderLocation(updateShader,"bed");
        int resLoc=GetShaderLocation(renderShader,"resolution"),ballLoc=GetShaderLocation(renderShader,"ball"),texelLoc=GetShaderLocation(renderShader,"texel"),
            warmLoc=GetShaderLocation(renderShader,"warmth"),ballShownLoc=GetShaderLocation(renderShader,"ballShown"),
            bedLoc=GetShaderLocation(renderShader,"bed"),renderFieldLoc=GetShaderLocation(renderShader,"field");
        int cellLoc=GetShaderLocation(reduceShader,"cell");
        float repose=PlateReposeGap;SetShaderValue(plateShader,reposeLoc,&repose,SHADER_UNIFORM_FLOAT);
        auto buildTray=[&](int size) {
            fieldSize=size;
            for(auto& f:field) {
                if(f.id)UnloadRenderTexture(f);
                f=floatField(size,RL_PIXELFORMAT_UNCOMPRESSED_R32);
                // Grains are counted, never blended: a filtered read would
                // invent half a grain between two cells and the speckle would
                // wash out.
                SetTextureFilter(f.texture,TEXTURE_FILTER_POINT);
                SetTextureWrap(f.texture,TEXTURE_WRAP_CLAMP);
            }
            float texel=1.f/float(size),side=float(size);
            SetShaderValue(renderShader,texelLoc,&texel,SHADER_UNIFORM_FLOAT);
            SetShaderValue(renderShader,renderFieldLoc,&side,SHADER_UNIFORM_FLOAT);
            SetShaderValue(plateShader,plateFieldLoc,&side,SHADER_UNIFORM_FLOAT);
            // Coarser cells differ by more energy across a block, so the shake
            // is measured against the spacing and the sand behaves the same.
            float push=.055f*(side*.5f);
            SetShaderValue(plateShader,pushLoc,&push,SHADER_UNIFORM_FLOAT);
            current=0;clear();
        };
        buildTray(GrainSteps[grainChoice]);
        float cell=1.f/GaugeSize;SetShaderValue(reduceShader,cellLoc,&cell,SHADER_UNIFORM_FLOAT);
        DesktopMonitor monitor;monitor.requestedSource=options.monitor;monitor.start();
        SandMotion motion;PlateDriver plate;
        bool paused=false;
        // The tray is counted rather than trusted: the mass is what shows the
        // plate moves grains instead of inventing them, and the spread is what
        // shows it sorted them into a figure instead of leaving a flat bed.
        float sandMass=bedOf(onPlateSurface),sandSpread=0;
        uint64_t sweeps=0;
        auto measureMass=[&] {
            BeginTextureMode(gauge);BeginShaderMode(reduceShader);
            DrawTexturePro(field[current].texture,{0,0,float(fieldSize),-float(fieldSize)},{0,0,float(GaugeSize),float(GaugeSize)},{0,0},0,WHITE);
            EndShaderMode();EndTextureMode();
            float* patch=static_cast<float*>(rlReadTexturePixels(gauge.texture.id,GaugeSize,GaugeSize,RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32));
            if(!patch)return;
            double depth=0,squares=0,counted=0;
            for(int i=0;i<GaugeSize*GaugeSize;++i) {
                depth+=patch[i*4];squares+=patch[i*4+1];counted+=patch[i*4+2];
            }
            RL_FREE(patch);
            if(counted<1)return;
            double mean=depth/counted;
            sandMass=float(mean);
            sandSpread=float(std::sqrt(std::max(0.0,squares/counted-mean*mean)));
        };
        double started=GetTime(),lastData=started,lastState=0,lastRoute=started,lastTouch=started,lastMass=0;
        std::future<std::string> routeQuery;
        bool captured=false,showHelp=true,draggingGrain=false;
        // The slider lives below the tray so it never competes with the drag
        // that steers the ball.
        auto grainTrack=[&]{
            float ui=std::max(1.f,GetScreenHeight()/1000.f);
            return Rectangle{32*ui,GetScreenHeight()-78*ui,210*ui,4*ui};
        };
        auto write=[&](bool running) {
            fs::create_directories(options.state.parent_path());auto temp=options.state;temp+=".tmp";
            std::ofstream out(temp);const auto& f=monitor.analysis.value;
            float scale=std::min(GetScreenWidth()*.45f,GetScreenHeight()*.43f);
            out<<"{\"app\":\"orbital-drift\",\"version\":\"0.23.0\",\"mode\":\"listening\",\"running\":"<<(running?"true":"false")
               <<",\"renderer\":"<<quote(gpu)<<",\"hardware_accelerated\":true,\"fps\":"<<GetFPS()
               <<",\"fullscreen\":"<<(IsWindowFullscreen()?"true":"false")<<",\"width\":"<<GetScreenWidth()<<",\"height\":"<<GetScreenHeight()
               <<",\"listening\":{\"connected\":"<<(monitor.connected?"true":"false")<<",\"source\":"<<quote(monitor.source)
               <<",\"error\":"<<quote(monitor.error)<<",\"sample_rate\":24000,\"frames\":"<<f.frames
               <<",\"rms\":"<<f.rms<<",\"bass\":"<<f.bass<<",\"body\":"<<f.body<<",\"air\":"<<f.air<<",\"pulse\":"<<f.pulse<<",\"onsets\":"<<f.onsets
               <<",\"tone\":"<<f.tone<<",\"tone_hz\":"<<f.toneHz<<",\"clarity\":"<<f.clarity
               <<",\"surface\":"<<(onPlateSurface?"\"plate\"":"\"rake\"")<<",\"field_size\":"<<fieldSize<<",\"grain_px\":"<<2*scale/float(fieldSize)
               <<",\"gpu_relief\":true,\"sand_mass\":"<<sandMass<<",\"sand_spread\":"<<sandSpread
               <<",\"bed\":"<<bedOf(onPlateSurface)
               <<",\"paused\":"<<(paused?"true":"false")<<",\"clears\":"<<clears
               <<",\"strokes\":"<<strokes<<",\"sweeps\":"<<sweeps
               <<",\"ball_x\":"<<motion.x<<",\"ball_y\":"<<motion.y
               <<",\"ball_screen_x\":"<<int(GetScreenWidth()*.5f+motion.x*scale)<<",\"ball_screen_y\":"<<int(GetScreenHeight()*.5f+motion.y*scale)
               <<",\"distance\":"<<motion.distance<<",\"speed\":"<<motion.speed<<",\"drive\":"<<motion.drive
               <<",\"rings\":"<<plate.live.rings<<",\"lobes\":"<<plate.live.lobes<<",\"spin\":"<<plate.live.spin
               <<",\"harmonic\":"<<plate.harmonicWeight<<",\"agitation\":"<<plate.agitation
               <<",\"tuning\":"<<plate.tuning<<",\"reconfigures\":"<<plate.reconfigures<<"}}\n";
            out.close();if(!out)throw std::runtime_error("Cannot write sand state");fs::rename(temp,options.state);
        };
        while(!WindowShouldClose()&&!interrupted) {
            double now=GetTime(),elapsed=now-started;
            if(options.seconds>0&&elapsed>=options.seconds)break;
            if(IsKeyPressed(KEY_ESCAPE))break;
            if(IsKeyPressed(KEY_F11)) {
                if(!IsWindowFullscreen()){int m=GetCurrentMonitor();SetWindowSize(GetMonitorWidth(m),GetMonitorHeight(m));ToggleFullscreen();}
                else {ToggleFullscreen();SetWindowSize(1280,900);}
            }
            if(IsKeyPressed(KEY_R)){monitor.start();lastData=now;}
            if(monitor.requestedSource=="auto"&&!routeQuery.valid()&&now-lastRoute>3) {
                routeQuery=std::async(std::launch::async,DesktopMonitor::musicSource);lastRoute=now;
            }
            if(routeQuery.valid()&&routeQuery.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
                auto source=routeQuery.get();if(source!=monitor.source){monitor.start(source);lastData=now;}
            }
            if(monitor.poll())lastData=now;
            if(now-lastData>1) {
                monitor.connected=false;auto frames=monitor.analysis.value.frames,onsets=monitor.analysis.value.onsets;
                monitor.analysis.value={};monitor.analysis.value.frames=frames;monitor.analysis.value.onsets=onsets;
            }
            // The same sand, handed between the two mechanisms.
            // The two mechanisms want different depths of sand, so handing the
            // tray over levels it: the plate cannot rake and the ball cannot
            // carve a bed that is not there. Not TAB: a window manager hands the
            // focused window a Tab press on its way out of an Alt-Tab, which
            // silently swapped the surface mid-run more than once.
            if(IsKeyPressed(KEY_P)){onPlateSurface=!onPlateSurface;clear();motion.clear();plate.clear();strokes=0;lastTouch=now;lastMass=0;}
            if(IsKeyPressed(KEY_C)||IsKeyPressed(KEY_BACKSPACE)) {
                clear();motion.clear();plate.clear();strokes=0;++clears;lastTouch=now;lastMass=0;
            }
            if(IsKeyPressed(KEY_SPACE)){paused=!paused;lastTouch=now;}
            if(IsKeyPressed(KEY_H))showHelp=!showHelp;
            // Grain size, dragged. Changing it rebuilds the tray, because a
            // coarser grain is a bigger cell rather than a blurrier picture.
            {
                float ui=std::max(1.f,GetScreenHeight()/1000.f);
                Rectangle track=grainTrack();
                Rectangle grab{track.x-10*ui,track.y-16*ui,track.width+20*ui,32*ui};
                Vector2 at=GetMousePosition();
                if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&showHelp&&CheckCollisionPointRec(at,grab))draggingGrain=true;
                if(!IsMouseButtonDown(MOUSE_BUTTON_LEFT))draggingGrain=false;
                if(draggingGrain) {
                    float along=std::clamp((at.x-track.x)/track.width,0.f,1.f);
                    int choice=int(std::lround(along*(GrainChoices-1)));
                    if(choice!=grainChoice) {
                        grainChoice=choice;buildTray(GrainSteps[choice]);
                        motion.clear();plate.clear();strokes=0;lastMass=0;
                    }
                    lastTouch=now;
                }
            }
            float wheel=GetMouseWheelMove();
            if(onPlateSurface)plate.tuning=std::clamp(plate.tuning+wheel*.1f,.25f,3.f);
            else motion.drive=std::clamp(motion.drive+wheel*.1f,.25f,3.f);
            Vector2 delta=GetMouseDelta();if(delta.x||delta.y||wheel)lastTouch=now;
            float scale=std::min(GetScreenWidth()*.45f,GetScreenHeight()*.43f);
            Vector2 pointer=GetMousePosition();
            float px=(pointer.x-GetScreenWidth()*.5f)/scale,py=(pointer.y-GetScreenHeight()*.5f)/scale;
            bool hand=!onPlateSurface&&!draggingGrain&&IsMouseButtonDown(MOUSE_BUTTON_LEFT)&&std::hypot(px,py)<1;
            const auto& f=monitor.analysis.value;
            motion.paused=paused;plate.paused=paused;
            float frame=GetFrameTime();
            if(onPlateSurface) {
                plate.update(f,frame);
                if(plate.agitation>0) {
                    float mode[3]={plate.live.rings,float(plate.live.lobes),plate.live.spin};
                    float harmonic[3]={plate.harmonic.rings,float(plate.harmonic.lobes),plate.harmonic.spin};
                    SetShaderValue(plateShader,modeLoc,mode,SHADER_UNIFORM_VEC3);
                    SetShaderValue(plateShader,harmonicLoc,harmonic,SHADER_UNIFORM_VEC3);
                    SetShaderValue(plateShader,weightLoc,&plate.harmonicWeight,SHADER_UNIFORM_FLOAT);
                    SetShaderValue(plateShader,agitationLoc,&plate.agitation,SHADER_UNIFORM_FLOAT);
                    for(int sweep=0;sweep<PlateSweeps;++sweep) {
                        // The blocks shift every sweep; without that a grain
                        // rattles inside its own four cells and never travels.
                        float phaseValue=float(sweeps&1),salt=float(sweeps%977)+.5f;
                        SetShaderValue(plateShader,phaseLoc,&phaseValue,SHADER_UNIFORM_FLOAT);
                        SetShaderValue(plateShader,saltLoc,&salt,SHADER_UNIFORM_FLOAT);
                        BeginTextureMode(field[1-current]);BeginShaderMode(plateShader);
                        DrawTexturePro(field[current].texture,{0,0,float(fieldSize),-float(fieldSize)},{0,0,float(fieldSize),float(fieldSize)},{0,0},0,WHITE);
                        EndShaderMode();EndTextureMode();current=1-current;++sweeps;
                    }
                    ++strokes;
                }
            } else {
                motion.update(f,frame,hand,px,py);
                if(motion.speed>0) {
                    float from[2]={motion.previousX,motion.previousY},to[2]={motion.x,motion.y},carve=1;
                    float carveBed=RakeBedGrains;
                    SetShaderValue(updateShader,fromLoc,from,SHADER_UNIFORM_VEC2);SetShaderValue(updateShader,toLoc,to,SHADER_UNIFORM_VEC2);
                    SetShaderValue(updateShader,carveLoc,&carve,SHADER_UNIFORM_FLOAT);
                    SetShaderValue(updateShader,carveBedLoc,&carveBed,SHADER_UNIFORM_FLOAT);
                    BeginTextureMode(field[1-current]);BeginShaderMode(updateShader);
                    DrawTexturePro(field[current].texture,{0,0,float(fieldSize),-float(fieldSize)},{0,0,float(fieldSize),float(fieldSize)},{0,0},0,WHITE);
                    EndShaderMode();EndTextureMode();current=1-current;++strokes;
                }
            }
            if(now-lastMass>.25){measureMass();lastMass=now;}
            float resolution[2]={float(GetScreenWidth()),float(GetScreenHeight())},ball[2]={motion.x,motion.y};
            float ballShown=onPlateSurface?0.f:1.f;
            SetShaderValue(renderShader,resLoc,resolution,SHADER_UNIFORM_VEC2);SetShaderValue(renderShader,ballLoc,ball,SHADER_UNIFORM_VEC2);
            SetShaderValue(renderShader,warmLoc,&f.bass,SHADER_UNIFORM_FLOAT);
            SetShaderValue(renderShader,ballShownLoc,&ballShown,SHADER_UNIFORM_FLOAT);
            float bed=bedOf(onPlateSurface);SetShaderValue(renderShader,bedLoc,&bed,SHADER_UNIFORM_FLOAT);
            BeginDrawing();ClearBackground(BLACK);BeginShaderMode(renderShader);
            DrawTexturePro(field[current].texture,{0,0,float(fieldSize),-float(fieldSize)},{0,0,resolution[0],resolution[1]},{0,0},0,WHITE);
            EndShaderMode();
            float ui=std::max(1.f,GetScreenHeight()/1000.f);
            if(showHelp) {
                float alpha=float(std::clamp(1.0-(now-lastTouch-5)*.25,.15,1.0));
                Color ink=Fade({211,202,179,255},alpha);
                text(font,onPlateSurface?"PLATE / ORBITAL DRIFT":"SAND / ORBITAL DRIFT",32*ui,26*ui,16*ui,ink);
                const char* condition=!monitor.connected?"Waiting for music output"
                    :(paused?"Table paused":(f.rms>.0001f?"Listening":"Quiet. The sand remembers."));
                const char* status=condition;
                if(onPlateSurface&&monitor.connected&&f.rms>.0001f)
                    status=TextFormat("%s   %d lobes   %.0f Hz",condition,plate.live.lobes,double(f.toneHz));
                text(font,status,32*ui,51*ui,14*ui,ink);
                // Grain slider: coarse on the left, down to a grain a pixel.
                Rectangle track=grainTrack();
                DrawRectangleRec({track.x,track.y,track.width,track.height},Fade(ink,.22f));
                for(int i=0;i<GrainChoices;++i) {
                    float at=track.x+track.width*float(i)/(GrainChoices-1);
                    DrawRectangleRec({at-1*ui,track.y-3*ui,2*ui,10*ui},Fade(ink,.30f));
                }
                float along=track.x+track.width*float(grainChoice)/(GrainChoices-1);
                DrawRectangleRec({along-3*ui,track.y-8*ui,6*ui,20*ui},ink);
                float grainPixels=2*scale/float(fieldSize);
                text(font,TextFormat("GRAIN   %.2f px",double(grainPixels)),track.x,track.y-30*ui,12*ui,ink);
                centered(font,onPlateSurface
                    ?"SCROLL TO TUNE   /   SPACE PAUSE   /   C CLEAR   /   P FOR THE BALL   /   H HIDE"
                    :"DRAG TO GUIDE   /   SCROLL FOR SPEED   /   SPACE PAUSE   /   C CLEAR   /   P FOR THE PLATE   /   H HIDE",
                    GetScreenWidth()*.5f,GetScreenHeight()-34*ui,13*ui,ink);
                if(options.dev)text(font,"DEV",GetScreenWidth()-63*ui,26*ui,13*ui,ink);
            }
            if(!monitor.error.empty())centered(font,"Cannot hear the output. Press R to reconnect.",GetScreenWidth()*.5f,GetScreenHeight()-59*ui,16*ui,{221,173,114,255});
            EndDrawing();
            if(now-lastState>.2){write(true);lastState=now;}
            if(!options.capture.empty()&&((!captured&&elapsed>=options.captureAfter)||IsKeyPressed(KEY_F2))) {
                fs::create_directories(options.capture.parent_path());Image shot=LoadImageFromScreen();
                bool saved=ExportImage(shot,options.capture.c_str());UnloadImage(shot);
                if(!saved)throw std::runtime_error("Cannot save sand screenshot");
                captured=true;
            }
        }
        monitor.stop();write(false);
        for(auto& f:field)UnloadRenderTexture(f);
        UnloadRenderTexture(gauge);
        UnloadShader(updateShader);UnloadShader(plateShader);UnloadShader(renderShader);UnloadShader(reduceShader);
        UnloadShader(levelShader);UnloadFont(font);CloseWindow();return 0;
    } catch(const std::exception& error) {
        std::cerr<<"[sand] "<<error.what()<<'\n';
        for(auto& f:field)if(f.id)UnloadRenderTexture(f);
        if(gauge.id)UnloadRenderTexture(gauge);
        if(updateShader.id)UnloadShader(updateShader);
        if(plateShader.id)UnloadShader(plateShader);
        if(renderShader.id)UnloadShader(renderShader);
        if(reduceShader.id)UnloadShader(reduceShader);
        if(levelShader.id)UnloadShader(levelShader);
        if(font.texture.id)UnloadFont(font);
        if(windowReady)CloseWindow();
        return 1;
    }
}
