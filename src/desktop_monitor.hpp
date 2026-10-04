#pragma once
// PulseAudio's monitor sources contain OUTPUT audio. Never fall back to a mic.
// parec is an optional desktop utility, spawned directly without a shell.
#include "listening_audio.hpp"
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <fcntl.h>
#include <spawn.h>
#include <string>
#include <sstream>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;

namespace orbital {
class DesktopMonitor {
    pid_t child=-1;
    int output=-1,errors=-1;
    std::array<unsigned char,sizeof(float)> partial{};
    size_t used=0;
    float left=0;
    bool haveLeft=false;
public:
    struct SourceOption { std::string name,label;bool input=false; };
    ListenAnalyzer analysis;
    std::string requestedSource="auto",source="@DEFAULT_MONITOR@",error;
    bool connected=false;
    DesktopMonitor()=default;
    DesktopMonitor(const DesktopMonitor&)=delete;
    DesktopMonitor& operator=(const DesktopMonitor&)=delete;
    ~DesktopMonitor(){stop();}
    static std::string list(const char* kind,bool brief) {
        // A bounded read-only query; never run shell text or change audio routing.
        int fd[2];if(pipe2(fd,O_CLOEXEC)<0)return {};
        posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions,fd[1],STDOUT_FILENO);
        posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
        posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
        const char* args[]={"pactl","list","short",kind,nullptr};
        const char* full[]={"pactl","list",kind,nullptr};
        pid_t process=-1;
        int result=posix_spawnp(&process,"pactl",&actions,nullptr,const_cast<char* const*>(brief?args:full),environ);
        posix_spawn_file_actions_destroy(&actions);close(fd[1]);
        if(result){close(fd[0]);return {};}
        fcntl(fd[0],F_SETFL,O_NONBLOCK);std::string text;char buffer[4096];
        auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(400);
        bool ended=false;
        while(std::chrono::steady_clock::now()<deadline) {
            ssize_t n=read(fd[0],buffer,sizeof(buffer));
            if(n>0){text.append(buffer,size_t(n));if(text.size()>65536)break;}
            else if(n==0){ended=true;break;}
            else if(errno==EAGAIN||errno==EINTR)usleep(2000);
            else break;
        }
        close(fd[0]);
        if(!ended)kill(process,SIGKILL);
        int status=0;while(waitpid(process,&status,0)<0&&errno==EINTR){}
        return ended&&WIFEXITED(status)&&WEXITSTATUS(status)==0?text:std::string{};
    }
    // Whichever sink is actually playing something. A paused stream is still a
    // stream, so a corked or muted one never wins: it is the difference between
    // following what you are listening to and following what you listened to
    // first. `inputs` is the long `pactl list sink-inputs` form, because the
    // short one does not say whether a stream is paused.
    static std::string route(const std::string& inputs,const std::string& sinks) {
        size_t at=inputs.find("Sink Input #");
        while(at!=std::string::npos) {
            size_t next=inputs.find("Sink Input #",at+1);
            std::string block=inputs.substr(at,next==std::string::npos?std::string::npos:next-at);
            at=next;
            auto field=[&block](const char* key)->std::string {
                size_t found=block.find(key);
                if(found==std::string::npos)return {};
                size_t start=found+std::strlen(key);
                size_t end=block.find('\n',start);
                std::string value=block.substr(start,end==std::string::npos?std::string::npos:end-start);
                size_t first=value.find_first_not_of(" \t");
                return first==std::string::npos?std::string{}:value.substr(first);
            };
            if(field("Corked:").rfind("yes",0)==0)continue;
            if(field("Mute:").rfind("yes",0)==0)continue;
            std::string sinkField=field("Sink:");
            if(sinkField.empty())continue;
            int sink=std::atoi(sinkField.c_str());
            std::istringstream outputs(sinks);std::string entry;
            while(std::getline(outputs,entry)) {
                std::istringstream output(entry);int index=-1;std::string name;output>>index>>name;
                if(index==sink&&!name.empty())return name+".monitor";
            }
        }
        return "@DEFAULT_MONITOR@";
    }
    // The desktop's own output carries everything you can hear, whichever
    // program made it, so that is the default. Only when nothing reaches it --
    // a player routed through its own sink, say -- is it worth hunting for the
    // stream that is actually sounding.
    static std::string musicSource(bool hunting) {
        if(!hunting)return "@DEFAULT_MONITOR@";
        return route(list("sink-inputs",false),list("sinks",true));
    }
    static std::vector<SourceOption> parseSources(const std::string& listing) {
        std::vector<SourceOption> choices;
        size_t at=listing.find("Source #");
        while(at!=std::string::npos) {
            size_t next=listing.find("Source #",at+1);
            std::string block=listing.substr(at,next==std::string::npos?std::string::npos:next-at);
            at=next;
            auto field=[&](const char* key) {
                size_t found=block.find(std::string("\n\t")+key);
                if(found==std::string::npos)return std::string{};
                size_t start=found+std::strlen(key)+2;
                size_t end=block.find('\n',start);
                return block.substr(start,end==std::string::npos?std::string::npos:end-start);
            };
            std::string name=field("Name: "),description=field("Description: ");
            if(name.empty())continue;
            bool input=field("Monitor of Sink: ")=="n/a";
            if(description.empty())description=name;
            choices.push_back({name,(input?"INPUT: ":"OUTPUT: ")+description,input});
        }
        return choices;
    }
    static std::vector<SourceOption> availableSources() { return parseSources(list("sources",false)); }
    void stop() {
        if(child>0) {
            kill(child,SIGTERM);
            int status=0;
            for(int i=0;i<20;++i) {
                pid_t result=waitpid(child,&status,WNOHANG);
                if(result==child||(result<0&&errno==ECHILD)){child=-1;break;}
                usleep(5000);
            }
            if(child>0){kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}child=-1;}
        }
        if(output>=0)close(output);
        if(errors>=0)close(errors);
        output=errors=-1;connected=false;used=0;haveLeft=false;left=0;
    }
    bool start(const std::string& resolved={}) {
        stop();analysis=ListenAnalyzer{};error.clear();
        source=resolved.empty()?(requestedSource=="auto"?musicSource(false):requestedSource):resolved;
        if(source.empty()){error="Choose an audio source.";return false;}
        int audioPipe[2],errorPipe[2];
        if(pipe2(audioPipe,O_CLOEXEC)<0){error="Cannot open audio pipe";return false;}
        if(pipe2(errorPipe,O_CLOEXEC)<0){close(audioPipe[0]);close(audioPipe[1]);error="Cannot open error pipe";return false;}
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions,audioPipe[1],STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions,errorPipe[1],STDERR_FILENO);
        posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
        // Two channels, folded to one here. Asking parec for mono instead makes
        // it average every channel the sink has, and on a 7.1 output that
        // divides the music by the four silent surrounds -- which is how the
        // desktop's own output once got written off as silent.
        const char* args[]={"parec","--raw","--format=float32le","--rate=24000","--channels=2",
            "--latency-msec=30","--process-time-msec=10","--client-name=Orbital Drift Sand Table",
            "--stream-name=Desktop music (output monitor only)","--device",source.c_str(),nullptr};
        int result=posix_spawnp(&child,"parec",&actions,nullptr,const_cast<char* const*>(args),environ);
        posix_spawn_file_actions_destroy(&actions);
        close(audioPipe[1]);close(errorPipe[1]);
        if(result!=0) {
            close(audioPipe[0]);close(errorPipe[0]);child=-1;
            error="Desktop listener unavailable: install pulseaudio-utils (parec).";return false;
        }
        output=audioPipe[0];errors=errorPipe[0];
        fcntl(output,F_SETFL,O_NONBLOCK);fcntl(errors,F_SETFL,O_NONBLOCK);
        return true;
    }
    bool poll() {
        bool received=false;
        std::array<unsigned char,8192> bytes{};
        for(int pass=0;pass<16&&output>=0;++pass) {
            ssize_t n=read(output,bytes.data(),bytes.size());
            if(n<=0)break;
            connected=true;received=true;
            for(ssize_t i=0;i<n;++i) {
                partial[used++]=bytes[size_t(i)];
                if(used==sizeof(float)) {
                    float sample;std::memcpy(&sample,partial.data(),sizeof(sample));used=0;
                    if(!haveLeft){left=sample;haveLeft=true;continue;}
                    haveLeft=false;
                    analysis.push((left+sample)*.5f);
                }
            }
        }
        analysis.settle();
        if(errors>=0) {
            char message[512];ssize_t n=read(errors,message,sizeof(message));
            if(n>0) {
                for(ssize_t i=0;i<n&&error.size()<512;++i)
                    if(message[i]>=32&&message[i]<127)error+=message[i];
            }
        }
        if(child>0) {
            int status;pid_t result=waitpid(child,&status,WNOHANG);
            if(result==child) {
                child=-1;connected=false;
                if(error.empty())error="Desktop audio disconnected. Press R to reconnect.";
            }
        }
        return received;
    }
};
} // namespace orbital
