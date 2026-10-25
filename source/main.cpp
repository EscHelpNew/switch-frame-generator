// SPDX-License-Identifier: GPL-3.0-or-later
#include <switch.h>
#include <deko3d.hpp>
#include <sfg/generator.hpp>
#include <sfg/scene.hpp>
#include <build_info.hpp>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
constexpr unsigned W=1280, H=720, CommandBytes=256*1024;
constexpr unsigned CpuGpu=DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached;
double seconds(uint64_t ticks) { return double(ticks)/armGetSystemTickFreq(); }
struct alignas(16) Report { uint64_t value{}, timestamp{}; };
struct alignas(16) SceneUniform { float ball[4], extent[4]; };
struct alignas(16) HudUniform { uint32_t text[320]{}; };
enum class Mode { real30, lsfg, native60 };
const char* name(Mode mode) {
    return mode==Mode::lsfg ? "LSFG 30>60" : mode==Mode::real30 ? "REAL 30" : "NATIVE 60";
}

class Graphics {
public:
    // Device declared first, so it outlives every resource on normal destruction.
    dk::UniqueDevice device;
    dk::UniqueMemBlock framebuffer_memory, code_memory, command_memory, uniforms, reports;
    std::array<dk::Image,2> framebuffers;
    dk::UniqueSwapchain swapchain;
    dk::UniqueCmdBuf cmd;
    dk::UniqueQueue queue;
    std::unique_ptr<sfg::Generator> generator;
    dk::Image fallback;
    dk::UniqueMemBlock fallback_memory;
    dk::Shader vertex, scene_shader, hud_shader;
    std::string lsfg_error;
    double scene_ms{}, lsfg_ms{}, submit_ms{}, present_interval_ms{};

    Graphics() {
        try { initialize(); }
        catch (...) { if (queue) dkQueueWaitIdle(queue); throw; }
    }
    ~Graphics() { if(queue) dkQueueWaitIdle(queue); }
    Graphics(const Graphics&)=delete;
    Graphics& operator=(const Graphics&)=delete;

    dk::UniqueMemBlock allocate(uint64_t size, unsigned flags) {
        size=(size+DK_MEMBLOCK_ALIGNMENT-1)&~uint64_t(DK_MEMBLOCK_ALIGNMENT-1);
        if(!size || size>UINT32_MAX) throw std::runtime_error("Invalid GPU allocation size");
        auto result=dk::MemBlockMaker{device,uint32_t(size)}.setFlags(flags).create();
        if(!result) throw std::runtime_error("GPU allocation failed");
        return result;
    }
    void initialize() {
        device=dk::DeviceMaker{}.create();
        if(!device) throw std::runtime_error("deko3d device failed");
        queue=dk::QueueMaker{device}.setPerWarpScratchMemorySize(65536).create();
        if(!queue) throw std::runtime_error("deko3d queue failed");
        dk::ImageLayout layout;
        dk::ImageLayoutMaker{device}.setFormat(DkImageFormat_RGBA8_Unorm)
          .setDimensions(W,H).setFlags(DkImageFlags_UsageRender|DkImageFlags_UsagePresent|
              DkImageFlags_Usage2DEngine).initialize(layout);
        auto size=(layout.getSize()+layout.getAlignment()-1)&~uint64_t(layout.getAlignment()-1);
        framebuffer_memory=allocate(size*2,DkMemBlockFlags_GpuCached|DkMemBlockFlags_Image);
        const DkImage* images[2];
        for(unsigned i=0;i<2;++i) {
            framebuffers[i].initialize(layout,framebuffer_memory,uint32_t(size*i)); images[i]=&framebuffers[i];
        }
        swapchain=dk::SwapchainMaker{device,nwindowGetDefault(),images,2}.create();
        if(!swapchain) throw std::runtime_error("Swapchain failed");
        swapchain.setSwapInterval(1);
        code_memory=allocate(256*1024,CpuGpu|DkMemBlockFlags_Code);
        unsigned offset=0;
        load(vertex,"romfs:/shaders/fullscreen.dksh",offset);
        load(scene_shader,"romfs:/shaders/scene.dksh",offset);
        load(hud_shader,"romfs:/shaders/hud.dksh",offset);
        command_memory=allocate(CommandBytes,CpuGpu);
        uniforms=allocate(4096,CpuGpu);
        reports=allocate(4096,CpuGpu|DkMemBlockFlags_ZeroFillInit);
        cmd=dk::CmdBufMaker{device}.create();
        if(!cmd) throw std::runtime_error("Command buffer failed");
        cmd.addMemory(command_memory,0,CommandBytes);
        // A separate scene target keeps base modes usable if cache validation fails.
        dk::ImageLayoutMaker{device}.setFormat(DkImageFormat_RGBA8_Unorm)
          .setDimensions(W,H).setFlags(DkImageFlags_UsageRender|DkImageFlags_Usage2DEngine)
          .initialize(layout);
        fallback_memory=allocate(layout.getSize(),DkMemBlockFlags_GpuCached|DkMemBlockFlags_Image);
        fallback.initialize(layout,fallback_memory,0);
        generator=sfg::Generator::create(device,W,H,"romfs:/lsfg",lsfg_error);
        if(!generator) std::printf("LSFG disabled: %s\n",lsfg_error.c_str());
    }
    void load(dk::Shader& shader,const char* path,unsigned& offset) {
        FILE* file=std::fopen(path,"rb");
        if(!file) throw std::runtime_error(std::string("Missing shader: ")+path);
        std::fseek(file,0,SEEK_END); long length=std::ftell(file); std::rewind(file);
        if(length<=0 || offset+length+DK_SHADER_CODE_UNUSABLE_SIZE>256*1024) {
            std::fclose(file); throw std::runtime_error("Invalid scene shader size");
        }
        void* destination=static_cast<uint8_t*>(code_memory.getCpuAddr())+offset;
        bool read=std::fread(destination,1,size_t(length),file)==size_t(length);
        std::fclose(file);
        if(!read) throw std::runtime_error("Scene shader read failed");
        dk::ShaderMaker{code_memory,offset}.initialize(shader);
        if(!shader.isValid()) throw std::runtime_error("Invalid scene shader DKSH");
        offset=(offset+unsigned(length)+DK_SHADER_CODE_UNUSABLE_SIZE+DK_SHADER_CODE_ALIGNMENT-1)&
            ~(DK_SHADER_CODE_ALIGNMENT-1);
    }
    void wait() {
        if(inflight_) {
            if(dkFenceWait(&fence_,UINT64_MAX)!=DkResult_Success)
                throw std::runtime_error("GPU fence failed");
            inflight_=false;
        }
        if(dkQueueIsInErrorState(queue)) throw std::runtime_error("GPU queue entered error state");
    }
    void begin() { wait(); cmd.clear(); }
    void draw(const DkImage* target,const DkShader* fragment,DkGpuAddr address,unsigned bytes) {
        DkImageView view; dkImageViewDefaults(&view,target);
        dkCmdBufBindRenderTarget(cmd,&view,nullptr);
        const DkViewport viewport{0,0,float(W),float(H),0,1};
        const DkScissor scissor{0,0,W,H};
        dkCmdBufSetViewports(cmd,0,&viewport,1); dkCmdBufSetScissors(cmd,0,&scissor,1);
        DkRasterizerState raster; dkRasterizerStateDefaults(&raster);
        DkColorState color; dkColorStateDefaults(&color);
        DkColorWriteState write; dkColorWriteStateDefaults(&write);
        dkCmdBufBindRasterizerState(cmd,&raster);
        dkCmdBufBindColorState(cmd,&color); dkCmdBufBindColorWriteState(cmd,&write);
        const DkShader* shaders[]{&vertex,fragment};
        dkCmdBufBindShaders(cmd,DkStageFlag_GraphicsMask,shaders,2);
        dkCmdBufBindUniformBuffer(cmd,DkStage_Fragment,0,address,bytes);
        dkCmdBufDraw(cmd,DkPrimitive_Triangles,3,1,0,0);
    }
    void timestamp(unsigned index) {
        dkCmdBufBarrier(cmd,DkBarrier_Full,0);
        dkCmdBufReportCounter(cmd,DkCounter_Timestamp,reports.getGpuAddr()+index*sizeof(Report));
    }
    void render(const sfg::Scene& scene,Mode mode,const DkImage*& generated,const DkImage*& original) {
        begin();
        generated=nullptr;
        original=mode==Mode::lsfg && generator ? generator->input() : &fallback;
        const auto& b=scene.ball();
        const SceneUniform constants{{float(b.x),float(b.y),float(b.radius),float(scene.time())},
            {float(W),float(H),float(sfg::Scene::floor_y),0}};
        dkCmdBufPushConstants(cmd,uniforms.getGpuAddr(),256,0,sizeof(constants),&constants);
        timestamp(0);
        draw(original,&scene_shader,uniforms.getGpuAddr(),256);
        timestamp(1);
        if(mode==Mode::lsfg && generator) {
            if(!generator->record(cmd,generated,original,lsfg_error))
                throw std::runtime_error(lsfg_error);
        }
        timestamp(2);
    }
    void present(const DkImage* image,const HudUniform& hud,bool finish_render) {
        // First present submits scene + compute + composition together.
        if(!finish_render) begin();
        int slot=dkQueueAcquireImage(queue,swapchain);
        if(slot<0 || slot>=2) throw std::runtime_error("Swapchain acquire failed");
        DkImageView src,dst;
        dkImageViewDefaults(&src,image); dkImageViewDefaults(&dst,&framebuffers[slot]);
        const DkImageRect rect{0,0,0,W,H,1};
        dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image);
        dkCmdBufBlitImage(cmd,&src,&rect,&dst,&rect,0,0);
        dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_Image|DkInvalidateFlags_Shader);
        dkCmdBufPushConstants(cmd,uniforms.getGpuAddr()+256,sizeof(hud),0,sizeof(hud),&hud);
        draw(&framebuffers[slot],&hud_shader,uniforms.getGpuAddr()+256,sizeof(hud));
        auto start=armGetSystemTick();
        dkQueueSubmitCommands(queue,cmd.finishList());
        dkQueueSignalFence(queue,&fence_,true); inflight_=true;
        submit_ms=seconds(armGetSystemTick()-start)*1000;
        dkQueuePresentImage(queue,swapchain,slot);
        auto now=armGetSystemTick();
        if(last_present_) present_interval_ms=seconds(now-last_present_)*1000;
        last_present_=now;
    }
    void collect() {
        wait();
        const auto* result=static_cast<const volatile Report*>(reports.getCpuAddr());
        const uint64_t a=result[0].timestamp,b=result[1].timestamp,c=result[2].timestamp;
        scene_ms=b>=a ? double(dkTimestampToNs(b-a))/1e6 : 0;
        lsfg_ms=c>=b ? double(dkTimestampToNs(c-b))/1e6 : 0;
    }
private:
    DkFence fence_{};
    bool inflight_{};
    uint64_t last_present_{};
};

HudUniform hud(Mode mode,bool paused,bool slow,const Graphics& graphics,
               double renders,double presents,double generated) {
    HudUniform result;
    char lines[5][128]{};
    std::snprintf(lines[0],sizeof(lines[0]),"LSFG SWITCH  %s  1280X720  %s %s",name(mode),paused?"PAUSE":"",slow?"SLOW":"");
    std::snprintf(lines[1],sizeof(lines[1]),"RENDER %.1F  PRESENT CALLS %.1F  GENERATED %.1F",renders,presents,generated);
    std::snprintf(lines[2],sizeof(lines[2]),"GPU SCENE %.2F MS  LSFG %.2F MS  CPU SUBMIT %.2F MS",graphics.scene_ms,graphics.lsfg_ms,graphics.submit_ms);
    std::snprintf(lines[3],sizeof(lines[3]),"A MODE  X RESET  B PAUSE  Y SLOW  + EXIT");
    std::snprintf(lines[4],sizeof(lines[4]),"%s",graphics.generator?"NATIVE 720P  FLOW 1/4  PERFORMANCE  LSFG 2X":graphics.lsfg_error.c_str());
    for(unsigned row=0;row<5;++row) for(unsigned column=0;column<64 && lines[row][column];++column) {
        auto c=static_cast<unsigned char>(lines[row][column]);
        if(c>='a' && c<='z') c-=32;
        result.text[row*64+column]=c;
    }
    return result;
}
}

int main(int argc,char** argv) {
    bool network=R_SUCCEEDED(socketInitializeDefault());
    int log_socket=network ? nxlinkStdio() : -1;
    bool romfs=R_SUCCEEDED(romfsInit());
    int status=0;
    try {
        if(!romfs) throw std::runtime_error("romfsInit failed");
        Mode mode=Mode::real30; unsigned frame_limit=0;
        for(int i=1;i<argc;++i) {
            if(std::strcmp(argv[i],"--mode=lsfg")==0) mode=Mode::lsfg;
            else if(std::strcmp(argv[i],"--mode=native60")==0) mode=Mode::native60;
            else if(std::strncmp(argv[i],"--frames=",9)==0) frame_limit=unsigned(std::strtoul(argv[i]+9,nullptr,10));
        }
        std::printf("SFG %s DLL %s\n",SFG_BUILD_ID,SFG_DLL_HASH);
        std::printf("Controls: A mode, X reset, B pause, Y slow, + exit. Native 1280x720.\n");
        {
            Graphics graphics;
            if(mode==Mode::lsfg && !graphics.generator) mode=Mode::real30;
            sfg::Scene scene;
            padConfigureInput(1,HidNpadStyleSet_NpadStandard);
            PadState pad; padInitializeDefault(&pad);
            bool paused=false,slow=false;
            uint64_t last=armGetSystemTick(),window=last;
            unsigned total=0,render_count=0,present_count=0,generated_count=0;
            double render_rate=0,present_rate=0,generated_rate=0;
            while(appletMainLoop() && (!frame_limit || total<frame_limit)) {
                padUpdate(&pad); const uint64_t down=padGetButtonsDown(&pad);
                if(down&HidNpadButton_Plus) break;
                if(down&HidNpadButton_A) {
                    graphics.wait();
                    mode=mode==Mode::real30 ? (graphics.generator?Mode::lsfg:Mode::native60) :
                        mode==Mode::lsfg?Mode::native60:Mode::real30;
                    if(graphics.generator) graphics.generator->reset();
                    render_count=present_count=generated_count=0;
                    render_rate=present_rate=generated_rate=0;
                    window=armGetSystemTick();
                    std::printf("MODE %s\n",name(mode));
                }
                if(down&HidNpadButton_X) { scene.reset(); if(graphics.generator) graphics.generator->reset(); }
                if(down&HidNpadButton_B) { paused=!paused; if(graphics.generator) graphics.generator->reset(); }
                if(down&HidNpadButton_Y) { slow=!slow; if(graphics.generator) graphics.generator->reset(); }
                const auto now=armGetSystemTick();
                if(!paused) scene.advance(seconds(now-last)*(slow?0.25:1.0));
                last=now;
                const DkImage *generated=nullptr,*original=nullptr;
                graphics.render(scene,mode,generated,original);
                const auto overlay=hud(mode,paused,slow,graphics,render_rate,present_rate,generated_rate);
                if(mode==Mode::native60) {
                    graphics.present(original,overlay,true); ++present_count;
                } else {
                    graphics.present(generated?generated:original,overlay,true); ++present_count;
                    if(generated) ++generated_count;
                    graphics.present(original,overlay,false); ++present_count;
                }
                graphics.collect();
                ++render_count; ++total;
                const double elapsed=seconds(armGetSystemTick()-window);
                if(elapsed>=1.0) {
                    render_rate=render_count/elapsed; present_rate=present_count/elapsed;
                    generated_rate=generated_count/elapsed;
                    std::printf("PERF mode=%s render=%.2f present_calls=%.2f generated=%.2f gpu_scene_ms=%.3f gpu_lsfg_ms=%.3f cpu_submit_ms=%.3f present_call_interval_ms=%.3f frame=%u\n",
                        name(mode),render_rate,present_rate,generated_rate,graphics.scene_ms,graphics.lsfg_ms,
                        graphics.submit_ms,graphics.present_interval_ms,total);
                    std::fflush(stdout);
                    render_count=present_count=generated_count=0; window=armGetSystemTick();
                }
            }
            graphics.wait(); std::printf("SFG EXIT frames=%u\n",total); std::fflush(stdout);
        }
    } catch(const std::exception& e) {
        std::printf("SFG ERROR: %s\n",e.what()); std::fflush(stdout); status=1;
        consoleInit(nullptr);
        std::printf("SFG ERROR: %s\nPress + to exit.\n",e.what());
        padConfigureInput(1,HidNpadStyleSet_NpadStandard); PadState pad; padInitializeDefault(&pad);
        while(appletMainLoop()) { padUpdate(&pad); if(padGetButtonsDown(&pad)&HidNpadButton_Plus) break; consoleUpdate(nullptr); }
        consoleExit(nullptr);
    }
    if(romfs) romfsExit();
    if(log_socket>=0) close(log_socket);
    if(network) socketExit();
    return status;
}
