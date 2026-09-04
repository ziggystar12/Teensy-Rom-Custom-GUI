#ifndef MPE_DOOM_SESSION_H
#define MPE_DOOM_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "mpe_doom_runtime.h"
#include "mpe_doom_video.h"

namespace mpe_doom {

// Input presented to one, and only one, Doom simulation tic. Ordered scan
// transitions are delivered to the next runnable tic while held_actions keeps
// continuous movement independent of the host packet rate.
struct CoreTicInput {
  ActionMask held_actions;
  uint8_t joystick_snapshot;
  const ScanEvent *scan_events;
  uint8_t scan_event_count;
};

// Adapter boundary for a Doom core. run_one_tic must advance exactly one Doom
// gametic and return; a self-scheduling driver such as MCUME D_DoomLoop is not
// a valid implementation. The framebuffer remains core-owned. Video copies an
// immutable target before advance() returns, so the core may reuse its buffer.
// stop must also clean up a partially initialized, failed start attempt.
struct CoreCallbacks {
  void *context;
  bool (*start)(void *context);
  bool (*run_one_tic)(void *context, const CoreTicInput *input);
  const uint8_t *(*framebuffer)(void *context, size_t *bytes);
  const uint8_t *(*palette_rgb)(void *context, size_t *bytes);
  void (*stop)(void *context);
  const char *(*error)(void *context);
};

// One packet carries an optional ordered keyboard transition followed by the
// complete port-2 joystick state. If the scan queue is full, neither part is
// accepted; callers can retry the same packet without a partial input update.
struct InputUpdate {
  bool has_scan_event;
  uint8_t scan_code;
  bool pressed;
  uint8_t joystick_snapshot;
};

struct AdvanceResult {
  TicBatch timing;
  uint32_t executed_tics;
  bool frame_attempted;
  StageResult frame_result;
  bool core_failed;
};

class Session {
 public:
  explicit Session(uint32_t max_catch_up = 4u);

  // start() is fail-closed: it stops and detaches any previous session before
  // validating the new core and workspace.
  MPE_DOOM_CODE bool start(const CoreCallbacks &core, void *video_workspace,
                           size_t video_workspace_bytes, uint32_t now_ms);

  // Restarts the currently bound core using the same video workspace. A failed
  // core restart detaches that workspace so it cannot be modified later.
  MPE_DOOM_CODE bool reset(uint32_t now_ms);
  MPE_DOOM_CODE void exit();

  MPE_DOOM_CODE bool updateInput(const InputUpdate &update);
  MPE_DOOM_CODE AdvanceResult advance(uint32_t now_ms);

  // Proxies the established 12-byte cell-record format. Returned records must
  // be accepted by the transport before the caller asks for another batch.
  // When the returned batch completes a frame, the session remains video-busy
  // until acknowledgeFrameEnd() is called after that packet's real ACK. This
  // prevents a newer target from being staged across the unacknowledged frame
  // boundary; retries remain owned by the transport's immutable packet buffer.
  MPE_DOOM_CODE uint16_t changes(uint8_t *records, uint16_t maximum);
  MPE_DOOM_CODE bool acknowledgeFrameEnd();

  bool active() const { return active_; }
  ActionMask heldActions() const { return runtime_.controls().heldActions(); }
  uint8_t joystickSnapshot() const {
    return runtime_.controls().joystickSnapshot();
  }
  size_t pendingScanEvents() const {
    return runtime_.controls().pendingScanEvents();
  }
  bool videoBusy() const { return video_.busy() || frame_end_pending_ack_; }
  bool frameEndAwaitingAck() const { return frame_end_pending_ack_; }
  uint16_t pendingCells() const { return video_.pendingCells(); }
  uint32_t acceptedFrames() const { return video_.acceptedFrames(); }
  uint32_t droppedFrames() const { return video_.droppedFrames(); }
  const char *lastError() const { return last_error_; }

 private:
  static constexpr size_t ErrorBytes = 128u;

  Runtime runtime_;
  Video video_;
  CoreCallbacks core_;
  char last_error_[ErrorBytes];
  bool core_started_;
  bool active_;
  bool frame_pending_;
  bool frame_deferred_busy_;
  bool frame_end_pending_ack_;

  MPE_DOOM_CODE bool callbacksValid(const CoreCallbacks &core) const;
  MPE_DOOM_CODE void clearCallbacks();
  MPE_DOOM_CODE void clearError();
  MPE_DOOM_CODE void setError(const char *message);
  MPE_DOOM_CODE void captureCoreError(const char *fallback);
  MPE_DOOM_CODE void shutdown(bool preserve_error);

  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
};

}  // namespace mpe_doom

#endif  // MPE_DOOM_SESSION_H
