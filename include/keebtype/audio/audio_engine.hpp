#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "keebtype/audio/mixer.hpp"
#include "keebtype/soundpack/soundpack.hpp"

struct ma_device;

namespace keebtype {

class AudioEngine {
 public:
  struct PreparedSoundpack {
    DecodedAudio audio;
    ResolvedSoundpack soundpack;
    std::string soundpack_name;
  };

  struct PrepareResult {
    std::optional<PreparedSoundpack> prepared;
    std::string error;

    [[nodiscard]] bool ok() const noexcept { return prepared.has_value(); }
  };

  struct InitResult {
    std::unique_ptr<AudioEngine> engine;
    std::string error;

    [[nodiscard]] bool ok() const noexcept { return engine != nullptr; }
  };

  static PrepareResult prepare(const Soundpack& soundpack);
  static InitResult create(const Soundpack& soundpack);
  static InitResult create(PreparedSoundpack prepared);

  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  ~AudioEngine();

  [[nodiscard]] bool playKey(int key_code) noexcept;
  void setVolumePercent(int volume_percent) noexcept;
  [[nodiscard]] int volumePercent() const noexcept;
  [[nodiscard]] const std::string& soundpackName() const noexcept { return soundpack_name_; }
  [[nodiscard]] std::uint64_t droppedRequests() const noexcept;

 private:
  AudioEngine(DecodedAudio audio, ResolvedSoundpack soundpack, std::string soundpack_name);

  static void dataCallback(ma_device* device, void* output, const void* input, unsigned int frame_count);

  [[nodiscard]] bool initDevice(std::string* error);
  void shutdown() noexcept;

  DecodedAudio audio_;
  ResolvedSoundpack soundpack_;
  std::unique_ptr<SoundMixer> mixer_;
  std::string soundpack_name_;
  std::unique_ptr<ma_device> device_;
  bool device_initialized_ = false;
  bool device_started_ = false;
};

}  // namespace keebtype
