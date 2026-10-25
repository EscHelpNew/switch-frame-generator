// SPDX-License-Identifier: GPL-3.0-or-later
#include <sfg/frame_pacer.hpp>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

int main() {
    for(double fps:{20.0,25.0,30.0}) {
        sfg::FramePacer pacer;
        auto first=pacer.start(0);
        assert(first.tick==0 && first.kind==sfg::SampleKind::real);
        unsigned outputs=1,generated=0,originals=1;
        int64_t previous=0;
        for(int i=1;i<=int(fps*6);++i) {
            auto pair=pacer.push(double(i)/fps);
            assert(pair.generated<=1 && pair.count<=2);
            for(unsigned j=0;j<pair.count;++j) {
                auto s=pair.samples[j];
                assert(s.tick==previous+1); previous=s.tick; ++outputs;
                if(s.kind==sfg::SampleKind::generated) {
                    assert(s.alpha>0 && s.alpha<1); ++generated;
                }
                if(s.kind==sfg::SampleKind::real) ++originals;
            }
        }
        assert(outputs==241);
        assert(originals==unsigned(fps*6)+1);
        assert(generated==240-unsigned(fps*6));
    }
    sfg::FramePacer irregular;
    irregular.start(0);
    double time=0;
    int64_t last=0;
    for(int i=0;i<300;++i) {
        time+=1.0/(20+(i*7)%11);
        auto pair=irregular.push(time);
        assert(pair.generated<=1 && pair.count<=2);
        for(unsigned j=0;j<pair.count;++j) assert(pair.samples[j].tick==++last);
    }
    sfg::FramePacer p;
    p.start(0);
    auto stalled=p.push(0.2);
    assert(stalled.count==8 && stalled.generated==1);
    unsigned repeated=0;
    for(unsigned i=0;i<stalled.count;++i) repeated+=stalled.samples[i].kind==sfg::SampleKind::repeat;
    assert(repeated==6);
    assert(stalled.samples[6].kind==sfg::SampleKind::generated);
    assert(stalled.samples[7].kind==sfg::SampleKind::real);
    assert(sfg::source_sample_time(1.0,3.0)==3.0);
    assert(sfg::source_sample_time(1.0,1.01)==1.0);
    auto cut=p.start(1.01);
    assert(cut.tick==41);
    auto next=p.push(1.06);
    assert(next.count==2 && next.samples[0].tick==42 && next.samples[1].tick==43);
    bool rejected=false;
    try { p.push(1.0); } catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);
    rejected=false;
    try { p.push(std::numeric_limits<double>::quiet_NaN()); } catch(const std::invalid_argument&) { rejected=true; }
    assert(rejected);
    std::cout<<"Pacer: 20/25/30 to40, irregular timestamps, cap1, stalls, reset and invalid timestamps passed\n";
}
