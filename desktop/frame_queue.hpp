// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gl_api.hpp"
#include <sfg/frame_pacer.hpp>
#include <sfg/scene.hpp>
#include <array>
#include <optional>

struct QueuedFrame {
    int64_t tick{};
    unsigned source{},slot{};
    sfg::SampleKind kind{sfg::SampleKind::real};
    sfg::Ball ball;
};

// Copies detach queued output from the two mutable LSFG history images.
class FrameQueue {
public:
    static constexpr unsigned capacity=12;
    static constexpr uint64_t image_bytes=uint64_t(capacity)*1280*720*4;
    FrameQueue() {
        glGenTextures(capacity,textures_.data());
        for(auto image:textures_) {
            glBindTexture(GL_TEXTURE_2D,image);
            glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,1280,720);
        }
        check_gl("output queue allocation");
    }
    ~FrameQueue() { glFinish(); glDeleteTextures(capacity,textures_.data()); }
    bool enqueue(GLuint image,QueuedFrame frame) {
        auto slot=copy(image); if(!slot) { ++overflow; return false; }
        frame.slot=*slot;
        pending_[(head_+count_)%capacity]=frame; ++count_;
        peak=std::max(peak,count_); return true;
    }
    void input_cut(GLuint image,QueuedFrame frame) {
        clear_pending();
        if(urgent_) { used_[urgent_->slot]=false; urgent_.reset(); }
        auto slot=copy(image);
        if(!slot) throw std::runtime_error("No slot for immediate input response");
        frame.slot=*slot; frame.kind=sfg::SampleKind::real; urgent_=frame;
    }
    std::optional<QueuedFrame> take(int64_t tick) {
        while(count_ && pending_[head_].tick<tick) { discard_front(); ++stale; }
        if(urgent_) {
            if(count_ && pending_[head_].tick==tick) discard_front();
            replace_held(*urgent_); urgent_.reset(); held_->tick=tick;
            return held_;
        }
        if(count_ && pending_[head_].tick==tick) {
            const auto frame=pending_[head_]; head_=(head_+1)%capacity; --count_;
            replace_held(frame); return held_;
        }
        if(!held_) return {};
        auto repeated=*held_; repeated.tick=tick; repeated.kind=sfg::SampleKind::repeat;
        return repeated;
    }
    const std::optional<QueuedFrame>& shown() const { return held_; }
    GLuint image(const QueuedFrame& frame) const { return textures_[frame.slot]; }
    unsigned size() const { return count_; }
    void clear_pending() { while(count_) discard_front(); }
    unsigned overflow{},stale{},peak{};
private:
    std::array<GLuint,capacity> textures_{};
    std::array<bool,capacity> used_{};
    std::array<QueuedFrame,capacity> pending_{};
    std::optional<QueuedFrame> held_,urgent_;
    unsigned head_{},count_{};
    std::optional<unsigned> copy(GLuint image) {
        for(unsigned i=0;i<capacity;++i) if(!used_[i]) {
            used_[i]=true;
            glMemoryBarrier(GL_ALL_BARRIER_BITS);
            glCopyImageSubData(image,GL_TEXTURE_2D,0,0,0,0,textures_[i],GL_TEXTURE_2D,0,0,0,0,1280,720,1);
            glMemoryBarrier(GL_ALL_BARRIER_BITS);
            return i;
        }
        return {};
    }
    void discard_front() {
        used_[pending_[head_].slot]=false;
        head_=(head_+1)%capacity; --count_;
    }
    void replace_held(const QueuedFrame& frame) {
        if(held_) used_[held_->slot]=false;
        held_=frame;
    }
};
