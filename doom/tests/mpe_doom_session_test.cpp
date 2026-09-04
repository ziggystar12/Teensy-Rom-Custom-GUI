#include "mpe_doom_session.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using mpe_doom::ActionMask;
using mpe_doom::AdvanceResult;
using mpe_doom::CoreCallbacks;
using mpe_doom::CoreTicInput;
using mpe_doom::InputUpdate;
using mpe_doom::ScanEvent;
using mpe_doom::Session;
using mpe_doom::StageResult;
using mpe_doom::Video;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

struct TicCapture {
  ActionMask held_actions = mpe_doom::kActionNone;
  uint8_t joystick_snapshot = 0;
  std::vector<ScanEvent> scan_events;
};

struct FakeCore {
  std::array<uint8_t, Video::SourceBytes> frame{};
  std::array<uint8_t, Video::PaletteBytes> palette{};
  std::vector<TicCapture> captures;
  std::string error_text;
  uint32_t start_calls = 0;
  uint32_t stop_calls = 0;
  uint32_t tic_calls = 0;
  uint32_t fail_tic_call = 0;
  bool fail_start = false;
  bool invalid_video = false;
  bool started = false;

  FakeCore() {
    const uint8_t *vic = mpe_doom::vicPaletteRgb();
    for (size_t index = 0; index != 256u; ++index) {
      std::memcpy(palette.data() + index * 3u, vic + (index & 15u) * 3u, 3u);
    }
  }

  static bool start(void *context) {
    FakeCore &core = *static_cast<FakeCore *>(context);
    ++core.start_calls;
    core.tic_calls = 0;
    core.captures.clear();
    core.frame.fill(0);
    if (core.fail_start) {
      core.started = false;
      core.error_text = "injected start failure";
      return false;
    }
    core.error_text.clear();
    core.started = true;
    return true;
  }

  static bool runOneTic(void *context, const CoreTicInput *input) {
    FakeCore &core = *static_cast<FakeCore *>(context);
    if (!core.started || !input) {
      core.error_text = "tic called while stopped";
      return false;
    }
    ++core.tic_calls;
    if (core.fail_tic_call && core.tic_calls == core.fail_tic_call) {
      core.error_text = "injected one-tic failure";
      return false;
    }

    TicCapture capture;
    capture.held_actions = input->held_actions;
    capture.joystick_snapshot = input->joystick_snapshot;
    if (input->scan_events && input->scan_event_count) {
      capture.scan_events.assign(input->scan_events,
                                 input->scan_events + input->scan_event_count);
    }
    core.captures.push_back(capture);

    // Make the latest simulated state visibly distinct in exactly one cell.
    core.frame.fill(0);
    const uint8_t color = uint8_t(1u + core.tic_calls % 15u);
    for (size_t y = 0; y != 8u; ++y) {
      for (size_t x = 0; x != 8u; ++x) {
        core.frame[y * Video::SourceWidth + x] = color;
      }
    }
    return true;
  }

  static const uint8_t *framebuffer(void *context, size_t *bytes) {
    FakeCore &core = *static_cast<FakeCore *>(context);
    if (bytes) *bytes = core.invalid_video ? 0u : core.frame.size();
    return core.invalid_video ? nullptr : core.frame.data();
  }

  static const uint8_t *paletteRgb(void *context, size_t *bytes) {
    FakeCore &core = *static_cast<FakeCore *>(context);
    if (bytes) *bytes = core.invalid_video ? 0u : core.palette.size();
    return core.invalid_video ? nullptr : core.palette.data();
  }

  static void stop(void *context) {
    FakeCore &core = *static_cast<FakeCore *>(context);
    ++core.stop_calls;
    core.started = false;
  }

  static const char *error(void *context) {
    return static_cast<FakeCore *>(context)->error_text.c_str();
  }

  CoreCallbacks callbacks() {
    const CoreCallbacks result = {this,      &FakeCore::start,
                                  &FakeCore::runOneTic,
                                  &FakeCore::framebuffer,
                                  &FakeCore::paletteRgb,
                                  &FakeCore::stop,
                                  &FakeCore::error};
    return result;
  }
};

struct GuardedWorkspace {
  static constexpr size_t GuardBytes = 32u;
  std::vector<uint8_t> storage;

  GuardedWorkspace()
      : storage(Video::WorkspaceBytes + GuardBytes * 2u, uint8_t(0xa5)) {}

  void *data() { return storage.data() + GuardBytes; }

  bool guardsIntact() const {
    return std::all_of(storage.begin(), storage.begin() + GuardBytes,
                       [](uint8_t byte) { return byte == 0xa5; }) &&
           std::all_of(storage.end() - GuardBytes, storage.end(),
                       [](uint8_t byte) { return byte == 0xa5; });
  }
};

void testCadenceSimultaneousControlsAndRecords() {
  FakeCore core;
  GuardedWorkspace workspace;
  Session session(64u);
  expect(session.start(core.callbacks(), workspace.data(), Video::WorkspaceBytes,
                       0u),
         "session start must accept a complete core and workspace");

  const InputUpdate simultaneous = {true, 0x48u, true,
                                    mpe_doom::kJoystickFire};
  expect(session.updateInput(simultaneous),
         "simultaneous keyboard and joystick input must be accepted");

  uint32_t executed = 0u;
  uint32_t dropped = 0u;
  for (uint32_t now = 1u; now <= 1000u; ++now) {
    const AdvanceResult result = session.advance(now);
    executed += result.executed_tics;
    dropped += result.timing.dropped_tics;
    expect(!result.core_failed, "regular 35 Hz advance must keep core active");
  }
  expect(executed == 35u && core.tic_calls == 35u,
         "one second must invoke the one-tic callback exactly 35 times");
  expect(dropped == 0u, "regular 35 Hz service must not drop simulation tics");
  expect(core.captures.size() == 35u,
         "each runnable tic must produce exactly one core callback");
  expect(!core.captures.empty() &&
             (core.captures[0].held_actions &
              (mpe_doom::kActionForward | mpe_doom::kActionFire)) ==
                 (mpe_doom::kActionForward | mpe_doom::kActionFire),
         "keyboard forward and joystick fire must be held simultaneously");
  expect(!core.captures.empty() && core.captures[0].scan_events.size() == 1u &&
             core.captures[0].scan_events[0].scan_code == 0x48u &&
             core.captures[0].scan_events[0].pressed,
         "accepted scan transition must reach the next runnable tic");
  expect(core.captures.size() < 2u || core.captures[1].scan_events.empty(),
         "ordered scan transitions must not be replayed on later tics");

  std::array<uint8_t, Video::RecordBytes * 2u + 2u> guarded_records{};
  guarded_records.fill(0xa5);
  const uint16_t count = session.changes(guarded_records.data() + 1u, 2u);
  expect(count == 2u, "session must proxy bounded Doom cell records");
  expect(guarded_records.front() == 0xa5 && guarded_records.back() == 0xa5,
         "12-byte record proxy must preserve caller bounds");
  expect(guarded_records[1] == 0u && guarded_records[2] == 0u &&
             guarded_records[1u + Video::RecordBytes] == 1u &&
             guarded_records[2u + Video::RecordBytes] == 0u,
         "initial record traversal must begin with unique cells zero and one");
  expect(workspace.guardsIntact(), "session video staging crossed workspace guards");
  session.exit();
}

void testTransactionalInputOrdering() {
  FakeCore core;
  GuardedWorkspace workspace;
  Session session;
  expect(session.start(core.callbacks(), workspace.data(), Video::WorkspaceBytes,
                       0u),
         "input transaction fixture must start");
  expect(session.updateInput(
             {false, 0u, false, mpe_doom::kJoystickLeft}),
         "baseline joystick snapshot must be accepted");

  for (size_t index = 0; index != mpe_doom::Controls::kScanEventCapacity;
       ++index) {
    expect(session.updateInput({true, uint8_t(0x80u + index), true,
                                mpe_doom::kJoystickLeft}),
           "scan queue fill transaction must be accepted");
  }
  expect(!session.updateInput(
             {true, 0x48u, true, mpe_doom::kJoystickFire}),
         "full scan queue must reject the whole input transaction");
  expect(session.joystickSnapshot() == mpe_doom::kJoystickLeft &&
             (session.heldActions() & mpe_doom::kActionFire) == 0u,
         "rejected scan transition must not partially update joystick state");

  const AdvanceResult tick = session.advance(29u);
  expect(tick.executed_tics == 1u && core.captures.size() == 1u,
         "accepted transaction queue must reach one runnable tic");
  expect(core.captures[0].scan_events.size() ==
             mpe_doom::Controls::kScanEventCapacity,
         "one tic must receive every queued transition in order");
  bool ordered = true;
  for (size_t index = 0; index != core.captures[0].scan_events.size(); ++index) {
    const ScanEvent &event = core.captures[0].scan_events[index];
    ordered = ordered && event.sequence == index &&
              event.scan_code == uint8_t(0x80u + index) && event.pressed;
  }
  expect(ordered, "transactional scan transitions lost their accepted order");
  expect(core.captures[0].joystick_snapshot == mpe_doom::kJoystickLeft,
         "core observed joystick data from a rejected input transaction");
  session.exit();
}

void drain(Session &session, std::vector<uint8_t> *records) {
  std::array<uint8_t, Video::RecordBytes * 19u> batch{};
  while (session.videoBusy()) {
    const uint16_t count = session.changes(batch.data(), 19u);
    expect(count != 0u, "busy session video must make bounded drain progress");
    if (!count) break;
    records->insert(records->end(), batch.begin(),
                    batch.begin() + size_t(count) * Video::RecordBytes);
    if (session.frameEndAwaitingAck()) {
      expect(session.acknowledgeFrameEnd(),
             "final cell batch ACK must release the frame boundary");
    }
  }
}

void testFinalPacketAckGatesNewFrame() {
  FakeCore core;
  GuardedWorkspace workspace;
  Session session;
  expect(session.start(core.callbacks(), workspace.data(), Video::WorkspaceBytes,
                       0u),
         "frame ACK fixture must start");
  const AdvanceResult initial = session.advance(0u);
  expect(initial.frame_result == StageResult::Accepted,
         "frame ACK fixture must stage its initial frame");

  std::vector<uint8_t> records(size_t(Video::Cells) * Video::RecordBytes);
  expect(session.changes(records.data(), Video::Cells) == Video::Cells,
         "one bounded test batch must return the complete initial frame");
  expect(session.pendingCells() == 0u && session.videoBusy() &&
             session.frameEndAwaitingAck(),
         "session must remain video-busy after copying the final packet");

  const AdvanceResult before_ack = session.advance(29u);
  expect(before_ack.executed_tics == 1u && !before_ack.frame_attempted &&
             session.acceptedFrames() == 1u,
         "new simulation may run but must not stage across the final ACK");
  expect(session.changes(records.data(), 1u) == 0u,
         "an unacknowledged frame end must reject another change batch");

  expect(session.acknowledgeFrameEnd(),
         "real final-packet ACK must release the staged-frame gate");
  expect(!session.acknowledgeFrameEnd(),
         "duplicate frame-end ACK must fail closed");
  const AdvanceResult after_ack = session.advance(29u);
  expect(after_ack.executed_tics == 0u && after_ack.frame_attempted &&
             after_ack.frame_result == StageResult::Accepted &&
             session.acceptedFrames() == 2u,
         "newest completed frame must stage immediately after final ACK");
  expect(workspace.guardsIntact(),
         "final-packet ACK path crossed workspace guards");
  session.exit();
}

void testBoundedCatchUpAndBusyFrameDrop() {
  FakeCore core;
  GuardedWorkspace workspace;
  Session session(4u);
  expect(session.start(core.callbacks(), workspace.data(), Video::WorkspaceBytes,
                       0u),
         "catch-up fixture must start");
  const AdvanceResult initial = session.advance(0u);
  expect(initial.executed_tics == 0u && initial.frame_attempted &&
             initial.frame_result == StageResult::Accepted &&
             session.pendingCells() == Video::Cells,
         "zero-time advance must stage the initial immutable frame");

  const AdvanceResult delayed = session.advance(1000u);
  expect(delayed.timing.scheduled_tics == 35u &&
             delayed.timing.runnable_tics == 4u &&
             delayed.timing.dropped_tics == 31u &&
             delayed.executed_tics == 4u && core.tic_calls == 4u,
         "one-second delay must run four one-tic calls and drop 31 tics");
  expect(delayed.frame_attempted &&
             delayed.frame_result == StageResult::DroppedBusy &&
             session.droppedFrames() == 1u,
         "new frame must drop while the immutable target is still busy");

  const AdvanceResult idle_busy_poll = session.advance(1000u);
  expect(idle_busy_poll.executed_tics == 0u &&
             !idle_busy_poll.frame_attempted && session.droppedFrames() == 1u,
         "zero-tic busy poll must not re-offer or recount the same frame");

  std::vector<uint8_t> initial_records;
  std::array<uint8_t, Video::RecordBytes> first_record{};
  expect(session.changes(first_record.data(), 1u) == 1u,
         "partial transport drain must publish one initial record");
  initial_records.insert(initial_records.end(), first_record.begin(),
                         first_record.end());
  const AdvanceResult partial_busy_poll = session.advance(1000u);
  expect(!partial_busy_poll.frame_attempted && session.droppedFrames() == 1u,
         "partial drain plus zero tics must not re-offer a deferred frame");

  const AdvanceResult following = session.advance(1100u);
  expect(following.timing.runnable_tics == 3u &&
             following.executed_tics == 3u && core.tic_calls == 7u &&
             following.frame_result == StageResult::DroppedBusy &&
             session.droppedFrames() == 2u,
         "simulation must continue while video awaits transport acceptance");

  drain(session, &initial_records);
  expect(initial_records.size() == size_t(Video::Cells) * Video::RecordBytes,
         "initial immutable target must still publish all 1000 cells");
  expect(initial_records.size() < 12u ||
             (std::all_of(initial_records.begin() + 2u,
                          initial_records.begin() + 10u,
                          [](uint8_t byte) { return byte == 0u; }) &&
              initial_records[10] == 0x12u && initial_records[11] == 3u),
         "busy simulation overwrote the previously staged black target");

  const AdvanceResult retry = session.advance(1100u);
  expect(retry.executed_tics == 0u &&
             retry.frame_result == StageResult::Accepted &&
             session.acceptedFrames() == 2u && session.videoBusy(),
         "latest simulated frame must stage immediately after the drain");
  expect(workspace.guardsIntact(), "busy/drop path crossed workspace guards");
  session.exit();
}

void testExitReentryAndFailedRestartDetachment() {
  FakeCore core;
  GuardedWorkspace workspace;
  Session session;
  expect(session.start(core.callbacks(), workspace.data(), Video::WorkspaceBytes,
                       100u),
         "exit fixture must start");
  expect(session.updateInput(
             {true, 0x48u, true, mpe_doom::kJoystickFire}),
         "exit fixture input must be accepted");
  session.exit();
  expect(!session.active() && session.heldActions() == mpe_doom::kActionNone &&
             session.pendingScanEvents() == 0u,
         "exit must clear all held and queued input");
  expect(!session.updateInput(
             {true, 0x48u, true, mpe_doom::kJoystickFire}),
         "exited session must reject input that could become stuck");

  expect(session.start(core.callbacks(), workspace.data(), Video::WorkspaceBytes,
                       500u),
         "clean reentry must reuse an explicitly rebound workspace");
  const AdvanceResult reentered = session.advance(529u);
  expect(reentered.executed_tics == 1u && core.captures.size() == 1u &&
             core.captures[0].held_actions == mpe_doom::kActionNone &&
             core.captures[0].scan_events.empty(),
         "reentry must not inherit stuck input from the prior session");

  core.fail_start = true;
  expect(!session.reset(700u), "injected core restart failure must surface");
  expect(!session.active() &&
             std::string(session.lastError()) == "injected start failure",
         "failed restart must stop the session and retain the core error");
  const std::vector<uint8_t> detached_snapshot = workspace.storage;
  core.frame.fill(9u);
  const AdvanceResult stopped = session.advance(1000u);
  std::array<uint8_t, Video::RecordBytes> record{};
  expect(stopped.executed_tics == 0u && !stopped.frame_attempted &&
             session.changes(record.data(), 1u) == 0u,
         "failed restart must expose neither simulation nor stale changes");
  expect(workspace.storage == detached_snapshot,
         "failed restart retained and modified its released workspace");
  expect(workspace.guardsIntact(), "restart failure crossed workspace guards");

  core.fail_start = false;
  expect(session.start(core.callbacks(), workspace.data(), Video::WorkspaceBytes,
                       1200u),
         "workspace must be reusable only after an explicit successful bind");
  expect(session.lastError()[0] == '\0',
         "successful start must clear the previous restart error");
  session.exit();
}

void testCoreErrorStopsSession() {
  FakeCore core;
  GuardedWorkspace workspace;
  Session session(4u);
  core.fail_tic_call = 2u;
  expect(session.start(core.callbacks(), workspace.data(), Video::WorkspaceBytes,
                       0u),
         "core failure fixture must start");
  const AdvanceResult result = session.advance(100u);
  expect(result.timing.runnable_tics == 3u && result.executed_tics == 1u &&
             result.core_failed && !result.frame_attempted,
         "one-tic failure must stop the batch before frame staging");
  expect(!session.active() &&
             std::string(session.lastError()) == "injected one-tic failure" &&
             core.stop_calls == 1u,
         "one-tic failure must call stop and preserve the adapter error");
}

}  // namespace

int main() {
  testCadenceSimultaneousControlsAndRecords();
  testTransactionalInputOrdering();
  testFinalPacketAckGatesNewFrame();
  testBoundedCatchUpAndBusyFrameDrop();
  testExitReentryAndFailedRestartDetachment();
  testCoreErrorStopsSession();

  if (failures) {
    std::cerr << "Doom session host test: " << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "Doom session host test: PASS (35 Hz, bounded catch-up, "
               "transactional input, final-packet ACK gate, clean exit/restart)\n";
  return 0;
}
