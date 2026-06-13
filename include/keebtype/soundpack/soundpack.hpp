#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace keebtype {

struct SoundSliceConfig {
  int key_code = 0;
  std::int64_t offset_ms = 0;
  std::int64_t duration_ms = 0;
};

struct Soundpack {
  std::filesystem::path root;
  std::filesystem::path config_file;
  std::filesystem::path sound_file;
  std::string id;
  std::string name;
  bool is_default = false;
  std::unordered_map<int, SoundSliceConfig> slices;
};

struct SoundpackLoadResult {
  std::optional<Soundpack> pack;
  std::string error;

  [[nodiscard]] bool ok() const noexcept { return pack.has_value(); }
};

struct ResolvedSoundSlice {
  int key_code = 0;
  std::uint64_t start_frame = 0;
  std::uint64_t frame_count = 0;
};

struct ResolvedSoundpack {
  std::unordered_map<int, ResolvedSoundSlice> slices;
  ResolvedSoundSlice fallback;
};

struct ResolveSoundpackResult {
  std::optional<ResolvedSoundpack> soundpack;
  std::string error;

  [[nodiscard]] bool ok() const noexcept { return soundpack.has_value(); }
};

SoundpackLoadResult loadSoundpack(const std::filesystem::path& soundpack_dir);

ResolveSoundpackResult resolveSoundpackSlices(
    const Soundpack& soundpack,
    std::uint32_t sample_rate,
    std::uint64_t total_frames);

}  // namespace keebtype

