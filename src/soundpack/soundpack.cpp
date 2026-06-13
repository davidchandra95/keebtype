#include "keebtype/soundpack/soundpack.hpp"

#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace keebtype {
namespace {

std::string pathString(const std::filesystem::path& path) {
  return path.lexically_normal().string();
}

bool parseKeyCode(const std::string& raw, int* out) {
  if (raw.empty() || out == nullptr) {
    return false;
  }

  std::size_t parsed = 0;
  long long value = 0;
  try {
    value = std::stoll(raw, &parsed, 10);
  } catch (...) {
    return false;
  }

  if (parsed != raw.size()) {
    return false;
  }
  if (value < 0 || value > std::numeric_limits<int>::max()) {
    return false;
  }

  *out = static_cast<int>(value);
  return true;
}

std::uint64_t msToFrames(std::int64_t ms, std::uint32_t sample_rate) {
  const auto value = static_cast<std::uint64_t>(ms);
  return (value * static_cast<std::uint64_t>(sample_rate)) / 1000ULL;
}

}  // namespace

SoundpackLoadResult loadSoundpack(const std::filesystem::path& soundpack_dir) {
  SoundpackLoadResult result;

  const auto config_path = soundpack_dir / "config.json";
  if (!std::filesystem::exists(config_path)) {
    result.error = "soundpack config does not exist: " + pathString(config_path);
    return result;
  }

  std::ifstream file(config_path);
  if (!file.is_open()) {
    result.error = "failed to open soundpack config: " + pathString(config_path);
    return result;
  }

  nlohmann::json parsed;
  try {
    file >> parsed;
  } catch (const std::exception& err) {
    result.error = std::string("invalid soundpack JSON: ") + err.what();
    return result;
  }

  if (!parsed.is_object()) {
    result.error = "soundpack config must be a JSON object";
    return result;
  }

  const auto getString = [&](const char* field, std::string* out) -> bool {
    const auto it = parsed.find(field);
    if (it == parsed.end() || !it->is_string() || it->get<std::string>().empty()) {
      result.error = std::string("soundpack field is missing or invalid: ") + field;
      return false;
    }
    *out = it->get<std::string>();
    return true;
  };

  Soundpack pack;
  pack.root = std::filesystem::absolute(soundpack_dir).lexically_normal();
  pack.config_file = std::filesystem::absolute(config_path).lexically_normal();

  if (!getString("id", &pack.id) || !getString("name", &pack.name)) {
    return result;
  }

  std::string sound_file_name;
  if (!getString("sound", &sound_file_name)) {
    return result;
  }
  pack.sound_file = (pack.root / sound_file_name).lexically_normal();
  if (!std::filesystem::exists(pack.sound_file)) {
    result.error = "soundpack audio file does not exist: " + pathString(pack.sound_file);
    return result;
  }

  const auto default_it = parsed.find("default");
  if (default_it != parsed.end()) {
    if (!default_it->is_boolean()) {
      result.error = "soundpack field is invalid: default";
      return result;
    }
    pack.is_default = default_it->get<bool>();
  }

  const auto defines_it = parsed.find("defines");
  if (defines_it == parsed.end() || !defines_it->is_object()) {
    result.error = "soundpack field is missing or invalid: defines";
    return result;
  }

  for (auto it = defines_it->begin(); it != defines_it->end(); ++it) {
    int key_code = 0;
    if (!parseKeyCode(it.key(), &key_code)) {
      result.error = "soundpack define key is not a non-negative integer: " + it.key();
      return result;
    }

    if (!it.value().is_array() || it.value().size() != 2 || !it.value()[0].is_number_integer() ||
        !it.value()[1].is_number_integer()) {
      result.error = "soundpack define must be [offset_ms, duration_ms] for key: " + it.key();
      return result;
    }

    const auto offset_ms = it.value()[0].get<std::int64_t>();
    const auto duration_ms = it.value()[1].get<std::int64_t>();
    if (offset_ms < 0 || duration_ms <= 0) {
      result.error = "soundpack define has invalid offset or duration for key: " + it.key();
      return result;
    }

    pack.slices.emplace(key_code, SoundSliceConfig{key_code, offset_ms, duration_ms});
  }

  if (pack.slices.empty()) {
    result.error = "soundpack defines must contain at least one slice";
    return result;
  }

  result.pack = std::move(pack);
  return result;
}

ResolveSoundpackResult resolveSoundpackSlices(
    const Soundpack& soundpack,
    std::uint32_t sample_rate,
    std::uint64_t total_frames) {
  ResolveSoundpackResult result;
  if (sample_rate == 0) {
    result.error = "audio sample rate is zero";
    return result;
  }
  if (total_frames == 0) {
    result.error = "decoded audio contains no frames";
    return result;
  }

  ResolvedSoundpack resolved;
  bool fallback_set = false;

  for (const auto& [key_code, slice] : soundpack.slices) {
    const auto start_frame = msToFrames(slice.offset_ms, sample_rate);
    const auto frame_count = msToFrames(slice.duration_ms, sample_rate);
    if (frame_count == 0) {
      std::ostringstream message;
      message << "soundpack slice is shorter than one frame for key: " << key_code;
      result.error = message.str();
      return result;
    }
    if (start_frame >= total_frames || frame_count > total_frames - start_frame) {
      std::ostringstream message;
      message << "soundpack slice exceeds decoded audio bounds for key: " << key_code;
      result.error = message.str();
      return result;
    }

    ResolvedSoundSlice resolved_slice{key_code, start_frame, frame_count};
    resolved.slices.emplace(key_code, resolved_slice);
    if (!fallback_set) {
      resolved.fallback = resolved_slice;
      fallback_set = true;
    }
  }

  if (!fallback_set) {
    result.error = "soundpack has no resolved slices";
    return result;
  }

  const auto space_it = resolved.slices.find(57);
  if (space_it != resolved.slices.end()) {
    resolved.fallback = space_it->second;
  }

  result.soundpack = std::move(resolved);
  return result;
}

}  // namespace keebtype
