#include "mpe_doom_runtime.h"

#include <stdint.h>

#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool has(mpe_doom::ActionMask value, mpe_doom::ActionMask bits) {
    return (value & bits) == bits;
}

void testActionDefinitionsAndMappings() {
    using namespace mpe_doom;

    const ActionMask actions[] = {
        kActionForward, kActionBackward, kActionTurnLeft, kActionTurnRight,
        kActionStrafeLeft, kActionStrafeRight, kActionFire, kActionUse,
        kActionRun, kActionMap, kActionMenu, kActionWeapon1, kActionWeapon2,
        kActionWeapon3, kActionWeapon4, kActionWeapon5, kActionWeapon6,
        kActionWeapon7
    };
    ActionMask union_mask = 0u;
    for (size_t i = 0u; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        expect((union_mask & actions[i]) == 0u, "action bits must be unique");
        union_mask |= actions[i];
    }
    expect(union_mask == kActionAll, "all action bits must be represented");

    struct Mapping {
        uint8_t scan_code;
        ActionMask action;
    };
    const Mapping mappings[] = {
        {0x48u, kActionForward}, {0x50u, kActionBackward},
        {0x4bu, kActionTurnLeft}, {0x4du, kActionTurnRight},
        {0x33u, kActionStrafeLeft}, {0x34u, kActionStrafeRight},
        {0x1du, kActionFire}, {0x39u, kActionUse}, {0x2au, kActionRun},
        {0x0fu, kActionMap}, {0x01u, kActionMenu},
        {0x02u, kActionWeapon1}, {0x03u, kActionWeapon2},
        {0x04u, kActionWeapon3}, {0x05u, kActionWeapon4},
        {0x06u, kActionWeapon5}, {0x07u, kActionWeapon6},
        {0x08u, kActionWeapon7}
    };
    Controls controls;
    for (size_t i = 0u; i < sizeof(mappings) / sizeof(mappings[0]); ++i) {
        controls.reset();
        expect(controls.pushScanEvent(mappings[i].scan_code, true),
               "mapped make must be accepted");
        expect(has(controls.heldActions(), mappings[i].action),
               "mapped make must hold its action");
        expect(controls.pushScanEvent(mappings[i].scan_code, false),
               "mapped break must be accepted");
        expect(!has(controls.heldActions(), mappings[i].action),
               "mapped break must release its action");
    }
}

void testOrderedEventsAndHeldAliases() {
    using namespace mpe_doom;

    Controls controls;
    expect(controls.pushScanEvent(0x48u, true), "forward make accepted");
    expect(controls.pushScanEvent(0x1du, true), "fire make accepted");
    expect(controls.pushScanEvent(0x48u, false), "forward break accepted");

    const uint8_t expected_scans[] = {0x48u, 0x1du, 0x48u};
    const bool expected_pressed[] = {true, true, false};
    for (uint32_t i = 0u; i < 3u; ++i) {
        ScanEvent event = {0u, 0u, false};
        expect(controls.popScanEvent(&event), "queued scan event must pop");
        expect(event.sequence == i, "scan events must retain accepted order");
        expect(event.scan_code == expected_scans[i], "scan code order mismatch");
        expect(event.pressed == expected_pressed[i], "make/break order mismatch");
    }
    expect(!controls.scanHeld(0x48u), "forward key must be released");
    expect(controls.scanHeld(0x1du), "fire key must remain held");

    controls.reset();
    controls.pushScanEvent(0x2au, true);
    controls.pushScanEvent(0x36u, true);
    controls.pushScanEvent(0x2au, false);
    expect(has(controls.keyboardActions(), kActionRun),
           "one shift break must not release the other held shift");
    controls.pushScanEvent(0x36u, false);
    expect(!has(controls.keyboardActions(), kActionRun),
           "run must release after both shifts break");
}

void testSourceMergeAndSnapshots() {
    using namespace mpe_doom;

    Controls controls;
    controls.pushScanEvent(0x48u, true);
    controls.setJoystickSnapshot(kJoystickUp);
    controls.setJoystickSnapshot(0u);
    expect(has(controls.heldActions(), kActionForward),
           "joystick release must not clear keyboard forward");
    controls.pushScanEvent(0x48u, false);
    expect(!has(controls.heldActions(), kActionForward),
           "forward must clear after both sources release");

    controls.setJoystickSnapshot(kJoystickFire);
    controls.pushScanEvent(0x1du, true);
    controls.pushScanEvent(0x1du, false);
    expect(has(controls.heldActions(), kActionFire),
           "keyboard break must not clear joystick fire");
    controls.setJoystickSnapshot(0u);
    expect(!has(controls.heldActions(), kActionFire),
           "fire must clear after both sources release");

    const uint8_t snapshot = static_cast<uint8_t>(
        kJoystickUp | kJoystickRight | kJoystickFire);
    controls.setJoystickSnapshot(snapshot);
    expect(controls.joystickSnapshot() == snapshot,
           "complete joystick snapshot must be retained");
    expect(controls.joystickActions() ==
               (kActionForward | kActionTurnRight | kActionFire),
           "simultaneous joystick state must map as one snapshot");
    controls.setJoystickSnapshot(kJoystickLeft);
    expect(controls.joystickActions() == kActionTurnLeft,
           "new joystick snapshot must replace omitted controls");
}

void testQueueBackpressureAndReset() {
    using namespace mpe_doom;

    Controls controls;
    for (size_t i = 0u; i < Controls::kScanEventCapacity; ++i) {
        expect(controls.pushScanEvent(static_cast<uint8_t>(0x80u + i), true),
               "queue fill event must be accepted");
    }
    expect(!controls.pushScanEvent(0x48u, true), "full scan queue must reject");
    expect(!controls.scanHeld(0x48u),
           "rejected scan event must not change held state");
    expect(controls.rejectedScanEvents() == 1u, "rejection must be counted");

    ScanEvent event = {0u, 0u, false};
    expect(controls.popScanEvent(&event), "one queued event must pop");
    expect(controls.pushScanEvent(0x48u, true), "rejected event must be retryable");
    expect(controls.scanHeld(0x48u), "retried make must update held state");
    controls.setJoystickSnapshot(kJoystickFire);
    controls.reset();
    expect(controls.pendingScanEvents() == 0u, "reset must clear event queue");
    expect(controls.heldActions() == kActionNone, "reset must clear held actions");
    expect(controls.joystickSnapshot() == 0u, "reset must clear joystick state");
    expect(controls.rejectedScanEvents() == 0u, "reset must clear diagnostics");
}

void testSchedulerCadenceWrapAndBound() {
    using namespace mpe_doom;

    TicScheduler exact(64u);
    exact.reset(0u);
    uint32_t scheduled = 0u;
    for (uint32_t now = 1u; now <= 1000u; ++now) {
        const TicBatch batch = exact.advance(now);
        scheduled += batch.scheduled_tics;
    }
    expect(scheduled == 35u, "one second must schedule exactly 35 tics");
    expect(exact.runnableTicCount() == 35u, "regular cadence must run 35 tics");
    expect(exact.droppedTicCount() == 0u, "regular cadence must not drop tics");
    expect(exact.fractionalAccumulator() == 0u,
           "one exact second must leave no fractional time");

    TicScheduler bounded(4u);
    bounded.reset(100u);
    const TicBatch delayed = bounded.advance(1100u);
    expect(delayed.scheduled_tics == 35u, "delay must account for all due tics");
    expect(delayed.runnable_tics == 4u, "catch-up must be bounded");
    expect(delayed.dropped_tics == 31u, "excess catch-up must be dropped");
    expect(bounded.droppedTicCount() == 31u, "dropped tics must accumulate");
    const TicBatch next = bounded.advance(1200u);
    expect(next.scheduled_tics == 3u && next.runnable_tics == 3u,
           "scheduler must continue independently after a dropped batch");
    expect(bounded.droppedTicCount() == 31u,
           "non-dropped advance must preserve dropped count");

    TicScheduler wrapped(8u);
    wrapped.reset(0xfffffff0u);
    const TicBatch across_wrap = wrapped.advance(0x0000000eu);
    expect(across_wrap.elapsed_ms == 30u, "millisecond subtraction must wrap safely");
    expect(across_wrap.scheduled_tics == 1u,
           "30 wrapped milliseconds must schedule one 35 Hz tic");

    TicScheduler independent(8u);
    independent.reset(0u);
    const TicBatch first = independent.advance(29u);
    const TicBatch second = independent.advance(58u);
    const TicBatch third = independent.advance(87u);
    expect(first.scheduled_tics == 1u && second.scheduled_tics == 1u &&
               third.scheduled_tics == 1u,
           "timer advances must not depend on a frame acknowledgement");
    independent.reset(87u);
    expect(independent.runnableTicCount() == 0u &&
               independent.droppedTicCount() == 0u &&
               independent.fractionalAccumulator() == 0u,
           "scheduler reset must clear timing state and diagnostics");
}

void testRuntimeExitAndReentry() {
    using namespace mpe_doom;

    Runtime runtime(4u);
    expect(!runtime.active(), "runtime must start inactive");
    expect(!runtime.pushScanEvent(0x48u, true),
           "inactive runtime must reject keyboard input");
    expect(!runtime.setJoystickSnapshot(kJoystickFire),
           "inactive runtime must reject joystick input");

    runtime.reset(100u);
    expect(runtime.active(), "reset must activate runtime");
    expect(runtime.pushScanEvent(0x48u, true), "active input must be accepted");
    expect(runtime.setJoystickSnapshot(kJoystickFire),
           "active joystick snapshot must be accepted");
    expect(runtime.controls().heldActions() != kActionNone,
           "active inputs must hold controls");
    runtime.exit();
    expect(!runtime.active(), "exit must deactivate runtime");
    expect(runtime.controls().heldActions() == kActionNone,
           "exit must clear all held controls");
    expect(runtime.controls().pendingScanEvents() == 0u,
           "exit must clear ordered input events");
    expect(runtime.advance(200u).scheduled_tics == 0u,
           "exited runtime must not schedule tics");
    expect(!runtime.pushScanEvent(0x48u, true),
           "post-exit input must not become stuck");

    runtime.reset(500u);
    expect(runtime.controls().heldActions() == kActionNone,
           "reentry must begin with clean controls");
    expect(runtime.advance(529u).scheduled_tics == 1u,
           "reentry must begin a fresh 35 Hz clock");
}

}  // namespace

int main() {
    testActionDefinitionsAndMappings();
    testOrderedEventsAndHeldAliases();
    testSourceMergeAndSnapshots();
    testQueueBackpressureAndReset();
    testSchedulerCadenceWrapAndBound();
    testRuntimeExitAndReentry();

    if (failures != 0) {
        std::cerr << "Doom runtime host test: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "Doom runtime host test: PASS\n";
    return 0;
}
