// Presentation and input logic that needs no window: the health bar's damage chip and wheel stepping between wands.
#include <cmath>

#include "app/InputLatch.hpp"
#include "tests/TestFramework.hpp"
#include "ui/Ui.hpp"

using namespace pm;

TEST_CASE("the mouse wheel steps one wand per notch, however it is sent") {
    // An ordinary wheel: one full delta per notch.
    input::WheelStepper notched;
    CHECK(notched.step(1.0f, 0.016f) == 1);
    CHECK(notched.step(0.0f, 0.2f) == 0);
    CHECK(notched.step(-1.0f, 0.016f) == -1);
    // A touchpad swipe: a notch's worth of small deltas over many frames is one step.
    input::WheelStepper pad;
    int steps = 0;
    for (int i = 0; i < 10; ++i) steps += pad.step(0.1f, 0.016f);
    CHECK(steps == 1);
    // A pause forgets a partial notch.
    input::WheelStepper partial;
    CHECK(partial.step(0.6f, 0.016f) == 0);
    CHECK(partial.step(0.0f, 0.3f) == 0);
    CHECK(partial.step(0.6f, 0.016f) == 0);
    // Notches faster than the cooldown are not lost, just spaced out: a flick of three, or two
    // notches reported in one frame, each give their full count.
    input::WheelStepper fast;
    int flick = 0;
    for (int frame = 0; frame < 60; ++frame) flick += fast.step(frame == 0 || frame == 2 || frame == 4 ? -1.0f : 0.0f, 1.0f / 60.0f);
    CHECK(flick == -3);
    input::WheelStepper twice;
    int both = 0;
    for (int frame = 0; frame < 60; ++frame) both += twice.step(frame == 0 ? 2.0f : 0.0f, 1.0f / 60.0f);
    CHECK(both == 2);
    // Turning back at once is one step back, not swallowed by what was queued.
    input::WheelStepper back;
    CHECK(back.step(1.0f, 0.016f) == 1);
    CHECK(back.step(0.5f, 0.016f) == 0);
    CHECK(back.step(-1.0f, 0.2f) == -1);
}

TEST_CASE("a health bar's damage chip holds, drains, and snaps up on a heal") {
    ui::BarTrail trail;
    trail.update(1.0f, 0.016f);
    CHECK(trail.shown == 1.0f);
    trail.update(0.6f, 0.016f); // a hit: the chip stays where the bar was
    CHECK(trail.shown == 1.0f);
    trail.update(0.6f, 0.2f); // still holding
    CHECK(trail.shown == 1.0f);
    for (int i = 0; i < 30; ++i) trail.update(0.6f, 0.05f);
    CHECK(std::fabs(trail.shown - 0.6f) < 1e-4f); // drained down to the bar, never below it
    trail.update(0.9f, 0.016f); // a heal snaps the chip up with the bar
    CHECK(trail.shown == 0.9f);
}
