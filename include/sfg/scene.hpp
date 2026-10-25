// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>

namespace sfg {
struct Ball {
    double x{0.35}, y{0.24}, vx{0.18}, vy{0.0}, radius{0.065};
};

class Scene {
public:
    static constexpr double floor_y = 0.84;
    void reset() { ball_ = {}; accumulator_ = 0; time_ = 0; }
    const Ball& ball() const { return ball_; }
    double time() const { return time_; }
    void jump() { ball_.vy = -1.35; }
    bool hit(double x, double y, double aspect) const {
        const double dx=(x-ball_.x)*aspect,dy=y-ball_.y;
        return dx*dx+dy*dy<=ball_.radius*ball_.radius;
    }
    void advance(double seconds) {
        accumulator_ += std::clamp(seconds, 0.0, 0.25);
        constexpr double step = 1.0 / 240.0;
        while (accumulator_ + 1e-12 >= step) {
            accumulator_ -= step;
            time_ += step;
            ball_.vy += 1.15 * step;
            ball_.y += ball_.vy * step;
            ball_.x += ball_.vx * step;
            if (ball_.y + ball_.radius >= floor_y) {
                ball_.y = floor_y - ball_.radius;
                ball_.vy = -std::max(0.72, ball_.vy * 0.88);
            }
            if (ball_.x < 0.12 || ball_.x > 0.88) {
                ball_.x = std::clamp(ball_.x, 0.12, 0.88);
                ball_.vx = -ball_.vx;
            }
        }
    }
private:
    Ball ball_{};
    double accumulator_{}, time_{};
};
}
