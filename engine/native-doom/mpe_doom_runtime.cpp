#include "mpe_doom_runtime.h"

#include <string.h>

namespace mpe_doom {

ActionMask defaultActionsForScan(uint8_t scan_code) {
    switch (scan_code) {
        case 0x01: return kActionMenu;                         // Escape
        case 0x02: return kActionWeapon1;
        case 0x03: return kActionWeapon2;
        case 0x04: return kActionWeapon3;
        case 0x05: return kActionWeapon4;
        case 0x06: return kActionWeapon5;
        case 0x07: return kActionWeapon6;
        case 0x08: return kActionWeapon7;
        case 0x0f: return kActionMap;                          // Tab
        case 0x11: return kActionForward;                      // W
        case 0x1d: return kActionFire;                         // Control
        case 0x1e: return kActionStrafeLeft;                   // A
        case 0x1f: return kActionBackward;                     // S
        case 0x20: return kActionStrafeRight;                  // D
        case 0x2a: return kActionRun;                          // Left Shift
        case 0x33: return kActionStrafeLeft;                   // Comma
        case 0x34: return kActionStrafeRight;                  // Period
        case 0x36: return kActionRun;                          // Right Shift
        case 0x39: return kActionUse;                          // Space
        case 0x48: return kActionForward;                      // Cursor up
        case 0x4b: return kActionTurnLeft;                     // Cursor left
        case 0x4d: return kActionTurnRight;                    // Cursor right
        case 0x50: return kActionBackward;                     // Cursor down
        default: return kActionNone;
    }
}

Controls::Controls() {
    reset();
}

void Controls::reset() {
    memset(scan_events_, 0, sizeof(scan_events_));
    memset(scan_held_, 0, sizeof(scan_held_));
    scan_read_ = 0u;
    scan_write_ = 0u;
    scan_count_ = 0u;
    next_sequence_ = 0u;
    rejected_scan_events_ = 0u;
    joystick_snapshot_ = 0u;
    keyboard_actions_ = kActionNone;
    joystick_actions_ = kActionNone;
}

bool Controls::pushScanEvent(uint8_t scan_code, bool pressed) {
    if (scan_count_ == kScanEventCapacity) {
        ++rejected_scan_events_;
        return false;
    }

    ScanEvent& event = scan_events_[scan_write_];
    event.sequence = next_sequence_++;
    event.scan_code = scan_code;
    event.pressed = pressed;
    scan_write_ = (scan_write_ + 1u) % kScanEventCapacity;
    ++scan_count_;

    setScanHeld(scan_code, pressed);
    rebuildKeyboardActions();
    return true;
}

bool Controls::popScanEvent(ScanEvent* event) {
    if (event == 0 || scan_count_ == 0u) {
        return false;
    }

    *event = scan_events_[scan_read_];
    scan_read_ = (scan_read_ + 1u) % kScanEventCapacity;
    --scan_count_;
    return true;
}

void Controls::setJoystickSnapshot(uint8_t snapshot) {
    joystick_snapshot_ = static_cast<uint8_t>(snapshot & kJoystickAll);
    rebuildJoystickActions();
}

bool Controls::scanHeld(uint8_t scan_code) const {
    const uint8_t mask = static_cast<uint8_t>(1u << (scan_code & 7u));
    return (scan_held_[scan_code >> 3] & mask) != 0u;
}

size_t Controls::pendingScanEvents() const {
    return scan_count_;
}

uint32_t Controls::rejectedScanEvents() const {
    return rejected_scan_events_;
}

uint8_t Controls::joystickSnapshot() const {
    return joystick_snapshot_;
}

ActionMask Controls::keyboardActions() const {
    return keyboard_actions_;
}

ActionMask Controls::joystickActions() const {
    return joystick_actions_;
}

ActionMask Controls::heldActions() const {
    return keyboard_actions_ | joystick_actions_;
}

void Controls::setScanHeld(uint8_t scan_code, bool pressed) {
    const uint8_t mask = static_cast<uint8_t>(1u << (scan_code & 7u));
    uint8_t& bits = scan_held_[scan_code >> 3];
    if (pressed) {
        bits = static_cast<uint8_t>(bits | mask);
    } else {
        bits = static_cast<uint8_t>(bits & static_cast<uint8_t>(~mask));
    }
}

void Controls::rebuildKeyboardActions() {
    ActionMask actions = kActionNone;
    for (uint32_t scan_code = 0u; scan_code <= 0xffu; ++scan_code) {
        if (scanHeld(static_cast<uint8_t>(scan_code))) {
            actions |= defaultActionsForScan(static_cast<uint8_t>(scan_code));
        }
    }
    keyboard_actions_ = actions;
}

void Controls::rebuildJoystickActions() {
    ActionMask actions = kActionNone;
    if ((joystick_snapshot_ & kJoystickUp) != 0u) {
        actions |= kActionForward;
    }
    if ((joystick_snapshot_ & kJoystickDown) != 0u) {
        actions |= kActionBackward;
    }
    if ((joystick_snapshot_ & kJoystickLeft) != 0u) {
        actions |= kActionTurnLeft;
    }
    if ((joystick_snapshot_ & kJoystickRight) != 0u) {
        actions |= kActionTurnRight;
    }
    if ((joystick_snapshot_ & kJoystickFire) != 0u) {
        actions |= kActionFire;
    }
    joystick_actions_ = actions;
}

TicScheduler::TicScheduler(uint32_t max_catch_up)
    : last_ms_(0u),
      accumulator_(0u),
      max_catch_up_(max_catch_up == 0u ? 1u : max_catch_up),
      runnable_tic_count_(0u),
      dropped_tic_count_(0u),
      active_(false) {
}

void TicScheduler::reset(uint32_t now_ms) {
    last_ms_ = now_ms;
    accumulator_ = 0u;
    runnable_tic_count_ = 0u;
    dropped_tic_count_ = 0u;
    active_ = true;
}

void TicScheduler::stop() {
    last_ms_ = 0u;
    accumulator_ = 0u;
    active_ = false;
}

TicBatch TicScheduler::advance(uint32_t now_ms) {
    TicBatch batch = {0u, 0u, 0u, 0u};
    if (!active_) {
        return batch;
    }

    batch.elapsed_ms = now_ms - last_ms_;
    last_ms_ = now_ms;

    const uint64_t scaled_time =
        static_cast<uint64_t>(accumulator_) +
        static_cast<uint64_t>(batch.elapsed_ms) * kTicsPerSecond;
    batch.scheduled_tics = static_cast<uint32_t>(scaled_time / 1000u);
    accumulator_ = static_cast<uint32_t>(scaled_time % 1000u);

    batch.runnable_tics = batch.scheduled_tics;
    if (batch.runnable_tics > max_catch_up_) {
        batch.runnable_tics = max_catch_up_;
    }
    batch.dropped_tics = batch.scheduled_tics - batch.runnable_tics;
    runnable_tic_count_ += batch.runnable_tics;
    dropped_tic_count_ += batch.dropped_tics;
    return batch;
}

bool TicScheduler::active() const {
    return active_;
}

uint32_t TicScheduler::maxCatchUp() const {
    return max_catch_up_;
}

uint32_t TicScheduler::fractionalAccumulator() const {
    return accumulator_;
}

uint64_t TicScheduler::runnableTicCount() const {
    return runnable_tic_count_;
}

uint64_t TicScheduler::droppedTicCount() const {
    return dropped_tic_count_;
}

Runtime::Runtime(uint32_t max_catch_up)
    : controls_(), scheduler_(max_catch_up), active_(false) {
}

void Runtime::reset(uint32_t now_ms) {
    controls_.reset();
    scheduler_.reset(now_ms);
    active_ = true;
}

void Runtime::exit() {
    controls_.reset();
    scheduler_.stop();
    active_ = false;
}

bool Runtime::pushScanEvent(uint8_t scan_code, bool pressed) {
    return active_ && controls_.pushScanEvent(scan_code, pressed);
}

bool Runtime::setJoystickSnapshot(uint8_t snapshot) {
    if (!active_) {
        return false;
    }
    controls_.setJoystickSnapshot(snapshot);
    return true;
}

TicBatch Runtime::advance(uint32_t now_ms) {
    if (!active_) {
        const TicBatch stopped = {0u, 0u, 0u, 0u};
        return stopped;
    }
    return scheduler_.advance(now_ms);
}

bool Runtime::active() const {
    return active_;
}

Controls& Runtime::controls() {
    return controls_;
}

const Controls& Runtime::controls() const {
    return controls_;
}

TicScheduler& Runtime::scheduler() {
    return scheduler_;
}

const TicScheduler& Runtime::scheduler() const {
    return scheduler_;
}

}  // namespace mpe_doom
