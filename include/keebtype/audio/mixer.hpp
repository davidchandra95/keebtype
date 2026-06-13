#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "keebtype/soundpack/soundpack.hpp"

namespace keebtype {

struct DecodedAudio {
  std::uint32_t sample_rate = 0;
  std::uint32_t channels = 0;
  std::vector<float> samples;

  [[nodiscard]] std::uint64_t frameCount() const noexcept {
    if (channels == 0) {
      return 0;
    }
    return static_cast<std::uint64_t>(samples.size() / channels);
  }
};

class SoundMixer {
 public:
  SoundMixer(const DecodedAudio* audio, std::size_t max_voices, std::size_t queue_capacity);

  SoundMixer(const SoundMixer&) = delete;
  SoundMixer& operator=(const SoundMixer&) = delete;

  [[nodiscard]] bool requestPlay(const ResolvedSoundSlice& slice) noexcept;
  void mix(float* output, std::uint32_t frame_count) noexcept;
  void setVolumePercent(int volume_percent) noexcept;

  [[nodiscard]] int volumePercent() const noexcept {
    return volume_percent_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] std::uint64_t droppedRequests() const noexcept {
    return dropped_requests_.load(std::memory_order_relaxed);
  }

 private:
  struct Voice {
    ResolvedSoundSlice slice;
    std::uint64_t position_frame = 0;
    bool active = false;
  };

  struct PlayRequest {
    ResolvedSoundSlice slice;
  };

  void drainRequests() noexcept;
  void startVoice(const ResolvedSoundSlice& slice) noexcept;
  [[nodiscard]] bool isSliceValid(const ResolvedSoundSlice& slice) const noexcept;

  const DecodedAudio* audio_ = nullptr;
  std::vector<Voice> voices_;
  std::vector<PlayRequest> queue_;
  std::atomic<std::uint32_t> read_index_{0};
  std::atomic<std::uint32_t> write_index_{0};
  std::atomic<std::uint64_t> dropped_requests_{0};
  std::atomic<int> volume_percent_{55};
};

}  // namespace keebtype
