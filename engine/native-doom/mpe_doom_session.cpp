#include "mpe_doom_session.h"

#include <string.h>

namespace mpe_doom {

Session::Session(uint32_t max_catch_up)
    : runtime_(max_catch_up),
      video_(),
      core_(),
      last_error_(),
      core_started_(false),
      active_(false),
      frame_pending_(false),
      frame_deferred_busy_(false),
      frame_end_pending_ack_(false) {
  clearCallbacks();
  clearError();
}

bool Session::callbacksValid(const CoreCallbacks &core) const {
  return core.start && core.run_one_tic && core.framebuffer &&
         core.palette_rgb && core.stop && core.error;
}

void Session::clearCallbacks() { memset(&core_, 0, sizeof(core_)); }

void Session::clearError() { last_error_[0] = '\0'; }

void Session::setError(const char *message) {
  if (!message) message = "Unknown Doom session error.";
  size_t length = strlen(message);
  if (length >= ErrorBytes) length = ErrorBytes - 1u;
  memcpy(last_error_, message, length);
  last_error_[length] = '\0';
}

void Session::captureCoreError(const char *fallback) {
  const char *message = nullptr;
  if (core_.error) message = core_.error(core_.context);
  setError(message && message[0] ? message : fallback);
}

void Session::shutdown(bool preserve_error) {
  if (core_started_ && core_.stop) core_.stop(core_.context);
  core_started_ = false;
  active_ = false;
  frame_pending_ = false;
  frame_deferred_busy_ = false;
  frame_end_pending_ack_ = false;
  runtime_.exit();
  // Video::start() deliberately detaches before validating a new workspace.
  (void)video_.start(nullptr, 0u);
  clearCallbacks();
  if (!preserve_error) clearError();
}

bool Session::start(const CoreCallbacks &core, void *video_workspace,
                    size_t video_workspace_bytes, uint32_t now_ms) {
  shutdown(false);

  if (!callbacksValid(core)) {
    setError("Incomplete Doom core callback table.");
    return false;
  }
  if (!video_.start(video_workspace, video_workspace_bytes)) {
    setError("Invalid Doom video workspace.");
    return false;
  }

  core_ = core;
  if (!core_.start(core_.context)) {
    captureCoreError("Doom core start failed.");
    // A failed start may have partially initialized the adapter. stop() is
    // required to be idempotent and is the cleanup half of every start try.
    core_.stop(core_.context);
    (void)video_.start(nullptr, 0u);
    clearCallbacks();
    return false;
  }

  core_started_ = true;
  runtime_.reset(now_ms);
  active_ = true;
  frame_pending_ = true;
  frame_deferred_busy_ = false;
  frame_end_pending_ack_ = false;
  return true;
}

bool Session::reset(uint32_t now_ms) {
  if (!active_ || !core_started_) {
    setError("Doom session is not active.");
    return false;
  }

  runtime_.exit();
  core_.stop(core_.context);
  core_started_ = false;
  video_.reset();
  clearError();

  if (!core_.start(core_.context)) {
    captureCoreError("Doom core restart failed.");
    core_.stop(core_.context);
    active_ = false;
    frame_pending_ = false;
    frame_deferred_busy_ = false;
    frame_end_pending_ack_ = false;
    (void)video_.start(nullptr, 0u);
    clearCallbacks();
    return false;
  }

  core_started_ = true;
  runtime_.reset(now_ms);
  active_ = true;
  frame_pending_ = true;
  frame_deferred_busy_ = false;
  frame_end_pending_ack_ = false;
  return true;
}

void Session::exit() { shutdown(false); }

bool Session::updateInput(const InputUpdate &update) {
  if (!active_) return false;
  // Queue admission is the transaction boundary. The joystick snapshot is
  // changed only after an optional ordered scan transition is accepted.
  if (update.has_scan_event &&
      !runtime_.pushScanEvent(update.scan_code, update.pressed)) {
    return false;
  }
  return runtime_.setJoystickSnapshot(update.joystick_snapshot);
}

AdvanceResult Session::advance(uint32_t now_ms) {
  AdvanceResult result = {{0u, 0u, 0u, 0u},
                          0u,
                          false,
                          StageResult::InvalidArgument,
                          false};
  if (!active_ || !core_started_) return result;

  result.timing = runtime_.advance(now_ms);
  ScanEvent events[Controls::kScanEventCapacity];
  uint8_t event_count = 0u;

  for (uint32_t tic = 0u; tic != result.timing.runnable_tics; ++tic) {
    if (tic == 0u) {
      ScanEvent event = {0u, 0u, false};
      while (event_count != Controls::kScanEventCapacity &&
             runtime_.controls().popScanEvent(&event)) {
        events[event_count++] = event;
      }
    }

    const CoreTicInput input = {
        runtime_.controls().heldActions(),
        runtime_.controls().joystickSnapshot(),
        tic == 0u && event_count ? events : nullptr,
        tic == 0u ? event_count : uint8_t(0u)};
    if (!core_.run_one_tic(core_.context, &input)) {
      captureCoreError("Doom core tic failed.");
      result.core_failed = true;
      shutdown(true);
      return result;
    }
    ++result.executed_tics;
    frame_pending_ = true;
    frame_deferred_busy_ = false;
  }

  // Avoid repeatedly offering the same frame when advance() is polled faster
  // than 35 Hz. A busy drop leaves the newest complete framebuffer pending,
  // but does not offer it again until a new tic supersedes it or transport
  // drains. This keeps busy-drop accounting tied to newly produced frames.
  if (!frame_pending_) return result;
  if (frame_deferred_busy_ && videoBusy()) return result;

  // Video::changes() commits each returned cell to its local shown-state. The
  // stop-and-wait publisher owns an immutable copy of the last batch, but that
  // batch is not authoritative until its ACK arrives. Never compare or stage a
  // newer frame against the optimistic shown-state during this final window.
  if (frame_end_pending_ack_) {
    frame_deferred_busy_ = true;
    return result;
  }

  size_t framebuffer_bytes = 0u;
  size_t palette_bytes = 0u;
  const uint8_t *framebuffer =
      core_.framebuffer(core_.context, &framebuffer_bytes);
  const uint8_t *palette = core_.palette_rgb(core_.context, &palette_bytes);
  result.frame_attempted = true;
  result.frame_result = video_.stageFrame(
      framebuffer, framebuffer_bytes, palette, palette_bytes);
  if (result.frame_result == StageResult::Accepted) {
    frame_pending_ = false;
    frame_deferred_busy_ = false;
  } else if (result.frame_result == StageResult::DroppedBusy) {
    frame_deferred_busy_ = true;
  }
  if (result.frame_result == StageResult::InvalidArgument) {
    captureCoreError("Doom core returned invalid video buffers.");
    result.core_failed = true;
    shutdown(true);
  }
  return result;
}

uint16_t Session::changes(uint8_t *records, uint16_t maximum) {
  if (!active_ || frame_end_pending_ack_) return 0u;
  const uint16_t count = video_.changes(records, maximum);
  if (count && !video_.busy()) frame_end_pending_ack_ = true;
  return count;
}

bool Session::acknowledgeFrameEnd() {
  if (!active_ || !frame_end_pending_ack_) return false;
  frame_end_pending_ack_ = false;
  frame_deferred_busy_ = false;
  return true;
}

}  // namespace mpe_doom
