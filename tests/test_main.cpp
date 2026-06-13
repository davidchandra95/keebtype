#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "keebtype/audio/mixer.hpp"
#include "keebtype/input/key_codes.hpp"
#include "keebtype/soundpack/soundpack.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << "\n";
  }
}

std::filesystem::path tempRoot(const std::string& name) {
  auto root = std::filesystem::temp_directory_path() / ("keebtype-test-" + name);
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  return root;
}

void writeFile(const std::filesystem::path& path, const std::string& data) {
  std::ofstream file(path, std::ios::binary);
  file << data;
}

void testLoadsBundledSoundpack() {
  const auto root = std::filesystem::path(KEEBTYPE_SOURCE_DIR) / "soundpacks" / "cherrymx-brown-abs";
  auto result = keebtype::loadSoundpack(root);
  check(result.ok(), "bundled soundpack should load: " + result.error);
  if (!result.ok()) {
    return;
  }

  check(result.pack->id == "sound-pack-1200000000005", "bundled soundpack id should match");
  check(result.pack->name == "CherryMX Brown - ABS keycaps", "bundled soundpack name should match");
  check(result.pack->is_default, "bundled soundpack should be default");
  check(result.pack->sound_file.filename() == "sound.ogg", "bundled soundpack should point at sound.ogg");
  check(!result.pack->slices.empty(), "bundled soundpack should include slices");
}

void testRejectsInvalidJson() {
  const auto root = tempRoot("invalid-json");
  writeFile(root / "config.json", "{not json");
  auto result = keebtype::loadSoundpack(root);
  check(!result.ok(), "invalid JSON should fail");
}

void testRejectsMissingSoundFile() {
  const auto root = tempRoot("missing-sound");
  writeFile(
      root / "config.json",
      R"json({
        "id": "pack",
        "name": "Pack",
        "default": true,
        "sound": "missing.ogg",
        "defines": { "57": [0, 10] }
      })json");
  auto result = keebtype::loadSoundpack(root);
  check(!result.ok(), "missing sound file should fail");
}

void testRejectsNegativeSlice() {
  const auto root = tempRoot("negative-slice");
  writeFile(root / "sound.ogg", "placeholder");
  writeFile(
      root / "config.json",
      R"json({
        "id": "pack",
        "name": "Pack",
        "default": true,
        "sound": "sound.ogg",
        "defines": { "57": [-1, 10] }
      })json");
  auto result = keebtype::loadSoundpack(root);
  check(!result.ok(), "negative slice offset should fail");
}

void testSliceBoundsValidationAndFallback() {
  const auto root = tempRoot("slice-bounds");
  writeFile(root / "sound.ogg", "placeholder");
  writeFile(
      root / "config.json",
      R"json({
        "id": "pack",
        "name": "Pack",
        "default": true,
        "sound": "sound.ogg",
        "defines": {
          "57": [10, 20],
          "30": [40, 10]
        }
      })json");

  auto loaded = keebtype::loadSoundpack(root);
  check(loaded.ok(), "valid synthetic soundpack should load: " + loaded.error);
  auto resolved = keebtype::resolveSoundpackSlices(*loaded.pack, 1000, 100);
  check(resolved.ok(), "valid synthetic slices should resolve: " + resolved.error);
  if (resolved.ok()) {
    const auto space = resolved.soundpack->slices.at(keebtype::key::Space);
    check(space.start_frame == 10, "space slice should convert offset ms to frames");
    check(space.frame_count == 20, "space slice should convert duration ms to frames");
    check(resolved.soundpack->fallback.key_code == keebtype::key::Space, "space should be fallback when present");
  }

  auto overflow = keebtype::resolveSoundpackSlices(*loaded.pack, 1000, 45);
  check(!overflow.ok(), "slice past decoded audio should fail");
}

void testMixerPlaysAndOverlapsWithoutAllocatingInMixPath() {
  keebtype::DecodedAudio audio;
  audio.sample_rate = 1000;
  audio.channels = 2;
  audio.samples.assign(20, 0.5F);

  keebtype::SoundMixer mixer(&audio, 4, 8);
  keebtype::ResolvedSoundSlice slice{keebtype::key::A, 0, 5};

  check(mixer.requestPlay(slice), "valid slice should enqueue");
  std::vector<float> output(4, 0.0F);
  mixer.mix(output.data(), 2);
  check(output[0] > 0.25F && output[1] > 0.25F, "single voice should produce output");

  check(mixer.requestPlay(slice), "first overlap slice should enqueue");
  check(mixer.requestPlay(slice), "second overlap slice should enqueue");
  std::vector<float> overlap(2, 0.0F);
  mixer.mix(overlap.data(), 1);
  check(overlap[0] > output[0], "overlapping voices should mix louder than one voice");

  keebtype::ResolvedSoundSlice invalid{keebtype::key::A, 100, 5};
  check(!mixer.requestPlay(invalid), "out-of-bounds slice should not enqueue");
}

void testMixerVolumeControlsOutputGain() {
  keebtype::DecodedAudio audio;
  audio.sample_rate = 1000;
  audio.channels = 1;
  audio.samples.assign(10, 1.0F);

  keebtype::SoundMixer mixer(&audio, 2, 4);
  keebtype::ResolvedSoundSlice slice{keebtype::key::A, 0, 3};

  mixer.setVolumePercent(25);
  check(mixer.volumePercent() == 25, "volume should store requested percentage");
  check(mixer.requestPlay(slice), "volume test slice should enqueue");
  std::vector<float> quiet(1, 0.0F);
  mixer.mix(quiet.data(), 1);
  check(std::abs(quiet[0] - 0.25F) < 0.001F, "25% volume should scale output");

  mixer.setVolumePercent(200);
  check(mixer.volumePercent() == 100, "volume should clamp above 100%");
  check(mixer.requestPlay(slice), "clamped high volume slice should enqueue");
  std::vector<float> loud(1, 0.0F);
  mixer.mix(loud.data(), 1);
  check(std::abs(loud[0] - 1.0F) < 0.001F, "100% volume should keep full-scale output");

  mixer.setVolumePercent(-10);
  check(mixer.volumePercent() == 0, "volume should clamp below 0%");
}

void testQueueDropsWhenFull() {
  keebtype::DecodedAudio audio;
  audio.sample_rate = 1000;
  audio.channels = 1;
  audio.samples.assign(20, 0.5F);

  keebtype::SoundMixer mixer(&audio, 1, 1);
  keebtype::ResolvedSoundSlice slice{keebtype::key::A, 0, 5};
  check(mixer.requestPlay(slice), "first queue item should fit");
  check(!mixer.requestPlay(slice), "full queue should drop request");
  check(mixer.droppedRequests() == 1, "drop counter should increment");
}

}  // namespace

int main() {
  testLoadsBundledSoundpack();
  testRejectsInvalidJson();
  testRejectsMissingSoundFile();
  testRejectsNegativeSlice();
  testSliceBoundsValidationAndFallback();
  testMixerPlaysAndOverlapsWithoutAllocatingInMixPath();
  testMixerVolumeControlsOutputGain();
  testQueueDropsWhenFull();

  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }

  std::cout << "all keebtype tests passed\n";
  return 0;
}
