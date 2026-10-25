// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace sfg {
enum class SampleKind { real, generated, repeat };
struct OutputSample { int64_t tick{}; double alpha{1}; SampleKind kind{SampleKind::real}; };
struct PairPlan {
    std::array<OutputSample,16> samples{};
    unsigned count{},generated{};
    uint64_t dropped{};
};
inline double source_sample_time(double scheduled,double now) {
    if(!std::isfinite(scheduled)||!std::isfinite(now)||scheduled<0||now<0)
        throw std::invalid_argument("Invalid source deadline");
    return now-scheduled>0.05?now:scheduled;
}

// Real images are shown at ceil(source time * target) on the output grid.
// Earlier slots use at most ONE interpolated image; excess slots repeat.
class FramePacer {
public:
    explicit FramePacer(double target=40):target_(target) {
        if(!std::isfinite(target)||target<=0) throw std::invalid_argument("Invalid target FPS");
    }
    OutputSample start(double time) {
        const auto tick=ceil_tick(time);
        previous_=time; next_=tick+1; started_=true;
        return {tick,1,SampleKind::real};
    }
    PairPlan push(double time) {
        if(!started_ || !std::isfinite(time) || time<=previous_)
            throw std::invalid_argument("Source timestamps must increase");
        PairPlan result;
        const auto end=ceil_tick(time);
        if(end<next_) { previous_=time; return result; }
        const auto total=uint64_t(end-next_+1);
        if(total>result.samples.size()) {
            result.dropped=total-result.samples.size();
            next_+=int64_t(result.dropped);
        }
        result.count=unsigned(end-next_+1);
        int64_t generated_tick=-1;
        if(result.count>1) {
            // Place the sole generated image immediately before the real one;
            // earlier overflow slots may repeat A, never return to A after FG.
            generated_tick=end-1;
            result.generated=1;
        }
        for(unsigned i=0;i<result.count;++i) {
            const int64_t tick=next_+i;
            const auto kind=tick==end?SampleKind::real:tick==generated_tick?SampleKind::generated:SampleKind::repeat;
            const double alpha=kind==SampleKind::real?1.0:(double(tick)/target_-previous_)/(time-previous_);
            result.samples[i]={tick,std::clamp(alpha,0.0,1.0),kind};
        }
        next_=end+1; previous_=time;
        return result;
    }
private:
    int64_t ceil_tick(double time) const {
        if(!std::isfinite(time)||time<0||time*target_>double(std::numeric_limits<int64_t>::max()/2))
            throw std::invalid_argument("Invalid source timestamp");
        return int64_t(std::ceil(time*target_-1e-8));
    }
    double target_,previous_{};
    int64_t next_{};
    bool started_{};
};
}
