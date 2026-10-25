// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gl_api.hpp"
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <iomanip>

inline double process_cpu_ms() {
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))
        throw std::runtime_error("GetProcessTimes failed");
    ULARGE_INTEGER k{},u{};
    k.LowPart=kernel.dwLowDateTime; k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime; u.HighPart=user.dwHighDateTime;
    return double(k.QuadPart+u.QuadPart)/10000.0;
}
inline uint64_t working_set_bytes() {
    PROCESS_MEMORY_COUNTERS_EX memory{};
    if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)))
        throw std::runtime_error("GetProcessMemoryInfo failed");
    return memory.WorkingSetSize;
}
class Metrics {
public:
    explicit Metrics(const std::filesystem::path& prefix) {
        if(prefix.empty()) return;
        source_.open(prefix.string()+"-source.csv"); output_.open(prefix.string()+"-present.csv");
        if(!source_||!output_) throw std::runtime_error("Cannot create profiling CSV files");
        source_<<"wall_s,source_id,logical_s,scene_gpu_ms,lsfg_gpu_ms,submit_wall_ms,process_cpu_ms,rss_bytes,generated_computed,generated_planned\n";
        output_<<"wall_s,output_id,logical_tick,source_id,kind,interval_ms,lateness_ms,queue_depth,input_cut,swap_submit_s,submit_interval_ms,swap_block_ms\n";
        source_<<std::fixed<<std::setprecision(6); output_<<std::fixed<<std::setprecision(6);
    }
    void source(double wall,unsigned id,double logical,double scene,double fg,double submission,
                unsigned computed,unsigned planned) {
        if(!source_.is_open()) return;
        source_<<wall<<','<<id<<','<<logical<<','<<scene<<','<<fg<<','<<submission<<','<<process_cpu_ms()<<','
            <<working_set_bytes()<<','<<computed<<','<<planned<<'\n';
    }
    void output(double wall,unsigned id,int64_t tick,unsigned source,const char* kind,double interval,
                double late,unsigned queue,bool input_cut,double submit=0,double submit_interval=0,double swap_block=0) {
        if(!output_.is_open()) return;
        output_<<wall<<','<<id<<','<<tick<<','<<source<<','<<kind<<','<<interval<<','<<late<<','<<queue<<','<<input_cut<<','<<submit<<','<<submit_interval<<','<<swap_block<<'\n';
    }
private:
    std::ofstream source_,output_;
};
