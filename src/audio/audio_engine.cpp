#include "keebtype/audio/audio_engine.hpp"

#include <cstdlib>
#include <sstream>
#include <utility>

#include "miniaudio.h"

extern "C" int stb_vorbis_decode_filename(const char* filename, int* channels, int* sample_rate, short** output);

namespace keebtype {
namespace {

struct DecodeResult {
  DecodedAudio audio;
  std::string error;

  [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

DecodeResult decodeOggFile(const std::filesystem::path& path) {
  DecodeResult result;

  int channels = 0;
  int sample_rate = 0;
  short* decoded = nullptr;
  const auto frame_count = stb_vorbis_decode_filename(path.string().c_str(), &channels, &sample_rate, &decoded);
  if (frame_count <= 0 || decoded == nullptr) {
    result.error = "failed to decode OGG sound file: " + path.string();
    return result;
  }

  if (channels <= 0 || channels > 8 || sample_rate <= 0) {
    std::free(decoded);
    std::ostringstream message;
    message << "decoded OGG has unsupported format: channels=" << channels
            << " sample_rate=" << sample_rate;
    result.error = message.str();
    return result;
  }

  const auto sample_count = static_cast<std::size_t>(frame_count) * static_cast<std::size_t>(channels);
  result.audio.sample_rate = static_cast<std::uint32_t>(sample_rate);
  result.audio.channels = static_cast<std::uint32_t>(channels);
  result.audio.samples.resize(sample_count);

  for (std::size_t index = 0; index < sample_count; ++index) {
    result.audio.samples[index] = static_cast<float>(decoded[index]) / 32768.0F;
  }

  std::free(decoded);
  return result;
}

}  // namespace

AudioEngine::AudioEngine(DecodedAudio audio, ResolvedSoundpack soundpack, std::string soundpack_name)
    : audio_(std::move(audio)),
      soundpack_(std::move(soundpack)),
      mixer_(std::make_unique<SoundMixer>(&audio_, 32, 256)),
      soundpack_name_(std::move(soundpack_name)),
      device_(std::make_unique<ma_device>()) {}

AudioEngine::~AudioEngine() {
  shutdown();
}

AudioEngine::InitResult AudioEngine::create(const Soundpack& soundpack) {
  InitResult result;

  auto decode = decodeOggFile(soundpack.sound_file);
  if (!decode.ok()) {
    result.error = decode.error;
    return result;
  }

  auto resolved = resolveSoundpackSlices(soundpack, decode.audio.sample_rate, decode.audio.frameCount());
  if (!resolved.ok()) {
    result.error = resolved.error;
    return result;
  }

  auto engine = std::unique_ptr<AudioEngine>(
      new AudioEngine(std::move(decode.audio), std::move(*resolved.soundpack), soundpack.name));

  std::string device_error;
  if (!engine->initDevice(&device_error)) {
    result.error = device_error;
    return result;
  }

  result.engine = std::move(engine);
  return result;
}

bool AudioEngine::playKey(int key_code) noexcept {
  const auto it = soundpack_.slices.find(key_code);
  const auto& slice = (it == soundpack_.slices.end()) ? soundpack_.fallback : it->second;
  return mixer_->requestPlay(slice);
}

void AudioEngine::setVolumePercent(int volume_percent) noexcept {
  mixer_->setVolumePercent(volume_percent);
}

int AudioEngine::volumePercent() const noexcept {
  return mixer_->volumePercent();
}

std::uint64_t AudioEngine::droppedRequests() const noexcept {
  return mixer_->droppedRequests();
}

void AudioEngine::dataCallback(ma_device* device, void* output, const void* input, unsigned int frame_count) {
  (void)input;
  auto* engine = static_cast<AudioEngine*>(device->pUserData);
  if (engine == nullptr || engine->mixer_ == nullptr) {
    return;
  }
  engine->mixer_->mix(static_cast<float*>(output), frame_count);
}

bool AudioEngine::initDevice(std::string* error) {
  auto config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_f32;
  config.playback.channels = audio_.channels;
  config.sampleRate = audio_.sample_rate;
  config.dataCallback = &AudioEngine::dataCallback;
  config.pUserData = this;

  const auto init_result = ma_device_init(nullptr, &config, device_.get());
  if (init_result != MA_SUCCESS) {
    if (error != nullptr) {
      *error = "failed to initialize audio device";
    }
    return false;
  }
  device_initialized_ = true;

  const auto start_result = ma_device_start(device_.get());
  if (start_result != MA_SUCCESS) {
    ma_device_uninit(device_.get());
    device_initialized_ = false;
    if (error != nullptr) {
      *error = "failed to start audio device";
    }
    return false;
  }

  device_started_ = true;
  return true;
}

void AudioEngine::shutdown() noexcept {
  if (device_ == nullptr) {
    return;
  }
  if (device_started_) {
    ma_device_stop(device_.get());
    device_started_ = false;
  }
  if (device_initialized_) {
    ma_device_uninit(device_.get());
    device_initialized_ = false;
  }
}

}  // namespace keebtype
