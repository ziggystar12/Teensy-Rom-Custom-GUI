#ifndef MPE_DOOM_RUNTIME_H
#define MPE_DOOM_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

namespace mpe_doom {

typedef uint32_t ActionMask;

enum Action : ActionMask {
    kActionNone = 0u,
    kActionForward = 1u << 0,
    kActionBackward = 1u << 1,
    kActionTurnLeft = 1u << 2,
    kActionTurnRight = 1u << 3,
    kActionStrafeLeft = 1u << 4,
    kActionStrafeRight = 1u << 5,
    kActionFire = 1u << 6,
    kActionUse = 1u << 7,
    kActionRun = 1u << 8,
    kActionMap = 1u << 9,
    kActionMenu = 1u << 10,
    kActionWeapon1 = 1u << 11,
    kActionWeapon2 = 1u << 12,
    kActionWeapon3 = 1u << 13,
    kActionWeapon4 = 1u << 14,
    kActionWeapon5 = 1u << 15,
    kActionWeapon6 = 1u << 16,
    kActionWeapon7 = 1u << 17,
    kActionAll = (1u << 18) - 1u
};

enum JoystickBit : uint8_t {
    kJoystickUp = 1u << 0,
    kJoystickDown = 1u << 1,
    kJoystickLeft = 1u << 2,
    kJoystickRight = 1u << 3,
    kJoystickFire = 1u << 4,
    kJoystickAll = (1u << 5) - 1u
};

struct ScanEvent {
    uint32_t sequence;
    uint8_t scan_code;
    bool pressed;
};

ActionMask defaultActionsForScan(uint8_t scan_code);

class Controls {
public:
    static constexpr size_t kScanEventCapacity = 32u;

    Controls();

    void reset();

    // Returns false without changing held state when the ordered queue is full.
    bool pushScanEvent(uint8_t scan_code, bool pressed);
    bool popScanEvent(ScanEvent* event);

    // Each call replaces the complete normalized port-2 joystick snapshot.
    void setJoystickSnapshot(uint8_t snapshot);

    bool scanHeld(uint8_t scan_code) const;
    size_t pendingScanEvents() const;
    uint32_t rejectedScanEvents() const;
    uint8_t joystickSnapshot() const;
    ActionMask keyboardActions() const;
    ActionMask joystickActions() const;
    ActionMask heldActions() const;

private:
    void setScanHeld(uint8_t scan_code, bool pressed);
    void rebuildKeyboardActions();
    void rebuildJoystickActions();

    ScanEvent scan_events_[kScanEventCapacity];
    uint8_t scan_held_[32];
    size_t scan_read_;
    size_t scan_write_;
    size_t scan_count_;
    uint32_t next_sequence_;
    uint32_t rejected_scan_events_;
    uint8_t joystick_snapshot_;
    ActionMask keyboard_actions_;
    ActionMask joystick_actions_;
};

struct TicBatch {
    uint32_t elapsed_ms;
    uint32_t scheduled_tics;
    uint32_t runnable_tics;
    uint32_t dropped_tics;
};

class TicScheduler {
public:
    static constexpr uint32_t kTicsPerSecond = 35u;

    explicit TicScheduler(uint32_t max_catch_up = 4u);

    void reset(uint32_t now_ms);
    void stop();
    TicBatch advance(uint32_t now_ms);

    bool active() const;
    uint32_t maxCatchUp() const;
    uint32_t fractionalAccumulator() const;
    uint64_t runnableTicCount() const;
    uint64_t droppedTicCount() const;

private:
    uint32_t last_ms_;
    uint32_t accumulator_;
    uint32_t max_catch_up_;
    uint64_t runnable_tic_count_;
    uint64_t dropped_tic_count_;
    bool active_;
};

class Runtime {
public:
    explicit Runtime(uint32_t max_catch_up = 4u);

    void reset(uint32_t now_ms);
    void exit();

    bool pushScanEvent(uint8_t scan_code, bool pressed);
    bool setJoystickSnapshot(uint8_t snapshot);
    TicBatch advance(uint32_t now_ms);

    bool active() const;
    Controls& controls();
    const Controls& controls() const;
    TicScheduler& scheduler();
    const TicScheduler& scheduler() const;

private:
    Controls controls_;
    TicScheduler scheduler_;
    bool active_;
};

}  // namespace mpe_doom

#endif  // MPE_DOOM_RUNTIME_H
