#include "keebtype/audio/mixer.hpp"

#include <algorithm>
#include <cmath>

namespace keebtype {

SoundMixer::SoundMixer(const DecodedAudio* audio, std::size_t max_voices, std::size_t queue_capacity)
    : audio_(audio),
      voices_(std::max<std::size_t>(1, max_voices)),
      queue_(std::max<std::size_t>(2, queue_capacity + 1)) {}

bool SoundMixer::requestPlay(const ResolvedSoundSlice& slice) noexcept {
  if (!isSliceValid(slice)) {
    return false;
  }

  const auto write = write_index_.load(std::memory_order_relaxed);
  const auto next = static_cast<std::uint32_t>((write + 1) % queue_.size());
  if (next == read_index_.load(std::memory_order_acquire)) {
    dropped_requests_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  queue_[write] = PlayRequest{slice};
  write_index_.store(next, std::memory_order_release);
  return true;
}

void SoundMixer::mix(float* output, std::uint32_t frame_count) noexcept {
  if (output == nullptr || audio_ == nullptr || audio_->channels == 0 || frame_count == 0) {
    return;
  }

  const auto channels = audio_->channels;
  const auto gain = static_cast<float>(volume_percent_.load(std::memory_order_relaxed)) / 100.0F;
  std::fill(output, output + (static_cast<std::size_t>(frame_count) * channels), 0.0F);

  drainRequests();

  for (auto& voice : voices_) {
    if (!voice.active) {
      continue;
    }

    for (std::uint32_t frame = 0; frame < frame_count; ++frame) {
      if (voice.position_frame >= voice.slice.frame_count) {
        voice.active = false;
        break;
      }

      const auto source_frame = voice.slice.start_frame + voice.position_frame;
      const auto source_base = static_cast<std::size_t>(source_frame * channels);
      const auto output_base = static_cast<std::size_t>(frame * channels);
      for (std::uint32_t channel = 0; channel < channels; ++channel) {
        output[output_base + channel] += audio_->samples[source_base + channel] * gain;
      }
      ++voice.position_frame;
    }
  }

  const auto sample_count = static_cast<std::size_t>(frame_count) * channels;
  for (std::size_t index = 0; index < sample_count; ++index) {
    output[index] = std::clamp(output[index], -1.0F, 1.0F);
  }
}

void SoundMixer::setVolumePercent(int volume_percent) noexcept {
  volume_percent_.store(std::clamp(volume_percent, 0, 100), std::memory_order_relaxed);
}

void SoundMixer::drainRequests() noexcept {
  auto read = read_index_.load(std::memory_order_relaxed);
  const auto write = write_index_.load(std::memory_order_acquire);

  while (read != write) {
    startVoice(queue_[read].slice);
    read = static_cast<std::uint32_t>((read + 1) % queue_.size());
    read_index_.store(read, std::memory_order_release);
  }
}

void SoundMixer::startVoice(const ResolvedSoundSlice& slice) noexcept {
  if (!isSliceValid(slice)) {
    return;
  }

  auto selected = voices_.end();
  for (auto it = voices_.begin(); it != voices_.end(); ++it) {
    if (!it->active) {
      selected = it;
      break;
    }
  }

  if (selected == voices_.end()) {
    selected = std::min_element(voices_.begin(), voices_.end(), [](const Voice& left, const Voice& right) {
      const auto left_remaining = left.slice.frame_count - left.position_frame;
      const auto right_remaining = right.slice.frame_count - right.position_frame;
      return left_remaining < right_remaining;
    });
  }

  selected->slice = slice;
  selected->position_frame = 0;
  selected->active = true;
}

bool SoundMixer::isSliceValid(const ResolvedSoundSlice& slice) const noexcept {
  if (audio_ == nullptr || audio_->channels == 0 || slice.frame_count == 0) {
    return false;
  }

  const auto total_frames = audio_->frameCount();
  return slice.start_frame < total_frames && slice.frame_count <= total_frames - slice.start_frame;
}

}  // namespace keebtype
