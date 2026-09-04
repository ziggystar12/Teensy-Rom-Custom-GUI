// SPDX-License-Identifier: GPL-2.0-or-later

#include "mpe_doom_session.h"

extern "C" {
#include "mhs_native_adapter.h"
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

static_assert(MHS_DOOM_ACTION_FORWARD == mpe_doom::kActionForward);
static_assert(MHS_DOOM_ACTION_BACKWARD == mpe_doom::kActionBackward);
static_assert(MHS_DOOM_ACTION_TURN_LEFT == mpe_doom::kActionTurnLeft);
static_assert(MHS_DOOM_ACTION_TURN_RIGHT == mpe_doom::kActionTurnRight);
static_assert(MHS_DOOM_ACTION_STRAFE_LEFT == mpe_doom::kActionStrafeLeft);
static_assert(MHS_DOOM_ACTION_STRAFE_RIGHT == mpe_doom::kActionStrafeRight);
static_assert(MHS_DOOM_ACTION_FIRE == mpe_doom::kActionFire);
static_assert(MHS_DOOM_ACTION_USE == mpe_doom::kActionUse);
static_assert(MHS_DOOM_ACTION_RUN == mpe_doom::kActionRun);
static_assert(MHS_DOOM_ACTION_MAP == mpe_doom::kActionMap);
static_assert(MHS_DOOM_ACTION_MENU == mpe_doom::kActionMenu);
static_assert(MHS_DOOM_ACTION_WEAPON1 == mpe_doom::kActionWeapon1);
static_assert(MHS_DOOM_ACTION_WEAPON2 == mpe_doom::kActionWeapon2);
static_assert(MHS_DOOM_ACTION_WEAPON3 == mpe_doom::kActionWeapon3);
static_assert(MHS_DOOM_ACTION_WEAPON4 == mpe_doom::kActionWeapon4);
static_assert(MHS_DOOM_ACTION_WEAPON5 == mpe_doom::kActionWeapon5);
static_assert(MHS_DOOM_ACTION_WEAPON6 == mpe_doom::kActionWeapon6);
static_assert(MHS_DOOM_ACTION_WEAPON7 == mpe_doom::kActionWeapon7);
static_assert(MHS_DOOM_ACTION_ALL == mpe_doom::kActionAll);
static_assert(sizeof(void *) == 4u,
              "The MCUME native-session proof requires a 32-bit host");

namespace {

uint32_t fnv1a(const uint8_t *bytes, size_t length) {
  uint32_t hash = 2166136261u;
  for (size_t index = 0; index != length; ++index) {
    hash = (hash ^ bytes[index]) * 16777619u;
  }
  return hash;
}

struct ActualCore {
  const char *wad_path = nullptr;
  std::vector<uint32_t> frame_hashes;
  uint32_t calls = 0u;
  uint32_t delivered_scan_events = 0u;
  int initial_gametic = 0;
  bool exact_tics = true;
  bool events_drained = true;
  std::string callback_error;

  static bool start(void *opaque) {
    ActualCore &core = *static_cast<ActualCore *>(opaque);
    if (!MHS_DoomStart(core.wad_path)) return false;
    core.initial_gametic = MHS_DoomGametic();
    return true;
  }

  static bool runOneTic(void *opaque, const mpe_doom::CoreTicInput *input) {
    ActualCore &core = *static_cast<ActualCore *>(opaque);
    std::array<mhs_doom_action_transition_t,
               mpe_doom::Controls::kScanEventCapacity>
        ordered{};
    size_t ordered_count = 0u;
    if (input == nullptr) {
      core.callback_error = "session supplied a null one-tic input";
      return false;
    }
    core.delivered_scan_events += input->scan_event_count;
    for (uint8_t index = 0u; index != input->scan_event_count; ++index) {
      const mpe_doom::ActionMask action =
          mpe_doom::defaultActionsForScan(input->scan_events[index].scan_code);
      if (action == mpe_doom::kActionNone) continue;
      ordered[ordered_count++] = {
          action, static_cast<uint8_t>(input->scan_events[index].pressed)};
    }
    const int before = MHS_DoomGametic();
    if (!MHS_DoomRunOneTic(input->held_actions,
                           ordered_count != 0u ? ordered.data() : nullptr,
                           ordered_count)) {
      return false;
    }
    const int after = MHS_DoomGametic();
    core.exact_tics = core.exact_tics && after == before + 1;
    core.events_drained = core.events_drained && MHS_DoomEventsDrained();

    size_t bytes = 0u;
    const uint8_t *frame = MHS_DoomFramebuffer(&bytes);
    if (frame == nullptr || bytes != mpe_doom::Video::SourceBytes) {
      core.callback_error = "adapter returned an invalid indexed framebuffer";
      return false;
    }
    core.frame_hashes.push_back(fnv1a(frame, bytes));
    ++core.calls;
    return true;
  }

  static const uint8_t *framebuffer(void *, size_t *bytes) {
    return MHS_DoomFramebuffer(bytes);
  }

  static const uint8_t *palette(void *, size_t *bytes) {
    return MHS_DoomPaletteRgb(bytes);
  }

  static void stop(void *) { MHS_DoomStop(); }

  static const char *error(void *opaque) {
    ActualCore &core = *static_cast<ActualCore *>(opaque);
    return core.callback_error.empty() ? MHS_DoomLastError()
                                       : core.callback_error.c_str();
  }

  mpe_doom::CoreCallbacks callbacks() {
    return {this, &ActualCore::start, &ActualCore::runOneTic,
            &ActualCore::framebuffer, &ActualCore::palette,
            &ActualCore::stop, &ActualCore::error};
  }
};

struct ActionScan {
  mpe_doom::ActionMask action;
  uint8_t scan;
};

constexpr std::array<ActionScan, 18> kActionScans{{
    {mpe_doom::kActionForward, 0x48u},
    {mpe_doom::kActionBackward, 0x50u},
    {mpe_doom::kActionTurnLeft, 0x4bu},
    {mpe_doom::kActionTurnRight, 0x4du},
    {mpe_doom::kActionStrafeLeft, 0x1eu},
    {mpe_doom::kActionStrafeRight, 0x20u},
    {mpe_doom::kActionFire, 0x1du},
    {mpe_doom::kActionUse, 0x39u},
    {mpe_doom::kActionRun, 0x2au},
    {mpe_doom::kActionMap, 0x0fu},
    {mpe_doom::kActionMenu, 0x01u},
    {mpe_doom::kActionWeapon1, 0x02u},
    {mpe_doom::kActionWeapon2, 0x03u},
    {mpe_doom::kActionWeapon3, 0x04u},
    {mpe_doom::kActionWeapon4, 0x05u},
    {mpe_doom::kActionWeapon5, 0x06u},
    {mpe_doom::kActionWeapon6, 0x07u},
    {mpe_doom::kActionWeapon7, 0x08u},
}};

struct Proof {
  int failures = 0;
  uint32_t now_ms = 0u;
  uint32_t fractional = 0u;

  void expect(bool condition, const char *message) {
    if (!condition) {
      std::fprintf(stderr, "FAIL: %s\n", message);
      ++failures;
    }
  }

  mpe_doom::AdvanceResult oneScheduledTic(mpe_doom::Session &session) {
    const uint32_t delta = (1000u - fractional + 34u) / 35u;
    now_ms += delta;
    fractional += delta * 35u;
    expect(fractional >= 1000u && fractional < 2000u,
           "test cadence calculation left one-tic bounds");
    fractional -= 1000u;
    const int before = MHS_DoomGametic();
    const mpe_doom::AdvanceResult result = session.advance(now_ms);
    if (result.core_failed) {
      std::fprintf(stderr, "CORE ERROR: %s\n", session.lastError());
    }
    expect(!result.core_failed && result.executed_tics == 1u &&
               result.timing.scheduled_tics == 1u &&
               MHS_DoomGametic() == before + 1,
           "session did not request and receive exactly one gametic");
    return result;
  }

  void transition(mpe_doom::Session &session, uint8_t scan, bool pressed) {
    expect(session.updateInput({true, scan, pressed, 0u}),
           "session rejected an MHS action transition");
    oneScheduledTic(session);
  }

  void pulseTwice(mpe_doom::Session &session, uint8_t scan) {
    for (int pulse = 0; pulse != 2; ++pulse) {
      transition(session, scan, true);
      transition(session, scan, false);
    }
  }
};

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: mcume-native-session-test DOOM1.WAD\n");
    return 64;
  }

  Proof proof;
  ActualCore core;
  core.wad_path = argv[1];
  std::vector<uint8_t> workspace(mpe_doom::Video::WorkspaceBytes, 0u);
  mpe_doom::Session session(4u);
  proof.expect(session.start(core.callbacks(), workspace.data(), workspace.size(),
                             proof.now_ms),
               "actual MCUME core did not start through mpe_doom::Session");
  if (!session.active()) {
    std::fprintf(stderr, "adapter error: %s\n", session.lastError());
    return 1;
  }

  size_t frame_bytes = 0u;
  size_t palette_bytes = 0u;
  const uint8_t *initial_frame = MHS_DoomFramebuffer(&frame_bytes);
  const uint8_t *initial_palette = MHS_DoomPaletteRgb(&palette_bytes);
  const uint32_t palette_hash =
      initial_palette == nullptr ? 0u : fnv1a(initial_palette, palette_bytes);
  proof.expect(initial_frame != nullptr && frame_bytes == 64000u,
               "adapter did not expose a 320x200 indexed framebuffer");
  proof.expect(initial_palette != nullptr && palette_bytes == 768u &&
                   palette_hash != 0u,
               "adapter did not expose the current 768-byte RGB palette");

  const mpe_doom::AdvanceResult initial = session.advance(proof.now_ms);
  proof.expect(initial.executed_tics == 0u && initial.frame_attempted &&
                   initial.frame_result == mpe_doom::StageResult::Accepted,
               "session did not stage the initial adapter frame without a tic");

  // Let the freshly spawned pistol finish raising before using ammo as the
  // observable proof for a one-tic fire tap.
  for (int warm = 0; warm != 20; ++warm) {
    proof.oneScheduledTic(session);
  }

  // A make+break arriving before one gametic remains two ordered Doom events
  // even though the final held mask is clear. The adapter holds its press for
  // this gametic and applies the ordered release immediately afterward.
  const uint32_t tap_log_begin = MHS_DoomPostedEventCount();
  const int tap_ammo_before = MHS_DoomPlayerBulletAmmo();
  proof.expect(session.updateInput({true, 0x1du, true, 0u}) &&
                   session.updateInput({true, 0x1du, false, 0u}) &&
                   session.heldActions() == mpe_doom::kActionNone,
               "same-tic fire tap was not queued in order");
  proof.oneScheduledTic(session);
  proof.expect(MHS_DoomPostedEventCount() == tap_log_begin + 2u &&
                   MHS_DoomPostedActionAt(tap_log_begin) ==
                       MHS_DOOM_ACTION_FIRE &&
                   MHS_DoomPostedPressedAt(tap_log_begin) == 1 &&
                   MHS_DoomPostedActionAt(tap_log_begin + 1u) ==
                       MHS_DOOM_ACTION_FIRE &&
                   MHS_DoomPostedPressedAt(tap_log_begin + 1u) == 0,
               "same-tic make/break collapsed or changed event order");
  for (int settle = 0; settle != 8; ++settle) {
    proof.oneScheduledTic(session);
  }
  const int tap_ammo_after = MHS_DoomPlayerBulletAmmo();
  proof.expect(MHS_DoomLatchedTapCount() == 1u &&
                   tap_ammo_before - tap_ammo_after == 1 &&
                   session.heldActions() == mpe_doom::kActionNone,
               "same-tic fire tap did not produce one gameplay attack then release");

  // A one-second service delay belongs to Session: four calls execute and the
  // remaining 31 are dropped. The core must not perform its own catch-up.
  const int before_catch_up = MHS_DoomGametic();
  proof.now_ms += 1000u;
  const mpe_doom::AdvanceResult catch_up = session.advance(proof.now_ms);
  proof.expect(catch_up.timing.scheduled_tics == 35u &&
                   catch_up.timing.runnable_tics == 4u &&
                   catch_up.timing.dropped_tics == 31u &&
                   catch_up.executed_tics == 4u &&
                   MHS_DoomGametic() == before_catch_up + 4,
               "session/core catch-up ownership contract failed");

  const size_t movement_begin = core.frame_hashes.size();
  const int32_t movement_x_before = MHS_DoomPlayerX();
  const int32_t movement_y_before = MHS_DoomPlayerY();
  const uint32_t movement_angle_before = MHS_DoomPlayerAngle();
  const int movement_ammo_before = MHS_DoomPlayerBulletAmmo();
  for (uint32_t tic = 0u; tic != 420u; ++tic) {
    if (tic == 0u) {
      proof.expect(session.updateInput({true, 0x48u, true, 0u}) &&
                       session.updateInput({true, 0x1du, true, 0u}),
                   "moving-view held actions were rejected");
    }
    if (tic == 120u) {
      proof.expect(session.updateInput({true, 0x4du, true, 0u}),
                   "moving-view turn press was rejected");
    }
    if (tic == 220u) {
      proof.expect(session.updateInput({true, 0x4du, false, 0u}),
                   "moving-view turn release was rejected");
    }
    if (tic == 270u) {
      proof.expect(session.updateInput({true, 0x48u, false, 0u}) &&
                       session.updateInput({true, 0x1du, false, 0u}),
                   "moving-view releases were rejected");
    }
    proof.oneScheduledTic(session);
  }

  const auto first = core.frame_hashes.begin() +
                     static_cast<std::ptrdiff_t>(movement_begin);
  const auto last = core.frame_hashes.end();
  std::set<uint32_t> unique(first, last);
  uint32_t changed = 0u;
  for (auto current = first + 1; current != last; ++current) {
    if (*current != *(current - 1)) ++changed;
  }
  proof.expect(static_cast<size_t>(last - first) == 420u && changed >= 200u &&
                   unique.size() >= 128u && MHS_DoomInE1M1(),
               "exact-tic E1M1 run did not produce sufficient moving frames");
  const int32_t movement_x_after = MHS_DoomPlayerX();
  const int32_t movement_y_after = MHS_DoomPlayerY();
  const uint32_t movement_angle_after = MHS_DoomPlayerAngle();
  const int movement_ammo_after = MHS_DoomPlayerBulletAmmo();
  proof.expect((movement_x_after != movement_x_before ||
                movement_y_after != movement_y_before) &&
                   movement_angle_after != movement_angle_before &&
                   movement_ammo_after < movement_ammo_before,
               "adapter held movement/turn/fire did not change real player state");

  // Every held action is injected once down and once up. This exhaustive edge
  // sweep is deliberately after gameplay proof so map, menu, and weapon toggles
  // cannot mask movement/fire evidence. Map and menu each receive a second
  // pulse to restore their toggle state.
  for (const ActionScan &entry : kActionScans) {
    proof.expect(MHS_DoomActionKey(entry.action) >= 0,
                 "an MHS action has no Doom key mapping");
    if (entry.action == mpe_doom::kActionMap ||
        entry.action == mpe_doom::kActionMenu) {
      continue;
    }
    proof.transition(session, entry.scan, true);
    proof.transition(session, entry.scan, false);
  }
  proof.pulseTwice(session, 0x0fu);
  proof.pulseTwice(session, 0x01u);
  proof.expect(MHS_DoomPostedDownMask() == MHS_DOOM_ACTION_ALL &&
                   MHS_DoomPostedUpMask() == MHS_DOOM_ACTION_ALL,
               "not every held MHS action posted both Doom event edges");
  proof.expect(core.events_drained,
               "Doom responders did not drain the posted MHS event queue");

  proof.expect(core.exact_tics &&
                   MHS_DoomGametic() ==
                       core.initial_gametic + static_cast<int>(core.calls),
               "actual MCUME gametic count diverged from Session calls");
  proof.expect(core.delivered_scan_events == 48u,
               "Session did not deliver the queued MHS scan transitions");

  size_t final_bytes = 0u;
  const uint8_t *final_frame = MHS_DoomFramebuffer(&final_bytes);
  const uint32_t final_hash =
      final_frame == nullptr ? 0u : fnv1a(final_frame, final_bytes);
  const uint32_t posted_events = MHS_DoomPostedEventCount();
  const uint32_t posted_down = MHS_DoomPostedDownMask();
  const uint32_t posted_up = MHS_DoomPostedUpMask();
  const uint32_t core_calls = core.calls;
  const int final_gametic = MHS_DoomGametic();
  const bool in_e1m1 = MHS_DoomInE1M1();
  const uint32_t scheduler_resyncs = MHS_DoomSchedulerResyncs();
  proof.expect(scheduler_resyncs == 0u,
               "a legacy wall-clock command leaked into MHS scheduling");
  proof.expect(!session.reset(proof.now_ms) && !session.active() &&
                   std::strstr(session.lastError(), "cannot restart safely") !=
                       nullptr,
               "host-only MCUME lifecycle did not fail closed on Session reset");

  if (proof.failures != 0) {
    std::fprintf(stderr, "MCUME native Session proof: %d failure(s)\n",
                 proof.failures);
    return 1;
  }

  std::printf(
      "{\"status\":\"PASS\",\"pointerBits\":32,"
      "\"sessionCoreCalls\":%u,\"gameticDelta\":%d,"
      "\"catchUpExecuted\":%u,\"catchUpDropped\":%u,"
      "\"postedEvents\":%u,\"postedDownMask\":%u,"
      "\"postedUpMask\":%u,\"movementFrames\":%u,"
      "\"changedTransitions\":%u,\"uniqueFrameHashes\":%u,"
      "\"tapAmmoBefore\":%d,\"tapAmmoAfter\":%d,"
      "\"movementXBefore\":%d,\"movementXAfter\":%d,"
      "\"movementYBefore\":%d,\"movementYAfter\":%d,"
      "\"movementAngleBefore\":%u,\"movementAngleAfter\":%u,"
      "\"movementAmmoBefore\":%d,\"movementAmmoAfter\":%d,"
      "\"schedulerResyncs\":%u,"
      "\"finalFrameFnv1a\":\"%08x\","
      "\"paletteFnv1a\":\"%08x\",\"inE1M1\":%s}\n",
      core_calls, final_gametic - core.initial_gametic,
      catch_up.executed_tics, catch_up.timing.dropped_tics, posted_events,
      posted_down, posted_up, 420u, changed,
      static_cast<unsigned int>(unique.size()), tap_ammo_before, tap_ammo_after,
      movement_x_before, movement_x_after, movement_y_before, movement_y_after,
      movement_angle_before, movement_angle_after, movement_ammo_before,
      movement_ammo_after, scheduler_resyncs, final_hash, palette_hash,
      in_e1m1 ? "true" : "false");
  return 0;
}
