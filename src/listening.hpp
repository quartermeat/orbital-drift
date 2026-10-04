#pragma once
// Individually stateful grains with live desktop audio. The original campaign mixer is not opened.
// A Chladni plate shakes sand into the still lines of whatever is playing.
#include "desktop_monitor.hpp"
#include "particle_sand.hpp"
#include <future>

static int runListening(const Options& options) {
    // Grain size controls how many independent grains cover the window.
    constexpr float GrainSteps[]={1,1.5f,2,2.5f,3,4};
    constexpr int GrainChoices=int(sizeof(GrainSteps)/sizeof(GrainSteps[0]));
    bool windowReady=false;
    Shader grainShader{};Font font{};unsigned grainVao=0,grainVbo=0;
    try {
        std::signal(SIGINT,onSignal);std::signal(SIGTERM,onSignal);
        SetConfigFlags(FLAG_VSYNC_HINT|FLAG_MSAA_4X_HINT|FLAG_WINDOW_RESIZABLE|FLAG_WINDOW_TRANSPARENT);
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
        grainShader=LoadShader((options.assets/"grain-points.vs").c_str(),(options.assets/"grain-points.fs").c_str());
        if(!IsShaderValid(grainShader)||grainShader.id==rlGetShaderIdDefault())
            throw std::runtime_error("Cannot compile grain point shaders");
        grainVao=rlLoadVertexArray();
        if(!grainVao)throw std::runtime_error("Cannot create grain vertex array");
        // Open on the coarse end. A grain a pixel is the finest the tray goes
        // and it reads as dust; a grain a few pixels across is where the figure
        // actually looks like sand, so that is what you get handed.
        int grainChoice=GrainChoices-1,fieldWidth=0,fieldHeight=0;
        auto gridExtent=[&](int pixels){return std::max(2,int(std::ceil(pixels/GrainSteps[grainChoice])));};
        ParticleSand sand;
        uint64_t strokes=0;int clears=0;
        auto clear=[&](){sand.reset(fieldWidth,fieldHeight);};
        auto buildTray=[&]() {
            fieldWidth=gridExtent(GetScreenWidth());fieldHeight=gridExtent(GetScreenHeight());
            clear();
            if(grainVbo)rlUnloadVertexBuffer(grainVbo);
            grainVbo=rlLoadVertexBuffer(nullptr,int(sand.grains.size()*3*sizeof(float)),true);
            if(!grainVbo)throw std::runtime_error("Cannot create grain vertex buffer");
            rlEnableVertexArray(grainVao);rlEnableVertexBuffer(grainVbo);
            rlSetVertexAttribute(0,3,RL_FLOAT,false,3*sizeof(float),0);
            rlEnableVertexAttribute(0);
            rlDisableVertexBuffer();rlDisableVertexArray();
        };
        buildTray();
        DesktopMonitor monitor;monitor.requestedSource=options.monitor;monitor.start();
        PlateDriver plate;
        bool paused=false;
        float sandMass=1,sandSpread=0;
        std::vector<float> vertices;
        int resLoc=GetShaderLocation(grainShader,"resolution"),sizeLoc=GetShaderLocation(grainShader,"grainSize");
        auto measureMass=[&] {
            sand.measure();sandSpread=sand.spread;
            sandMass=float(sand.grains.size())/float(fieldWidth*fieldHeight);
        };
        double started=GetTime(),lastData=started,lastState=0,lastRoute=started,lastTouch=started,lastMass=0;
        double lastSound=started;
        std::future<std::string> routeQuery;
        bool captured=false,showHelp=true,draggingGrain=false,sourcesOpen=false;
        std::vector<DesktopMonitor::SourceOption> sources;
        std::future<std::vector<DesktopMonitor::SourceOption>> sourcesQuery;
        int sourceScroll=0;
        // Controls overlay the sand; they reserve no part of the surface.
        auto grainTrack=[&]{
            float ui=std::max(1.f,GetScreenHeight()/1000.f);
            return Rectangle{32*ui,GetScreenHeight()-78*ui,210*ui,4*ui};
        };
        auto sourceButton=[&]{
            float ui=std::max(1.f,GetScreenHeight()/1000.f);
            return Rectangle{GetScreenWidth()-390*ui,24*ui,355*ui,34*ui};
        };
        auto write=[&](bool running) {
            fs::create_directories(options.state.parent_path());auto temp=options.state;temp+=".tmp";
            std::ofstream out(temp);const auto& f=monitor.analysis.value;
            out<<"{\"app\":\"orbital-drift\",\"version\":\"0.24.0\",\"mode\":\"listening\",\"running\":"<<(running?"true":"false")
               <<",\"renderer\":"<<quote(gpu)<<",\"hardware_accelerated\":true,\"fps\":"<<GetFPS()
               <<",\"fullscreen\":"<<(IsWindowFullscreen()?"true":"false")<<",\"width\":"<<GetScreenWidth()<<",\"height\":"<<GetScreenHeight()
               <<",\"transparent_background\":true"
               <<",\"listening\":{\"connected\":"<<(monitor.connected?"true":"false")<<",\"source\":"<<quote(monitor.source)
               <<",\"error\":"<<quote(monitor.error)<<",\"requested_source\":"<<quote(monitor.requestedSource)
               <<",\"sources_open\":"<<(sourcesOpen?"true":"false")<<",\"available_sources\":[";
            for(size_t i=0;i<sources.size();++i) {
                if(i)out<<',';
                out<<"{\"name\":"<<quote(sources[i].name)<<",\"label\":"<<quote(sources[i].label)
                   <<",\"input\":"<<(sources[i].input?"true":"false")<<'}';
            }
            out<<"]"<<",\"sample_rate\":24000,\"frames\":"<<f.frames
               <<",\"rms\":"<<f.rms<<",\"bass\":"<<f.bass<<",\"body\":"<<f.body<<",\"air\":"<<f.air<<",\"pulse\":"<<f.pulse<<",\"onsets\":"<<f.onsets
               <<",\"tone\":"<<f.tone<<",\"tone_hz\":"<<f.toneHz<<",\"clarity\":"<<f.clarity
               <<",\"surface\":\"plate\",\"field_width\":"<<fieldWidth<<",\"field_height\":"<<fieldHeight
               <<",\"grain_px\":"<<float(GetScreenWidth())/fieldWidth
               <<",\"gpu_relief\":true,\"sand_mass\":"<<sandMass<<",\"sand_spread\":"<<sandSpread
               <<",\"bed\":1,\"stateful_grains\":true,\"grain_count\":"<<sand.grains.size()
               <<",\"moving_grains\":"<<sand.moving<<",\"grain_updates\":"<<sand.grainUpdates
               <<",\"paused\":"<<(paused?"true":"false")<<",\"clears\":"<<clears
               <<",\"strokes\":"<<strokes<<",\"sweeps\":"<<sand.sweeps
               <<",\"mode_n\":"<<plate.live.n<<",\"mode_m\":"<<plate.live.m<<",\"mode_sign\":"<<plate.live.sign
               <<",\"mode_frequency\":"<<plate.live.frequency(float(fieldWidth)/fieldHeight)
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
            if(IsKeyPressed(KEY_R)){
                monitor.start();lastData=now;
                if(sourcesOpen&&!sourcesQuery.valid())sourcesQuery=std::async(std::launch::async,DesktopMonitor::availableSources);
            }
            if(IsKeyPressed(KEY_S)) {
                sourcesOpen=!sourcesOpen;lastTouch=now;
                if(sourcesOpen&&!sourcesQuery.valid())sourcesQuery=std::async(std::launch::async,DesktopMonitor::availableSources);
            }
            if(sourcesQuery.valid()&&sourcesQuery.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
                sources=sourcesQuery.get();sourceScroll=0;
            }
            // Resizing creates one independent grain at each new grid location.
            if(fieldWidth!=gridExtent(GetScreenWidth())||fieldHeight!=gridExtent(GetScreenHeight())) {
                buildTray();plate.clear();strokes=0;lastMass=0;
            }
            if(monitor.requestedSource=="auto"&&!routeQuery.valid()&&now-lastRoute>3) {
                // Only go hunting for a player's own sink once the desktop's
                // output has had nothing on it for a while. Everything you can
                // hear is on that output, so it is the right answer until it
                // demonstrably is not.
                bool hunting=now-lastSound>5;
                routeQuery=std::async(std::launch::async,DesktopMonitor::musicSource,hunting);lastRoute=now;
            }
            if(routeQuery.valid()&&routeQuery.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
                auto source=routeQuery.get();if(monitor.requestedSource=="auto"&&source!=monitor.source){monitor.start(source);lastData=now;}
            }
            if(monitor.poll())lastData=now;
            if(monitor.analysis.value.rms>.0002f)lastSound=now;
            if(now-lastData>1) {
                monitor.connected=false;auto frames=monitor.analysis.value.frames,onsets=monitor.analysis.value.onsets;
                monitor.analysis.value={};monitor.analysis.value.frames=frames;monitor.analysis.value.onsets=onsets;
            }
            if(IsKeyPressed(KEY_C)||IsKeyPressed(KEY_BACKSPACE)) {
                clear();plate.clear();strokes=0;++clears;lastTouch=now;lastMass=0;
            }
            if(IsKeyPressed(KEY_SPACE)){paused=!paused;lastTouch=now;}
            if(IsKeyPressed(KEY_H))showHelp=!showHelp;
            bool sourceClick=false;
            if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&(showHelp||sourcesOpen)) {
                Vector2 at=GetMousePosition();Rectangle button=sourceButton();
                if(CheckCollisionPointRec(at,button)) {
                    sourcesOpen=!sourcesOpen;sourceClick=true;lastTouch=now;
                    if(sourcesOpen&&!sourcesQuery.valid())sourcesQuery=std::async(std::launch::async,DesktopMonitor::availableSources);
                } else if(sourcesOpen) {
                    float ui=std::max(1.f,GetScreenHeight()/1000.f);
                    int shown=std::max(1,std::min(8,int((GetScreenHeight()-120*ui)/(34*ui))));
                    Rectangle menu{button.x,button.y+button.height+4*ui,button.width,float(std::min(shown,int(sources.size())+1))*34*ui};
                    if(CheckCollisionPointRec(at,menu)) {
                        int row=int((at.y-menu.y)/(34*ui)),item=row+sourceScroll;
                        if(item==0||item<=int(sources.size())) {
                            monitor.requestedSource=item==0?"auto":sources[size_t(item-1)].name;
                            monitor.start();lastData=now;lastSound=now;
                            sourcesOpen=false;sourceClick=true;lastTouch=now;
                        }
                    } else sourcesOpen=false;
                }
            }
            // Grain size, dragged. Changing it changes the number of grains.
            {
                float ui=std::max(1.f,GetScreenHeight()/1000.f);
                Rectangle track=grainTrack();
                Rectangle grab{track.x-10*ui,track.y-16*ui,track.width+20*ui,32*ui};
                Vector2 at=GetMousePosition();
                if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&showHelp&&!sourceClick&&CheckCollisionPointRec(at,grab))draggingGrain=true;
                if(!IsMouseButtonDown(MOUSE_BUTTON_LEFT))draggingGrain=false;
                if(draggingGrain) {
                    float along=std::clamp((at.x-track.x)/track.width,0.f,1.f);
                    int choice=int(std::lround(along*(GrainChoices-1)));
                    if(choice!=grainChoice) {
                        grainChoice=choice;buildTray();
                        plate.clear();strokes=0;lastMass=0;
                    }
                    lastTouch=now;
                }
            }
            float wheel=GetMouseWheelMove();
            Rectangle button=sourceButton();Vector2 pointer=GetMousePosition();
            bool overSources=sourcesOpen&&pointer.x>=button.x&&pointer.x<=button.x+button.width
                &&pointer.y>=button.y&&pointer.y<=button.y+button.height+std::min(8,int(sources.size())+1)*34*std::max(1.f,GetScreenHeight()/1000.f);
            if(overSources&&wheel)sourceScroll=std::clamp(sourceScroll-int(wheel),0,std::max(0,int(sources.size())+1-8));
            else plate.tuning=std::clamp(plate.tuning+wheel*.1f,.25f,3.f);
            Vector2 delta=GetMouseDelta();if(delta.x||delta.y||wheel)lastTouch=now;
            const auto& f=monitor.analysis.value;
            plate.paused=paused;
            float frame=GetFrameTime();
            plate.update(f,frame,float(fieldWidth)/fieldHeight);
            sand.update(plate,frame);
            if(plate.agitation>0)++strokes;
            if(now-lastMass>.25){measureMass();lastMass=now;}
            float resolution[2]={float(GetScreenWidth()),float(GetScreenHeight())};
            sand.vertices(vertices,resolution[0],resolution[1]);
            float grainSize=std::max(resolution[0]/fieldWidth,resolution[1]/fieldHeight)*1.18f;
            SetShaderValue(grainShader,resLoc,resolution,SHADER_UNIFORM_VEC2);
            SetShaderValue(grainShader,sizeLoc,&grainSize,SHADER_UNIFORM_FLOAT);
            BeginDrawing();ClearBackground(BLANK);
            rlDrawRenderBatchActive();
            rlUpdateVertexBuffer(grainVbo,vertices.data(),int(vertices.size()*sizeof(float)),0);
            BeginShaderMode(grainShader);rlDrawRenderBatchActive();
            rlEnableShader(grainShader.id);rlEnableVertexArray(grainVao);
            glEnable(GL_PROGRAM_POINT_SIZE);
            glDrawArrays(GL_POINTS,0,GLsizei(sand.grains.size()));
            rlDisableVertexArray();rlDisableShader();
            EndShaderMode();
            float ui=std::max(1.f,GetScreenHeight()/1000.f);
            if(showHelp||sourcesOpen) {
                float alpha=float(std::clamp(1.0-(now-lastTouch-5)*.25,.4,1.0));
                Color ink=Fade({238,231,214,255},alpha);
                auto label=[&](const char* value,float x,float y,float size) {
                    for(Vector2 offset:{Vector2{-ui,0},Vector2{ui,0},Vector2{0,-ui},Vector2{0,ui}})
                        text(font,value,x+offset.x,y+offset.y,size,Fade(BLACK,alpha*.85f));
                    text(font,value,x,y,size,ink);
                };
                label("SOUND TABLE / ORBITAL DRIFT",32*ui,26*ui,16*ui);
                const char* condition=!monitor.connected?"Waiting for music output"
                    :(paused?"Table paused":(f.rms>.0001f?"Listening":"Quiet. The sand remembers."));
                const char* status=condition;
                if(monitor.connected&&f.rms>.0001f)
                    status=TextFormat("%s   %.0f Hz",condition,double(f.toneHz));
                label(status,32*ui,51*ui,14*ui);
                // Grain slider: one pixel on the left, coarser to the right.
                Rectangle track=grainTrack();
                DrawRectangleRec({track.x,track.y,track.width,track.height},Fade(ink,.22f));
                for(int i=0;i<GrainChoices;++i) {
                    float at=track.x+track.width*float(i)/(GrainChoices-1);
                    DrawRectangleRec({at-1*ui,track.y-3*ui,2*ui,10*ui},Fade(ink,.30f));
                }
                float along=track.x+track.width*float(grainChoice)/(GrainChoices-1);
                DrawRectangleRec({along-3*ui,track.y-8*ui,6*ui,20*ui},ink);
                float grainPixels=float(GetScreenWidth())/fieldWidth;
                label(TextFormat("GRAIN   %.2f px",double(grainPixels)),track.x,track.y-30*ui,12*ui);
                const char* controls="SCROLL TO TUNE   /   SPACE PAUSE   /   C CLEAR   /   H HIDE";
                label(controls,(GetScreenWidth()-MeasureTextEx(font,controls,13*ui,.5f).x)*.5f,GetScreenHeight()-34*ui,13*ui);
                if(options.dev)label("DEV",GetScreenWidth()-63*ui,26*ui,13*ui);
                Rectangle button=sourceButton();
                DrawRectangleRec(button,Fade(BLACK,.67f));
                std::string selected="Auto desktop audio";
                if(monitor.requestedSource!="auto") {
                    selected=monitor.requestedSource;
                    for(const auto& source:sources)if(source.name==monitor.requestedSource){selected=source.label;break;}
                }
                while(selected.size()>3&&MeasureTextEx(font,("SOURCE: "+selected).c_str(),13*ui,.5f).x>button.width-14*ui)
                    selected.pop_back();
                label(("SOURCE: "+selected).c_str(),button.x+8*ui,button.y+8*ui,13*ui);
                if(sourcesOpen) {
                    int rows=std::min(8,int(sources.size())+1);
                    for(int row=0;row<rows;++row) {
                        int item=row+sourceScroll;
                        Rectangle line{button.x,button.y+button.height+4*ui+row*34*ui,button.width,34*ui};
                        DrawRectangleRec(line,Fade(BLACK,.82f));
                        const std::string name=item==0?"AUTO: Desktop audio":sources[size_t(item-1)].label;
                        std::string shown=name;
                        while(shown.size()>3&&MeasureTextEx(font,shown.c_str(),13*ui,.5f).x>line.width-16*ui)
                            shown.pop_back();
                        label(shown.c_str(),line.x+8*ui,line.y+8*ui,13*ui);
                    }
                    if(sources.empty())label("S: refresh sources",button.x+8*ui,button.y+button.height+46*ui,12*ui);
                }
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
        if(grainVbo)rlUnloadVertexBuffer(grainVbo);
        if(grainVao)rlUnloadVertexArray(grainVao);
        UnloadShader(grainShader);UnloadFont(font);CloseWindow();return 0;
    } catch(const std::exception& error) {
        std::cerr<<"[sand] "<<error.what()<<'\n';
        if(grainVbo)rlUnloadVertexBuffer(grainVbo);
        if(grainVao)rlUnloadVertexArray(grainVao);
        if(grainShader.id)UnloadShader(grainShader);
        if(font.texture.id)UnloadFont(font);
        if(windowReady)CloseWindow();
        return 1;
    }
}
