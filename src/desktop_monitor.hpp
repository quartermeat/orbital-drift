#pragma once
// PulseAudio's monitor sources contain OUTPUT audio. Never fall back to a mic.
// parec is an optional desktop utility, spawned directly without a shell.
#include "listening_audio.hpp"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <chrono>
#include <fcntl.h>
#include <spawn.h>
#include <string>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;

namespace orbital {
class DesktopMonitor {
    pid_t child=-1;
    int output=-1,errors=-1;
    std::array<unsigned char,sizeof(float)> partial{};
    size_t used=0;
public:
    ListenAnalyzer analysis;
    std::string requestedSource="auto",source="@DEFAULT_MONITOR@",error;
    bool connected=false;
    DesktopMonitor()=default;
    DesktopMonitor(const DesktopMonitor&)=delete;
    DesktopMonitor& operator=(const DesktopMonitor&)=delete;
    ~DesktopMonitor(){stop();}
    static std::string list(const char* kind) {
        // A bounded read-only query; never run shell text or change audio routing.
        int fd[2];if(pipe2(fd,O_CLOEXEC)<0)return {};
        posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions,fd[1],STDOUT_FILENO);
        posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
        posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
        const char* args[]={"pactl","list","short",kind,nullptr};pid_t process=-1;
        int result=posix_spawnp(&process,"pactl",&actions,nullptr,const_cast<char* const*>(args),environ);
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
    static std::string route(const std::string& inputs,const std::string& sinks) {
        std::istringstream rows(inputs);std::string line;
        while(std::getline(rows,line)) {
            std::istringstream input(line);int id=-1,sink=-1;input>>id>>sink;
            if(id<0||sink<0)continue;
            std::istringstream outputs(sinks);std::string entry;
            while(std::getline(outputs,entry)) {
                std::istringstream output(entry);int index=-1;std::string name;output>>index>>name;
                if(index==sink&&!name.empty())return name+".monitor";
            }
        }
        return "@DEFAULT_MONITOR@";
    }
    static std::string musicSource(){return route(list("sink-inputs"),list("sinks"));}
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
        output=errors=-1;connected=false;used=0;
    }
    bool start(const std::string& resolved={}) {
        stop();analysis=ListenAnalyzer{};error.clear();
        source=resolved.empty()?(requestedSource=="auto"?musicSource():requestedSource):resolved;
        if(source!="@DEFAULT_MONITOR@"&&!source.ends_with(".monitor")) {
            error="Choose an output monitor, never a microphone.";return false;
        }
        int audioPipe[2],errorPipe[2];
        if(pipe2(audioPipe,O_CLOEXEC)<0){error="Cannot open audio pipe";return false;}
        if(pipe2(errorPipe,O_CLOEXEC)<0){close(audioPipe[0]);close(audioPipe[1]);error="Cannot open error pipe";return false;}
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions,audioPipe[1],STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions,errorPipe[1],STDERR_FILENO);
        posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
        const char* args[]={"parec","--raw","--format=float32le","--rate=24000","--channels=1",
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
                    analysis.push(sample);
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
