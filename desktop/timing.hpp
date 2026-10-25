// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <chrono>
#include <stdexcept>

class PreciseTimer {
public:
    PreciseTimer() {
        handle_=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_ALL_ACCESS);
        if(!handle_) throw std::runtime_error("High-resolution waitable timer unavailable");
    }
    ~PreciseTimer() { CloseHandle(handle_); }
    void until(std::chrono::steady_clock::time_point deadline) {
        const auto remaining=deadline-std::chrono::steady_clock::now();
        const auto ticks=std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count()/100;
        if(ticks<=0) return;
        LARGE_INTEGER due{}; due.QuadPart=-ticks;
        if(!SetWaitableTimer(handle_,&due,0,nullptr,nullptr,FALSE) || WaitForSingleObject(handle_,INFINITE)!=WAIT_OBJECT_0)
            throw std::runtime_error("High-resolution timer wait failed");
    }
private:
    HANDLE handle_{};
};
inline void precise_wait_until(std::chrono::steady_clock::time_point deadline) {
    static thread_local PreciseTimer timer;
    timer.until(deadline);
}
