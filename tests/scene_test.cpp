// SPDX-License-Identifier: GPL-3.0-or-later
#include <sfg/scene.hpp>
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    sfg::Scene scene;
    const auto initial = scene.ball();
    scene.advance(0.1);
    assert(scene.ball().y > initial.y);
    assert(scene.ball().vy > 0);
    bool bounced = false;
    double previous_vy = scene.ball().vy;
    for (int i = 0; i < 1200; ++i) {
        scene.advance(1.0 / 240.0);
        const auto b = scene.ball();
        assert(b.y + b.radius <= sfg::Scene::floor_y + 1e-8);
        if (previous_vy > 0 && b.vy < 0) bounced = true;
        previous_vy = b.vy;
    }
    assert(bounced);
    sfg::Scene a, b;
    for (int i = 0; i < 120; ++i) a.advance(1.0 / 30.0);
    for (int i = 0; i < 240; ++i) b.advance(1.0 / 60.0);
    assert(std::abs(a.ball().y - b.ball().y) < 1e-8);
    assert(std::abs(a.ball().x - b.ball().x) < 1e-8);
    b.reset();
    assert(b.ball().y == initial.y && b.ball().vy == initial.vy);
    const auto before = b.ball();
    b.advance(0);
    assert(b.ball().y == before.y);
    b.advance(0.2);
    const double jump_y=b.ball().y;
    assert(b.hit(b.ball().x,b.ball().y,16.0/9.0));
    assert(!b.hit(0.01,0.01,16.0/9.0));
    b.jump();
    assert(b.ball().vy<0);
    b.advance(0.1);
    assert(b.ball().y<jump_y);
    std::cout << "Scene: fall, bounce, floor bounds, fixed-step parity, reset, click hit and jump passed\n";
}
