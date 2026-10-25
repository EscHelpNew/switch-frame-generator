// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Defined after Desktop: producer deadlines are independent of output ticks.
static Mode run_adaptive(Desktop& app,sfg::Scene& scene,Mode mode,Metrics& metrics,
                         double duration,unsigned& total,const fs::path& capture) {
    constexpr double period=0.025,buffer=0.075,submit_lead=0.008,source_guard=0.006;
    constexpr std::array<double,11> source_fps{20,30,22,28,24,26,21,29,23,27,25};
    FrameQueue queue;
    app.window.sync_target(40);
    sfg::FramePacer pacer(40);
    app.reset();
    app.render_rate=app.present_rate=app.generated_rate=app.repeat_rate=0;
    auto epoch=Clock::now(),window=epoch;
    double next_source=0,next_output=buffer,last_sim=0,last_present=0,last_call=0;
    int64_t output_tick=0;
    unsigned source_id=0,output_id=0,renders=0,presents=0,generated=0,repeated=0,computed=0;
    bool first=true,paused=false,slow=false,force=false,captured=false,input_cut=false;
    sfg::Ball previous_ball=scene.ball();
    auto wall=[&] { return std::chrono::duration<double>(Clock::now()-epoch).count(); };
    while(app.window.running) {
        app.window.messages();
        if(!app.window.running) break;
        if(duration>0 && wall()>=duration) { app.window.running=false; break; }
        const unsigned key=app.window.action; app.window.action=0;
        if(key=='V'||key=='A') return Mode::lsfg;
        if(key=='G') return mode==Mode::adaptive40?Mode::variable_off:Mode::adaptive40;
        const double now=wall();
        auto invalidate=[&] {
            queue.clear_pending(); app.reset(); first=true; force=true; input_cut=true;
            next_source=now;
        };
        if(key=='X') { scene.reset(); last_sim=now; invalidate(); }
        if(key=='B'||key==VK_SPACE) { paused=!paused; invalidate(); }
        if(key=='Y') { slow=!slow; invalidate(); }
        if(app.window.clicked) {
            app.window.clicked=false;
            const auto& shown=queue.shown();
            bool hit=false;
            if(shown) {
                const auto& b=shown->ball;
                const double dx=(double(app.window.click_x)/W-b.x)*double(W)/H;
                const double dy=double(app.window.click_y)/H-b.y;
                hit=dx*dx+dy*dy<=b.radius*b.radius;
            }
            if(hit) {
                if(!paused) scene.advance((now-last_sim)*(slow?0.25:1));
                last_sim=now; scene.jump(); paused=false; invalidate();
                std::printf("INPUT CUT: immediate real jump, stale queue discarded\n"); std::fflush(stdout);
            }
        }
        app.window.generator_button(generator_enabled(mode)); app.window.adaptive_mode_button(true);
        app.window.input_buttons(paused,slow);
        // Protect display submission from synchronous render/query work. The
        // lookahead buffer tolerates this bounded producer deferral.
        if((now>=next_source && next_output-submit_lead-now>=source_guard) || force) {
            // The controlled source uses its own scheduled sample time. Actual
            // submission time is separately recorded, never inferred from presents.
            const double logical=force?now:sfg::source_sample_time(next_source,now);
            if(logical>next_source+0.05) {
                queue.clear_pending(); app.reset();
                std::printf("SOURCE STALL: skipped expired samples\n");
            }
            if(!paused) scene.advance(std::max(0.0,logical-last_sim)*(slow?0.25:1));
            last_sim=logical;
            sfg::PairPlan pair;
            if(first) { pair.samples[0]=pacer.start(logical); pair.count=1; }
            else pair=pacer.push(logical);
            double alpha=0.5;
            for(unsigned i=0;i<pair.count;++i) if(pair.samples[i].kind==sfg::SampleKind::generated) alpha=pair.samples[i].alpha;
            const auto submit_start=Clock::now();
            app.scene(scene,mode,alpha);
            const double submit_ms=std::chrono::duration<double,std::milli>(Clock::now()-submit_start).count();
            const auto original=app.textures[app.input_index()];
            const auto previous=app.textures[(app.input_index()+1)%graph::history_image_count];
            for(unsigned i=0;i<pair.count;++i) {
                const auto& sample=pair.samples[i];
                if(sample.tick<output_tick) continue;
                QueuedFrame output{sample.tick,source_id,0,sample.kind,scene.ball()};
                GLuint image=original;
                if(sample.kind==sfg::SampleKind::generated) {
                    if(app.generated_valid) {
                        image=app.textures[app.generated_index];
                        output.ball.x=previous_ball.x+(scene.ball().x-previous_ball.x)*sample.alpha;
                        output.ball.y=previous_ball.y+(scene.ball().y-previous_ball.y)*sample.alpha;
                    } else { image=previous; output.kind=sfg::SampleKind::repeat; output.ball=previous_ball; }
                } else if(sample.kind==sfg::SampleKind::repeat) { image=previous; output.ball=previous_ball; }
                queue.enqueue(image,output);
            }
            if(force) queue.input_cut(original,{output_tick,source_id,0,sfg::SampleKind::real,scene.ball()});
            metrics.source(wall(),source_id,logical,app.scene_ms,app.lsfg_ms,submit_ms,generator_enabled(mode)?1:0,pair.generated);
            if(generator_enabled(mode)) ++computed;
            previous_ball=scene.ball(); app.next(); first=false; force=false;
            ++source_id; ++total; ++renders;
            next_source=logical+1.0/source_fps[(source_id-1)%source_fps.size()];
        }
        const double output_now=wall();
        if(output_now>=next_output-submit_lead) {
            // Skip expired display deadlines instead of bursting old frames.
            if(output_now-next_output>period) {
                const auto missed=int64_t((output_now-next_output)/period);
                next_output+=double(missed)*period; output_tick+=missed;
            }
            auto frame=queue.take(output_tick);
            if(frame) {
                auto dummy=Clock::now();
                auto screenshot=!captured && source_id>=50 ? capture : fs::path{};
                app.present(queue.image(*frame),mode,paused,slow,dummy,screenshot,false);
                if(!screenshot.empty()) captured=true;
                const double displayed=wall();
                const double submitted=std::chrono::duration<double>(app.last_swap_call-epoch).count();
                const double interval=last_present>0?(displayed-last_present)*1000:0;
                const auto kind=frame->kind==sfg::SampleKind::generated?"generated":frame->kind==sfg::SampleKind::real?"real":"repeat";
                metrics.output(displayed,output_id,output_tick,frame->source,kind,interval,
                    (displayed-next_output)*1000,queue.size(),input_cut,submitted,
                    last_call>0?(submitted-last_call)*1000:0,app.swap_block_ms);
                input_cut=false; last_present=displayed;
                last_call=submitted;
                generated+=frame->kind==sfg::SampleKind::generated;
                repeated+=frame->kind==sfg::SampleKind::repeat;
                ++presents; ++output_id;
            }
            next_output+=period; ++output_tick;
        }
        const double elapsed=std::chrono::duration<double>(Clock::now()-window).count();
        if(elapsed>=1) {
            app.render_rate=renders/elapsed; app.present_rate=presents/elapsed;
            app.generated_rate=generated/elapsed; app.repeat_rate=repeated/elapsed;
            char title[256]; std::snprintf(title,sizeof(title),"LSFG | %s | source %.1f | output %.1f | generated %.1f | repeats %.1f | GPU %.2f ms",
                mode_name(mode),app.render_rate,app.present_rate,app.generated_rate,app.repeat_rate,app.lsfg_ms);
            SetWindowTextA(app.window.hwnd,title);
            std::printf("ADAPT mode=%s source=%.2f present_calls=%.2f generated=%.2f repeats=%.2f computed=%u queue=%u peak=%u overflow=%u stale=%u gpu_lsfg_ms=%.3f frame=%u\n",
                mode_name(mode),app.render_rate,app.present_rate,app.generated_rate,app.repeat_rate,computed,
                queue.size(),queue.peak,queue.overflow,queue.stale,app.lsfg_ms,total);
            std::fflush(stdout);
            renders=presents=generated=repeated=computed=0; window=Clock::now();
        }
        precise_wait_until(Clock::now()+std::chrono::milliseconds(1));
    }
    std::printf("ADAPT EXIT source=%u output=%u peak=%u overflow=%u gl_error=%u\n",source_id,output_id,queue.peak,queue.overflow,glGetError());
    std::fflush(stdout);
    return mode;
}
