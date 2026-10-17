#include "planner.hpp"
#include "input.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace rf;
namespace {
void check(bool b, char const *s) {
    if (!b)
        throw std::runtime_error(s);
}
// Independent simple oracle, different step and integration from the planner.
void oracle(FormState &s, bool down, double dt) {
    double sign = s.inverted ? -1 : 1;
    bool press = down && !s.held;
    if (s.form == Form::Ball && s.grounded && press) {
        s.vy = sign * 6.7;
        s.inverted = !s.inverted;
        s.grounded = false;
    } else if (s.form == Form::Spider && s.grounded && press) {
        s.inverted = !s.inverted;
        s.y = s.inverted ? s.ceiling - s.halfHeight : s.floor + s.halfHeight;
        s.vy = 0;
    } else if (s.form == Form::Robot && s.grounded && down) {
        s.vy = sign * 5.52 * (s.scale < .9 ? .8 : 1);
        s.grounded = false;
        s.robotTime = 0;
    } else if (s.form == Form::Ufo && press) {
        s.vy = sign * (s.scale < .9 ? 6.75 : 7.05);
        s.grounded = false;
    } else if (s.form == Form::Swing && press) {
        s.inverted = !s.inverted;
        s.vy *= .81;
    }
    sign = s.inverted ? -1 : 1;
    double gravity = s.form == Form::Ball || s.form == Form::Spider ? .57
                     : s.form == Form::Robot                        ? .85
                     : s.form == Form::Swing                        ? .39
                                                                    : .49;
    bool boost = s.form == Form::Robot && down && s.robotTime < 15 && s.vy * sign > 0;
    if (!s.grounded && !boost)
        s.vy -= sign * gravity * dt;
    if (s.form == Form::Robot) {
        s.robotTime += dt;
        if (!down)
            s.robotTime = 16;
    }
    s.y += s.vy * dt;
    s.x += s.vx * dt;
    s.held = down;
    if (s.form == Form::Ball || s.form == Form::Spider || s.form == Form::Robot) {
        if (!s.inverted && s.y - s.halfHeight <= s.floor) {
            s.y = s.floor + s.halfHeight;
            s.vy = 0;
            s.grounded = true;
        } else if (s.inverted && s.y + s.halfHeight >= s.ceiling) {
            s.y = s.ceiling - s.halfHeight;
            s.vy = 0;
            s.grounded = true;
        } else
            s.grounded = false;
    }
}
void course(Form form, bool inverted, double direction) {
    FormState s;
    s.form = form;
    s.gravity = .958199;
    s.floor = 91;
    s.ceiling = 331;
    s.x = 0;
    s.y = (form == Form::Ufo || form == Form::Swing) ? 211 : inverted ? 317 : 105;
    s.inverted = inverted;
    s.vx = 5.2 * direction;
    s.grounded = form != Form::Ufo && form != Form::Swing;
    s.pressInterval = 3.75;
    std::vector<Obstacle> objects;
    if (form == Form::Ball || form == Form::Spider)
        objects = {{190, 225, 91, 151}, {430, 465, 271, 331}};
    if (form == Form::Robot)
        objects = {{190, 215, 91, 126}, {430, 455, 91, 136}};
    if (inverted && (form == Form::Robot)) {
        for (auto &o : objects) {
            double bottom = 422 - o.top;
            o.top = 422 - o.bottom;
            o.bottom = bottom;
        }
    }
    if (direction < 0)
        for (auto &o : objects) {
            double left = -o.right;
            o.right = -o.left;
            o.left = left;
        }
    InputPacer pacer;
    pacer.reset(16);
    for (int tick = 0; tick < 560; tick++) {
        s.pressRemaining = pacer.pressRemainingFrames(tick) / 4.;
        bool down = pacer.apply(chooseFormInput(s, objects, {}).down, tick);
        check(pacer.currentCps() <= 16, "Form exceeded 16 CPS");
        oracle(s, down, .125);
        oracle(s, down, .125);
        check(s.y - s.halfHeight >= s.floor - .1 && s.y + s.halfHeight <= s.ceiling + .1,
              "Form left oracle bounds");
        for (auto const &o : objects)
            if (s.x + s.halfWidth > o.left && s.x - s.halfWidth < o.right &&
                s.y + s.halfHeight > o.bottom + .1 && s.y - s.halfHeight < o.top - .1) {
                std::cerr << "form=" << static_cast<int>(form) << " inv=" << inverted << " x=" << s.x
                          << " y=" << s.y << '\n';
                throw std::runtime_error("Form hit independent oracle obstacle");
            }
    }
    check(std::abs(s.x) > 600, "Form course did not finish");
}
} // namespace
int main() {
    try {
        FormState s;
        s.gravity = .958199;
        std::uint64_t used = 0;
        auto ball = s;
        ball.form = Form::Ball;
        check(advanceForm(ball, true, .25, {}, {}, used) && ball.inverted && !ball.grounded,
              "Ball did not flip");
        auto spider = s;
        spider.form = Form::Spider;
        spider.ceiling = 250;
        used = 0;
        check(advanceForm(spider, true, .25, {}, {}, used) && spider.inverted && spider.grounded &&
                  spider.y > 200,
              "Spider did not snap to opposite surface");
        auto ufo = s;
        ufo.form = Form::Ufo;
        ufo.grounded = false;
        ufo.y = 160;
        ufo.ceiling = 350;
        used = 0;
        check(advanceForm(ufo, true, .25, {}, {}, used) && ufo.vy > 0, "UFO did not flap");
        double v = ufo.vy;
        check(advanceForm(ufo, true, .25, {}, {}, used) && ufo.vy < v,
              "Held UFO retriggered without release");
        auto shortRobot = s, longRobot = s;
        shortRobot.form = longRobot.form = Form::Robot;
        shortRobot.ceiling = longRobot.ceiling = 500;
        for (int i = 0; i < 40; i++) {
            used = 0;
            advanceForm(shortRobot, i == 0, .25, {}, {}, used);
            advanceForm(longRobot, true, .25, {}, {}, used);
        }
        check(longRobot.y > shortRobot.y + 20, "Robot hold did not vary jump height");
        auto swing = ufo;
        swing.form = Form::Swing;
        swing.held = false;
        swing.pressRemaining = 0;
        used = 0;
        check(advanceForm(swing, true, .25, {}, {}, used) && swing.inverted,
              "Swing did not invert on a new press");
        Interaction orb;
        orb.area = {-30, 30, 70, 160};
        orb.kind = InteractionKind::RedOrb;
        s.grounded = false;
        s.y = 120;
        used = 0;
        auto red = s;
        check(advanceForm(red, true, .25, {}, std::span(&orb, 1), used) && red.vy > 14,
              "Red orb impulse missing");
        double first = red.vy;
        check(advanceForm(red, true, .25, {}, std::span(&orb, 1), used) && red.vy < first,
              "Orb activated twice in one forecast");
        orb.kind = InteractionKind::BlackOrb;
        used = 0;
        auto black = s;
        check(advanceForm(black, true, .25, {}, std::span(&orb, 1), used) && black.vy < 0,
              "Black orb did not reverse vertical motion");
        orb.kind = InteractionKind::ModePortal;
        orb.target = Form::Wave;
        used = 0;
        auto portal = s;
        check(advanceForm(portal, false, .25, {}, std::span(&orb, 1), used) && portal.form == Form::Wave,
              "Mode portal not predicted");
        // Facing up sets normal gravity before the native spider-style flip.
        orb.kind = InteractionKind::SpiderOrb;
        orb.inverted = false;
        used = 0;
        auto spiderOrb = s;
        spiderOrb.ceiling = 250;
        std::vector<Obstacle> midwall{{-10, 10, 175, 190, true}};
        check(advanceForm(spiderOrb, true, .25, midwall, std::span(&orb, 1), used) && spiderOrb.inverted &&
                  spiderOrb.y == 161,
              "Spider orb direction or destination collision wrong");
        orb.inverted = true;
        used = 0;
        spiderOrb = s;
        check(advanceForm(spiderOrb, true, .25, {}, std::span(&orb, 1), used) && !spiderOrb.inverted &&
                  spiderOrb.grounded,
              "Inverted spider orb did not go to floor");
        int passed = 0;
        for (auto form : {Form::Ball, Form::Ufo, Form::Robot, Form::Spider, Form::Swing})
            for (bool inv : {false, true})
                for (double direction : {1., -1.}) {
                    course(form, inv, direction);
                    ++passed;
                }
        std::cout << passed
                  << " form oracle courses; ball/UFO/robot/spider/swing, orb reuse, mode portals and 16 CPS: "
                     "PASS\n";
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
