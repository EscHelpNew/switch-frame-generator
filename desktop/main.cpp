// SPDX-License-Identifier: GPL-3.0-or-later
// The same GLSL/graph as the Switch pipeline, executed by the PC OpenGL driver.
#include "gl_api.hpp"
#include "frame_queue.hpp"
#include "metrics.hpp"
#include "timing.hpp"
#include <sfg/scene.hpp>
#include <lsfg/backend/binding.hpp>
#include <lsfg/backend/cache_load.hpp>
#include <lsfg/backend/schedule.hpp>
#include <lsfg/common/cache_store.hpp>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <cstring>
#include <mmsystem.h>

extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement=1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance=1;
}
namespace fs=std::filesystem;
namespace backend=lsfg::backend;
namespace graph=lsfg::graph;
using Clock=std::chrono::steady_clock;
constexpr unsigned W=1280,H=720;

static std::string text_file(const fs::path& path) {
    std::ifstream f(path,std::ios::binary);
    if(!f) throw std::runtime_error("Missing file: "+path.string());
    std::ostringstream s; s<<f.rdbuf(); return s.str();
}
static void ok(lsfg::ErrorCode code,const char* operation) {
    if(!lsfg::succeeded(code)) throw std::runtime_error(std::string(operation)+": "+std::string(lsfg::error_name(code)));
}
static GLenum format(graph::Format f) {
    return f==graph::Format::r8 ? GL_R8 : f==graph::Format::rgba16f ? GL_RGBA16F : GL_RGBA8;
}
static GLuint shader(GLenum stage,const std::string& source) {
    auto handle=glCreateShader(stage); const char* p=source.c_str();
    glShaderSource(handle,1,&p,nullptr); glCompileShader(handle);
    GLint success=0; glGetShaderiv(handle,GL_COMPILE_STATUS,&success);
    if(!success) {
        GLint length=0; glGetShaderiv(handle,GL_INFO_LOG_LENGTH,&length);
        std::string log(size_t(length)+1,'\0'); glGetShaderInfoLog(handle,GLsizei(log.size()),nullptr,log.data());
        glDeleteShader(handle); throw std::runtime_error("GLSL compilation: "+log);
    }
    return handle;
}
static GLuint program(const std::vector<std::pair<GLenum,std::string>>& stages) {
    GLuint p=glCreateProgram(); std::vector<GLuint> shaders;
    try {
        for(const auto& [stage,source]:stages) { shaders.push_back(shader(stage,source)); glAttachShader(p,shaders.back()); }
        glLinkProgram(p); GLint success=0; glGetProgramiv(p,GL_LINK_STATUS,&success);
        if(!success) {
            GLint length=0; glGetProgramiv(p,GL_INFO_LOG_LENGTH,&length);
            std::string log(size_t(length)+1,'\0'); glGetProgramInfoLog(p,GLsizei(log.size()),nullptr,log.data());
            throw std::runtime_error("GLSL linking: "+log);
        }
        for(auto s:shaders) glDeleteShader(s);
        return p;
    } catch(...) { for(auto s:shaders) glDeleteShader(s); glDeleteProgram(p); throw; }
}
static std::string desktop_glsl(std::string source,bool fragment=false) {
    if(source.starts_with("//")) { auto p=source.find("#version 460"); if(p!=std::string::npos) source.replace(p,12,"#version 450"); }
    if(fragment) {
        auto version=source.find("#version");
        auto end=source.find('\n',version);
        if(end==std::string::npos) throw std::runtime_error("GLSL version line missing");
        source.insert(end+1,"layout(origin_upper_left) in vec4 gl_FragCoord;\n");
    }
    return source;
}
enum class Mode { real30,lsfg,native60,adaptive40,variable_off };
static bool adaptive_mode(Mode mode) { return mode==Mode::adaptive40||mode==Mode::variable_off; }
static bool generator_enabled(Mode mode) { return mode==Mode::lsfg||mode==Mode::adaptive40; }
static const char* mode_name(Mode mode) {
    return mode==Mode::adaptive40?"AUTO 20-30>40":mode==Mode::variable_off?"VARIABLE FG OFF":
        mode==Mode::lsfg?"LSFG 30>60":mode==Mode::real30?"REAL 30":"NATIVE 60";
}

class Desktop {
public:
    Window window; // GL context must outlive all objects below.
    lsfg::cache::Loaded cache;
    backend::Plan plan;
    backend::DescriptorLayout descriptors;
    backend::Schedule schedule;
    std::vector<GLuint> textures,programs,uniforms;
    std::array<GLuint,4> samplers{};
    std::array<GLuint,3> queries{};
    GLuint fbo{},vao{},scene_program{},hud_program{},scene_uniform{},hud_uniform{};
    unsigned phase{},warmup{},generated_index{};
    double scene_ms{},lsfg_ms{},render_rate{},present_rate{},generated_rate{};
    bool generated_valid{};
    uint64_t texture_bytes{};
    double repeat_rate{};
    Clock::time_point last_swap_call{};
    double swap_block_ms{};

    Desktop(const fs::path& root) {
        window.show();
        try { initialize(root); } catch(...) { release(); throw; }
    }
    ~Desktop() { release(); }
    void initialize(const fs::path& root) {
        std::string key=text_file(root/"build/cache/selected.txt");
        key.erase(key.find_last_not_of("\r\n \t")+1);
        if(key.size()!=64 || key.find_first_not_of("0123456789abcdef")!=std::string::npos)
            throw std::runtime_error("Invalid cache selection");
        auto directory=root/"build/cache"/key;
        ok(lsfg::cache::read(directory.string(),cache),"cache read");
        backend::Request request{.config=cache.graph.config,.output={W,H},.memory_budget_bytes=192ull*1024*1024};
        backend::Rejection why;
        if(!backend::accept(cache,request,plan,why)) throw std::runtime_error(std::string(why.reason));
        ok(backend::describe(cache.graph,descriptors),"descriptors");
        ok(backend::schedule(cache.graph,schedule),"schedule");
        for(size_t i=0;i<cache.passes.size();++i) {
            char name[32]; std::snprintf(name,sizeof(name),"%02u.comp",unsigned(i));
            programs.push_back(program({{GL_COMPUTE_SHADER,text_file(directory/"glsl"/name)}}));
            std::printf("GLSL %s compiled\n",name); std::fflush(stdout);
        }
        textures.resize(plan.images.size()); glGenTextures(GLsizei(textures.size()),textures.data());
        bool found_generated=false;
        const std::array<float,4> zero{};
        uint64_t total_bytes=0;
        for(size_t i=0;i<textures.size();++i) {
            const auto& image=plan.images[i];
            glBindTexture(GL_TEXTURE_2D,textures[i]);
            glTexStorage2D(GL_TEXTURE_2D,1,format(image.format),image.extent.width,image.extent.height);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
            glClearTexImage(textures[i],0,image.format==graph::Format::r8?GL_RED:GL_RGBA,GL_FLOAT,zero.data());
            if(image.role==graph::ImageRole::generated && !found_generated) { generated_index=unsigned(i); found_generated=true; }
            total_bytes+=image.bytes;
        }
        if(!found_generated) throw std::runtime_error("No generated image in graph");
        texture_bytes=total_bytes;
        glGenSamplers(4,samplers.data());
        for(unsigned i=0;i<4;++i) {
            glSamplerParameteri(samplers[i],GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glSamplerParameteri(samplers[i],GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            bool edge=i==unsigned(graph::Sampler::edge)||i==backend::introduced_sampler;
            glSamplerParameteri(samplers[i],GL_TEXTURE_WRAP_S,edge?GL_CLAMP_TO_EDGE:GL_CLAMP_TO_BORDER);
            glSamplerParameteri(samplers[i],GL_TEXTURE_WRAP_T,edge?GL_CLAMP_TO_EDGE:GL_CLAMP_TO_BORDER);
            std::array<float,4> border{};
            if(i==unsigned(graph::Sampler::border_white)) border.fill(1);
            glSamplerParameterfv(samplers[i],GL_TEXTURE_BORDER_COLOR,border.data());
        }
        uniforms.resize(plan.uniform_buffers); glGenBuffers(GLsizei(uniforms.size()),uniforms.data());
        for(unsigned i=0;i<uniforms.size();++i) {
            std::array<uint8_t,256> data{};
            auto constants=graph::constant_buffer(i,cache.graph.config);
            std::memcpy(data.data(),&constants,sizeof(constants));
            glBindBuffer(GL_UNIFORM_BUFFER,uniforms[i]); glBufferData(GL_UNIFORM_BUFFER,256,data.data(),GL_STATIC_DRAW);
        }
        glGenFramebuffers(1,&fbo); glGenVertexArrays(1,&vao); glBindVertexArray(vao);
        auto vertex=desktop_glsl(text_file(root/"shaders/fullscreen.vert"));
        // Texture row zero must represent the logical top, as on Switch.
        // Keep default GL fragment coordinates here; reverse Y only on display.
        scene_program=program({{GL_VERTEX_SHADER,vertex},{GL_FRAGMENT_SHADER,desktop_glsl(text_file(root/"shaders/scene.frag"))}});
        hud_program=program({{GL_VERTEX_SHADER,vertex},{GL_FRAGMENT_SHADER,desktop_glsl(text_file(root/"shaders/hud.frag"),true)}});
        glGenBuffers(1,&scene_uniform); glBindBuffer(GL_UNIFORM_BUFFER,scene_uniform); glBufferData(GL_UNIFORM_BUFFER,32,nullptr,GL_DYNAMIC_DRAW);
        glGenBuffers(1,&hud_uniform); glBindBuffer(GL_UNIFORM_BUFFER,hud_uniform); glBufferData(GL_UNIFORM_BUFFER,1280,nullptr,GL_DYNAMIC_DRAW);
        glGenQueries(3,queries.data()); glViewport(0,0,W,H);
        check_gl("initialization");
        std::printf("READY native1280x720 passes=%zu images=%zu image_bytes=%llu warmup=%u\n",
            programs.size(),textures.size(),static_cast<unsigned long long>(total_bytes),schedule.warmup_frames);
        std::fflush(stdout);
    }
    void reset() { warmup=0; }
    unsigned input_index() const { return phase%graph::history_image_count; }
    void scene(const sfg::Scene& scene,Mode mode,double alpha=0.5) {
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[input_index()],0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("Scene framebuffer incomplete");
        const auto& b=scene.ball();
        const float values[]{float(b.x),float(b.y),float(b.radius),float(scene.time()),float(W),float(H),float(sfg::Scene::floor_y),0};
        glBindBuffer(GL_UNIFORM_BUFFER,scene_uniform); glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(values),values);
        glBindBufferBase(GL_UNIFORM_BUFFER,0,scene_uniform); glUseProgram(scene_program);
        glQueryCounter(queries[0],GL_TIMESTAMP);
        glDrawArrays(GL_TRIANGLES,0,3); glMemoryBarrier(GL_ALL_BARRIER_BITS);
        glQueryCounter(queries[1],GL_TIMESTAMP);
        generated_valid=false;
        if(generator_enabled(mode)) {
            auto constants=graph::constant_buffer(1,cache.graph.config);
            constants.timestamp=float(std::clamp(alpha,0.001,0.999));
            glBindBuffer(GL_UNIFORM_BUFFER,uniforms[1]);
            glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(constants),&constants);
            for(unsigned i=0;i<cache.graph.dispatches.size();++i) {
                backend::DispatchBinding b;
                ok(backend::bind(cache,plan,descriptors,i,phase,b),"dispatch binding");
                glUseProgram(programs[b.pass]);
                for(unsigned j=0;j<b.texture_count;++j) {
                    const auto& t=b.textures[j]; glActiveTexture(GL_TEXTURE0+t.slot);
                    glBindTexture(GL_TEXTURE_2D,textures[t.image]); glBindSampler(t.slot,samplers[t.sampler]);
                }
                for(unsigned j=0;j<b.storage_count;++j) {
                    const auto& t=b.storages[j];
                    glBindImageTexture(t.slot,textures[t.image],0,GL_FALSE,0,GL_READ_WRITE,format(plan.images[t.image].format));
                }
                if(b.uniform_slot!=backend::no_slot) glBindBufferBase(GL_UNIFORM_BUFFER,b.uniform_slot,uniforms[b.uniform_buffer]);
                glDispatchCompute(b.groups_x,b.groups_y,1); glMemoryBarrier(GL_ALL_BARRIER_BITS);
            }
            if(warmup<schedule.warmup_frames) ++warmup; else generated_valid=true;
        }
        glQueryCounter(queries[2],GL_TIMESTAMP);
        glFinish(); // Conservative desktop verification path, no cross-frame overlap.
        std::array<GLuint64,3> ts{};
        for(unsigned i=0;i<3;++i) glGetQueryObjectui64v(queries[i],GL_QUERY_RESULT,&ts[i]);
        scene_ms=double(ts[1]-ts[0])/1e6; lsfg_ms=generator_enabled(mode)?double(ts[2]-ts[1])/1e6:0;
        check_gl("scene and LSFG compute");
    }
    void present(GLuint image,Mode mode,bool paused,bool slow,Clock::time_point& deadline,
                 const fs::path& capture={},bool pace=true) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,image,0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
        if(!capture.empty()) {
            std::vector<uint8_t> pixels(W*H*4); glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            auto raw=capture.parent_path()/"desktop-compute.rgba";
            std::ofstream file(raw,std::ios::binary); file.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
            if(!file) throw std::runtime_error("Compute capture write failed");
        }
        glBlitFramebuffer(0,H,W,0,0,0,W,H,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        std::array<uint32_t,320> text{}; char rows[5][128]{};
        std::snprintf(rows[0],128,"LSFG DESKTOP  %s  1280X720  %s %s",mode_name(mode),paused?"PAUSE":"",slow?"SLOW":"");
        std::snprintf(rows[1],128,"RENDER %.1F  PRESENT CALLS %.1F  GENERATED %.1F",render_rate,present_rate,generated_rate);
        std::snprintf(rows[2],128,"GPU SCENE %.2F MS  LSFG %.2F MS",scene_ms,lsfg_ms);
        std::snprintf(rows[3],128,"BUTTON OR G: LSFG ON/OFF  CLICK BALL: JUMP  ESC EXIT");
        if(adaptive_mode(mode)) std::snprintf(rows[4],128,"V VARIABLE  TARGET 40  CAP 1  BUFFER 75MS  REPEATS %.1F",repeat_rate);
        else std::snprintf(rows[4],128,"V 20-30>40  A MODE  X RESET  B PAUSE  Y SLOW  FLOW 1/4");
        for(unsigned y=0;y<5;++y) for(unsigned x=0;x<64 && rows[y][x];++x) text[y*64+x]=uint32_t(rows[y][x]);
        glUseProgram(hud_program); glBindBuffer(GL_UNIFORM_BUFFER,hud_uniform);
        glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(text),text.data()); glBindBufferBase(GL_UNIFORM_BUFFER,0,hud_uniform);
        glDrawArrays(GL_TRIANGLES,0,3);
        if(!capture.empty()) {
            std::vector<uint8_t> pixels(W*H*4); glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            std::ofstream file(capture,std::ios::binary); file.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
            if(!file) throw std::runtime_error("Capture write failed");
            std::printf("CAPTURE %s\n",capture.string().c_str());
        }
        check_gl("presentation");
        if(pace) {
            deadline+=std::chrono::nanoseconds(16666667);
            const auto now=Clock::now(); if(deadline<now-std::chrono::milliseconds(100)) deadline=now;
            precise_wait_until(deadline);
        }
        last_swap_call=Clock::now();
        if(!SwapBuffers(window.dc)) throw std::runtime_error("SwapBuffers failed");
        swap_block_ms=std::chrono::duration<double,std::milli>(Clock::now()-last_swap_call).count();
    }
    void next() { phase=(phase+1)%schedule.cycle; }
private:
    void release() {
        if(!glFinish) return;
        glFinish();
        for(auto p:programs) glDeleteProgram(p);
        glDeleteProgram(scene_program); glDeleteProgram(hud_program);
        glDeleteTextures(GLsizei(textures.size()),textures.data());
        glDeleteBuffers(GLsizei(uniforms.size()),uniforms.data());
        glDeleteBuffers(1,&scene_uniform); glDeleteBuffers(1,&hud_uniform);
        glDeleteFramebuffers(1,&fbo); glDeleteVertexArrays(1,&vao);
        glDeleteSamplers(4,samplers.data()); glDeleteQueries(3,queries.data());
    }
};

#include "adaptive_loop.hpp"

int main(int argc,char** argv) {
    try {
        struct TimerResolution {
            TimerResolution() { timeBeginPeriod(1); }
            ~TimerResolution() { timeEndPeriod(1); }
        } timer_resolution;
        wchar_t executable[MAX_PATH]; GetModuleFileNameW(nullptr,executable,MAX_PATH);
        fs::path root=fs::path(executable).parent_path().parent_path().parent_path();
        bool smoke=false; fs::path capture=root/"build/desktop-capture.rgba";
        Mode mode=Mode::adaptive40;
        double duration=0;
        std::string benchmark;
        bool explicit_mode=false;
        for(int i=1;i<argc;++i) {
            if(std::strcmp(argv[i],"--smoke")==0) smoke=true;
            else if(std::strncmp(argv[i],"--root=",7)==0) root=argv[i]+7;
            else if(std::strncmp(argv[i],"--seconds=",10)==0) duration=std::stod(argv[i]+10);
            else if(std::strncmp(argv[i],"--bench=",8)==0) benchmark=argv[i]+8;
            else if(std::strncmp(argv[i],"--mode=",7)==0) {
                explicit_mode=true; const std::string value=argv[i]+7;
                if(value=="adaptive40") mode=Mode::adaptive40;
                else if(value=="variable-off") mode=Mode::variable_off;
                else if(value=="lsfg") mode=Mode::lsfg;
                else if(value=="real30") mode=Mode::real30;
                else if(value=="native60") mode=Mode::native60;
                else throw std::runtime_error("Unknown mode");
            }
        }
        if(smoke&&!explicit_mode) mode=Mode::lsfg;
        if(smoke&&adaptive_mode(mode)&&duration<=0) duration=10;
        if(benchmark.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos)
            throw std::runtime_error("Invalid benchmark name");
        Desktop app(root); sfg::Scene scene;
        if(adaptive_mode(mode)) app.window.sync_target(40);
        Metrics metrics(benchmark.empty()?fs::path{}:root/"build"/("perf-"+benchmark));
        if(!benchmark.empty()) {
            SYSTEM_INFO cpu{}; GetSystemInfo(&cpu);
            DEVMODEA display{}; display.dmSize=sizeof(display); EnumDisplaySettingsA(nullptr,ENUM_CURRENT_SETTINGS,&display);
            std::ofstream metadata(root/"build"/("perf-"+benchmark+"-meta.json"));
            metadata<<"{\n\"mode\":"<<std::quoted(mode_name(mode))<<",\n\"gpu\":"<<std::quoted(reinterpret_cast<const char*>(glGetString(GL_RENDERER)))
                <<",\n\"gl\":"<<std::quoted(reinterpret_cast<const char*>(glGetString(GL_VERSION)))
                <<",\n\"texture_bytes\":"<<app.texture_bytes<<",\n\"queue_texture_bytes\":"<<(adaptive_mode(mode)?FrameQueue::image_bytes:0)
                <<",\n\"logical_processors\":"<<cpu.dwNumberOfProcessors<<",\n\"display_hz\":"<<display.dmDisplayFrequency
                <<",\n\"wgl_swap_interval\":"<<app.window.swap_interval
                <<",\n\"target_fps\":"<<(adaptive_mode(mode)?40:60)<<",\n\"cap_generated_per_source\":1,\n\"buffer_ms\":"<<(adaptive_mode(mode)?75:0)<<"\n}\n";
        }
        bool paused=false,slow=false;
        auto last=Clock::now(),window=last,deadline=last,profile_epoch=last;
        double last_profile_present=0,last_profile_call=0;
        unsigned total=0,renders=0,presents=0,generated=0;
        bool captured=false;
        while(app.window.running && (!smoke || total<360)) {
            if(adaptive_mode(mode)) {
                mode=run_adaptive(app,scene,mode,metrics,duration,total,capture);
                last=window=deadline=Clock::now();
                renders=presents=generated=0; app.repeat_rate=0;
                continue;
            }
            app.window.sync_target(0);
            if(duration>0 && std::chrono::duration<double>(Clock::now()-profile_epoch).count()>=duration) break;
            app.window.messages();
            const Mode previous_mode=mode;
            unsigned key=app.window.action; app.window.action=0;
            if(key=='A') { mode=mode==Mode::real30?Mode::lsfg:mode==Mode::lsfg?Mode::native60:Mode::real30; app.reset(); }
            if(key=='G') {
                mode=mode==Mode::lsfg?Mode::real30:Mode::lsfg; app.reset();
                std::printf("TOGGLE %s\n",mode_name(mode)); std::fflush(stdout);
            }
            if(key=='V') { mode=Mode::adaptive40; app.reset(); continue; }
            if(key=='X') { scene.reset(); app.reset(); }
            if(key=='B'||key==VK_SPACE) { paused=!paused; app.reset(); }
            if(key=='Y') { slow=!slow; app.reset(); }
            if(app.window.clicked) {
                app.window.clicked=false;
                if(scene.hit(double(app.window.click_x)/W,double(app.window.click_y)/H,double(W)/H)) {
                    scene.jump(); paused=false; app.reset();
                    std::printf("BALL JUMP\n"); std::fflush(stdout);
                }
            }
            if(smoke && total==120) { mode=Mode::native60; app.reset(); }
            if(smoke && total==240) { mode=Mode::real30; app.reset(); }
            if(mode!=previous_mode) {
                renders=presents=generated=0; window=Clock::now();
                app.render_rate=app.present_rate=app.generated_rate=0;
            }
            app.window.generator_button(mode==Mode::lsfg);
            app.window.adaptive_mode_button(false);
            app.window.input_buttons(paused,slow);
            auto now=Clock::now();
            if(!paused) scene.advance(std::chrono::duration<double>(now-last).count()*(slow?0.25:1.0));
            last=now;
            auto source_start=Clock::now();
            app.scene(scene,mode);
            auto profile_time=[&] { return std::chrono::duration<double>(Clock::now()-profile_epoch).count(); };
            metrics.source(profile_time(),total,std::chrono::duration<double>(now-profile_epoch).count(),app.scene_ms,app.lsfg_ms,
                std::chrono::duration<double,std::milli>(Clock::now()-source_start).count(),generator_enabled(mode)?1:0,generator_enabled(mode)?1:0);
            auto original=app.textures[app.input_index()];
            auto image=app.generated_valid?app.textures[app.generated_index]:original;
            auto screenshot=!captured && total>=50 ? capture : fs::path{};
            app.present(image,mode,paused,slow,deadline,screenshot); ++presents;
            double presented=profile_time();
            double submitted=std::chrono::duration<double>(app.last_swap_call-profile_epoch).count();
            metrics.output(presented,total*(mode==Mode::native60?1:2),total,total,app.generated_valid?"generated":"real",
                last_profile_present>0?(presented-last_profile_present)*1000:0,0,0,false,submitted,
                last_profile_call>0?(submitted-last_profile_call)*1000:0,app.swap_block_ms);
            last_profile_present=presented;
            last_profile_call=submitted;
            if(!screenshot.empty()) captured=true;
            if(mode!=Mode::native60) {
                app.present(original,mode,paused,slow,deadline); ++presents;
                presented=profile_time();
                submitted=std::chrono::duration<double>(app.last_swap_call-profile_epoch).count();
                metrics.output(presented,total*2+1,total,total,app.generated_valid?"real":"repeat",(presented-last_profile_present)*1000,0,0,false,
                    submitted,(submitted-last_profile_call)*1000,app.swap_block_ms);
                last_profile_present=presented;
                last_profile_call=submitted;
            }
            if(app.generated_valid) ++generated;
            app.next(); ++renders; ++total;
            auto elapsed=std::chrono::duration<double>(Clock::now()-window).count();
            if(elapsed>=1.0) {
                app.render_rate=renders/elapsed; app.present_rate=presents/elapsed; app.generated_rate=generated/elapsed;
                char title[256]; std::snprintf(title,sizeof(title),"LSFG Desktop | %s | render %.1f | present %.1f | generated %.1f | GPU LSFG %.2f ms | A mode, ESC exit",
                    mode_name(mode),app.render_rate,app.present_rate,app.generated_rate,app.lsfg_ms);
                SetWindowTextA(app.window.hwnd,title);
                std::printf("PERF mode=%s render=%.2f present_calls=%.2f generated=%.2f gpu_lsfg_ms=%.3f frame=%u\n",
                    mode_name(mode),app.render_rate,app.present_rate,app.generated_rate,app.lsfg_ms,total);
                std::fflush(stdout); renders=presents=generated=0; window=Clock::now();
            }
        }
        std::printf("DESKTOP EXIT frames=%u gl_error=%u\n",total,glGetError()); std::fflush(stdout);
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"DESKTOP ERROR: %s\n",e.what()); std::fflush(stderr);
        MessageBoxA(nullptr,e.what(),"LSFG Desktop error",MB_OK|MB_ICONERROR);
        return 1;
    }
}
