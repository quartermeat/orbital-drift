#pragma once
// Cumulative, atomically replaced counters from agent-bridge. The first valid
// read is a baseline: sand represents tokens used while this table is open.
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace orbital {
struct TokenDelta {uint64_t claude=0,codex=0;};

class TokenFeedReader {
    std::filesystem::path path;
    uint64_t priorClaude=0,priorCodex=0;
    bool primed=false;
public:
    bool connected=false;
    explicit TokenFeedReader(std::filesystem::path file):path(std::move(file)){}
    TokenDelta poll() {
        std::error_code status;
        auto modified=std::filesystem::last_write_time(path,status);
        if(status||std::filesystem::file_time_type::clock::now()-modified>std::chrono::seconds(10)) {
            connected=false;return {};
        }
        std::ifstream in(path);
        std::string line;
        if(!std::getline(in,line)||line!="agent-bridge-token-feed\t1") {connected=false;return {};}
        uint64_t claude=0,codex=0;
        bool haveClaude=false,haveCodex=false;
        while(std::getline(in,line)) {
            std::istringstream row(line);
            std::string name,value,extra;
            if(!std::getline(row,name,'\t')||!std::getline(row,value,'\t'))continue;
            if(name!="claude"&&name!="codex")continue;
            if(std::getline(row,extra,'\t')) {connected=false;return {};}
            size_t consumed=0;
            uint64_t total=0;
            if(value.empty()||value[0]=='-') {connected=false;return {};}
            try {total=std::stoull(value,&consumed);} catch(...) {connected=false;return {};}
            if(consumed!=value.size()) {connected=false;return {};}
            if(name=="claude") {claude=total;haveClaude=true;}
            else {codex=total;haveCodex=true;}
        }
        if(!haveClaude||!haveCodex) {connected=false;return {};}
        connected=true;
        TokenDelta delta;
        if(primed) {
            if(claude>=priorClaude)delta.claude=claude-priorClaude;
            if(codex>=priorCodex)delta.codex=codex-priorCodex;
        }
        priorClaude=claude;priorCodex=codex;primed=true;
        return delta;
    }
};
} // namespace orbital
