#include "mpe_doom_video.h"

#include <limits.h>
#include <string.h>

namespace mpe_doom {
namespace {

const uint8_t kVicPalette[16][3] = {
    {0, 0, 0},       {255, 255, 255}, {136, 57, 50},   {103, 182, 189},
    {139, 63, 150},  {85, 160, 73},   {64, 49, 141},   {191, 206, 114},
    {139, 84, 41},   {87, 66, 0},     {184, 105, 98},  {80, 80, 80},
    {120, 120, 120}, {148, 224, 137}, {120, 105, 196}, {159, 159, 159}};

uint32_t distanceSquared(uint32_t rgb, uint8_t color) {
  uint32_t total = 0;
  for (uint8_t channel = 0; channel != 3; ++channel) {
    const int32_t delta = int32_t(uint8_t(rgb >> (channel * 8u))) -
                          kVicPalette[color][channel];
    total += uint32_t(delta * delta);
  }
  return total;
}

}  // namespace

const uint8_t *vicPaletteRgb() { return &kVicPalette[0][0]; }

bool Video::start(void *workspace, size_t bytes) {
  // A failed rebind must leave the converter detached. The previous arena may
  // already belong to another native engine by the time a restart is tried.
  target_ = nullptr;
  shown_ = nullptr;
  dirty_ = nullptr;
  distanceScratch_ = nullptr;
  sampleRgbScratch_ = nullptr;
  pendingCells_ = cursor_ = 0;
  acceptedFrames_ = droppedFrames_ = 0;
  initialComplete_ = false;
#if defined(MPE_DOOM_VIDEO_DIAGNOSTICS)
  searchSampleEvaluations_ = 0;
#endif
  if (!workspace || bytes < WorkspaceBytes) return false;
  target_ = static_cast<uint8_t *>(workspace);
  shown_ = target_ + FrameBytes;
  dirty_ = shown_ + FrameBytes;
  const uintptr_t scratchAddress =
      (reinterpret_cast<uintptr_t>(dirty_ + DirtyBytes) +
       alignof(uint32_t) - 1u) &
      ~uintptr_t(alignof(uint32_t) - 1u);
  distanceScratch_ = reinterpret_cast<uint32_t *>(scratchAddress);
  sampleRgbScratch_ = distanceScratch_ + CellSamples * VicColors;
  reset();
  return true;
}

void Video::reset() {
  if (target_) memset(target_, 0, WorkspaceBytes);
  pendingCells_ = cursor_ = 0;
  acceptedFrames_ = droppedFrames_ = 0;
  initialComplete_ = false;
#if defined(MPE_DOOM_VIDEO_DIAGNOSTICS)
  searchSampleEvaluations_ = 0;
#endif
}

void Video::renderCell(const uint8_t *framebuffer, const uint8_t *paletteRgb,
                       uint16_t cell, uint8_t out[CellBytes]) const {
  uint8_t weights[CellSamples] = {};
  uint8_t sampleToUnique[CellSamples];
  uint8_t order[CellSamples];
  uint32_t currentDistances[CellSamples];
  uint32_t lowerBoundSuffix[CellSamples + 1u];
  const uint16_t cellX = uint16_t((cell % 40u) * 8u);
  const uint16_t cellY = uint16_t((cell / 40u) * 8u);

  uint8_t sample = 0;
  uint8_t uniqueSamples = 0;
  for (uint8_t y = 0; y != 8; ++y) {
    const size_t row = size_t(cellY + y) * SourceWidth;
    for (uint8_t x = 0; x != 4; ++x, ++sample) {
      const size_t left = row + cellX + x * 2u;
      const uint8_t leftIndex = framebuffer[left];
      const uint8_t rightIndex = framebuffer[left + 1u];
      uint32_t rgb = 0;
      for (uint8_t channel = 0; channel != 3; ++channel) {
        const uint16_t sum = uint16_t(paletteRgb[size_t(leftIndex) * 3u + channel]) +
                             paletteRgb[size_t(rightIndex) * 3u + channel];
        rgb |= uint32_t(uint8_t((sum + 1u) >> 1u)) << (channel * 8u);
      }

      uint8_t unique = 0;
      while (unique != uniqueSamples && sampleRgbScratch_[unique] != rgb)
        ++unique;
      if (unique == uniqueSamples) {
        sampleRgbScratch_[unique] = rgb;
        uint32_t *distances = distanceScratch_ + size_t(unique) * VicColors;
        for (uint8_t color = 0; color != VicColors; ++color)
          distances[color] = distanceSquared(rgb, color);
        ++uniqueSamples;
      }
      sampleToUnique[sample] = unique;
      ++weights[unique];
    }
  }

  // A greedy three-color set supplies a strong exact upper bound. It does not
  // choose the result: the exhaustive search below still visits every triple
  // which could beat (or, until the first exact result, tie) that bound.
  for (uint8_t unique = 0; unique != uniqueSamples; ++unique)
    currentDistances[unique] =
        distanceScratch_[size_t(unique) * VicColors + BackgroundColor];
  bool greedySelected[VicColors] = {};
  uint32_t cutoff = UINT32_MAX;
  for (uint8_t slot = 0; slot != 3; ++slot) {
    uint32_t bestGreedyCost = UINT32_MAX;
    uint8_t bestGreedyColor = 1;
    for (uint8_t color = 1; color != VicColors; ++color) {
      if (greedySelected[color]) continue;
      uint32_t cost = 0;
      for (uint8_t unique = 0; unique != uniqueSamples; ++unique) {
        const uint32_t distance =
            distanceScratch_[size_t(unique) * VicColors + color];
        const uint32_t closest =
            distance < currentDistances[unique] ? distance
                                                : currentDistances[unique];
        cost += closest * weights[unique];
      }
      if (cost < bestGreedyCost) {
        bestGreedyCost = cost;
        bestGreedyColor = color;
      }
    }
    greedySelected[bestGreedyColor] = true;
    for (uint8_t unique = 0; unique != uniqueSamples; ++unique) {
      const uint32_t distance =
          distanceScratch_[size_t(unique) * VicColors + bestGreedyColor];
      if (distance < currentDistances[unique])
        currentDistances[unique] = distance;
    }
    cutoff = bestGreedyCost;
  }

  // Visit likely expensive samples first so losing triples cross the upper
  // bound quickly. The suffix sum is an admissible lower bound: it uses each
  // sample's closest color even though a real cell may select only three.
  for (uint8_t unique = 0; unique != uniqueSamples; ++unique)
    order[unique] = unique;
  for (uint8_t at = 1; at != uniqueSamples; ++at) {
    const uint8_t moving = order[at];
    const uint32_t movingScore =
        distanceScratch_[size_t(moving) * VicColors + BackgroundColor] *
        weights[moving];
    uint8_t position = at;
    while (position) {
      const uint8_t previous = order[position - 1u];
      const uint32_t previousScore =
          distanceScratch_[size_t(previous) * VicColors + BackgroundColor] *
          weights[previous];
      if (previousScore >= movingScore) break;
      order[position] = previous;
      --position;
    }
    order[position] = moving;
  }
  lowerBoundSuffix[uniqueSamples] = 0;
  for (uint8_t at = uniqueSamples; at != 0; --at) {
    const uint8_t unique = order[at - 1u];
    const uint32_t *distances =
        distanceScratch_ + size_t(unique) * VicColors;
    uint32_t lowerBound = distances[0];
    for (uint8_t color = 1; color != VicColors; ++color)
      if (distances[color] < lowerBound) lowerBound = distances[color];
    lowerBoundSuffix[at - 1u] =
        lowerBoundSuffix[at] + lowerBound * weights[unique];
  }

  // Black is the fixed shared background. Exhaustively select the other
  // three distinct VIC colors with the lowest total squared RGB error. The
  // ascending search and strict comparison make every tie lexicographic. A
  // branch is cut only when its accumulated cost plus the optimistic suffix
  // cannot improve the result; this changes search work, never the answer.
  uint32_t bestCost = UINT32_MAX;
  uint8_t best[3] = {1, 2, 3};
  for (uint8_t first = 1; first <= 13; ++first) {
    for (uint8_t second = uint8_t(first + 1u); second <= 14; ++second) {
      for (uint8_t third = uint8_t(second + 1u); third <= 15; ++third) {
        uint32_t cost = 0;
        bool pruned = false;
        for (uint8_t at = 0; at != uniqueSamples; ++at) {
          const uint8_t unique = order[at];
          const uint32_t *distances =
              distanceScratch_ + size_t(unique) * VicColors;
          uint32_t closest = distances[BackgroundColor];
          if (distances[first] < closest) closest = distances[first];
          if (distances[second] < closest) closest = distances[second];
          if (distances[third] < closest) closest = distances[third];
          cost += closest * weights[unique];
#if defined(MPE_DOOM_VIDEO_DIAGNOSTICS)
          ++searchSampleEvaluations_;
#endif
          const uint32_t optimistic = cost + lowerBoundSuffix[at + 1u];
          const bool cannotImprove =
              bestCost != UINT32_MAX ? optimistic >= bestCost
                                     : optimistic > cutoff;
          if (cannotImprove) {
            pruned = true;
            break;
          }
        }
        if (!pruned && cost < bestCost) {
          bestCost = cost;
          best[0] = first;
          best[1] = second;
          best[2] = third;
        }
      }
    }
  }

  sample = 0;
  for (uint8_t y = 0; y != 8; ++y) {
    uint8_t packed = 0;
    for (uint8_t x = 0; x != 4; ++x, ++sample) {
      const uint8_t unique = sampleToUnique[sample];
      const uint32_t *distances =
          distanceScratch_ + size_t(unique) * VicColors;
      uint8_t slot = 0;
      uint32_t closest = distances[BackgroundColor];
      for (uint8_t candidate = 0; candidate != 3; ++candidate) {
        const uint32_t candidateDistance = distances[best[candidate]];
        if (candidateDistance < closest) {
          closest = candidateDistance;
          slot = uint8_t(candidate + 1u);
        }
      }
      packed |= uint8_t(slot << (6u - x * 2u));
    }
    out[y] = packed;
  }
  out[8] = uint8_t((best[0] << 4u) | best[1]);
  out[9] = best[2];
}

StageResult Video::stageFrame(const uint8_t *framebuffer,
                              size_t framebufferBytes,
                              const uint8_t *paletteRgb,
                              size_t paletteBytes) {
  if (!target_ || !framebuffer || framebufferBytes < SourceBytes ||
      !paletteRgb || paletteBytes < PaletteBytes)
    return StageResult::InvalidArgument;
  if (busy()) {
    ++droppedFrames_;
    return StageResult::DroppedBusy;
  }

#if defined(MPE_DOOM_VIDEO_DIAGNOSTICS)
  searchSampleEvaluations_ = 0;
#endif
  memset(dirty_, 0, DirtyBytes);
  pendingCells_ = 0;
  cursor_ = 0;
  for (uint16_t cell = 0; cell != Cells; ++cell) {
    uint8_t rendered[CellBytes];
    renderCell(framebuffer, paletteRgb, cell, rendered);
    uint8_t *target = target_ + size_t(cell) * CellBytes;
    memcpy(target, rendered, CellBytes);
    if (!initialComplete_ || memcmp(target, shown_ + size_t(cell) * CellBytes,
                                    CellBytes)) {
      dirty_[cell >> 3u] |= uint8_t(1u << (cell & 7u));
      ++pendingCells_;
    }
  }
  ++acceptedFrames_;
  return StageResult::Accepted;
}

uint16_t Video::changes(uint8_t *records, uint16_t maximum) {
  if (!target_ || !records || !maximum || !pendingCells_) return 0;
  uint16_t count = 0;
  while (cursor_ != Cells && pendingCells_ && count != maximum) {
    const uint16_t cell = cursor_++;
    const uint8_t mask = uint8_t(1u << (cell & 7u));
    uint8_t &dirty = dirty_[cell >> 3u];
    if (!(dirty & mask)) continue;
    dirty &= uint8_t(~mask);

    uint8_t *record = records + size_t(count) * RecordBytes;
    record[0] = uint8_t(cell);
    record[1] = uint8_t(cell >> 8u);
    const uint8_t *target = target_ + size_t(cell) * CellBytes;
    memcpy(record + 2, target, CellBytes);
    memcpy(shown_ + size_t(cell) * CellBytes, target, CellBytes);
    --pendingCells_;
    ++count;
  }
  if (!pendingCells_) {
    cursor_ = 0;
    initialComplete_ = true;
  }
  return count;
}

}  // namespace mpe_doom
