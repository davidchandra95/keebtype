#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "keebtype/soundpack/soundpack.hpp"

namespace keebtype {

enum class SoundpackLocation {
  Bundled,
  Imported,
  External,
};

struct SoundpackEntry {
  Soundpack pack;
  SoundpackLocation location = SoundpackLocation::Bundled;
};

struct SoundpackStoreResult {
  bool ok = false;
  std::string error;
};

struct SoundpackImportResult {
  std::optional<SoundpackEntry> entry;
  std::string error;

  [[nodiscard]] bool ok() const noexcept { return entry.has_value(); }
};

class SoundpackStore {
 public:
  SoundpackStore(
      std::vector<std::filesystem::path> bundled_roots,
      std::filesystem::path app_data_dir);

  void reload();

  [[nodiscard]] const std::vector<SoundpackEntry>& entries() const noexcept { return entries_; }
  [[nodiscard]] const std::vector<std::string>& scanErrors() const noexcept { return scan_errors_; }
  [[nodiscard]] const std::filesystem::path& importedRoot() const noexcept { return imported_root_; }
  [[nodiscard]] const std::filesystem::path& settingsPath() const noexcept { return settings_path_; }
  [[nodiscard]] const std::string& selectedSoundpackId() const noexcept {
    return selected_soundpack_id_;
  }

  [[nodiscard]] std::optional<SoundpackEntry> findById(const std::string& id) const;
  [[nodiscard]] std::optional<SoundpackEntry> selectedOrDefault() const;
  [[nodiscard]] std::optional<SoundpackEntry> defaultEntryExcluding(const std::string& excluded_id) const;

  SoundpackStoreResult persistSelectedSoundpackId(const std::string& id);
  SoundpackImportResult importSoundpackFolder(const std::filesystem::path& source_dir);
  SoundpackStoreResult deleteImportedSoundpack(const std::string& id);

 private:
  void loadSettings();
  void scanPackRoot(const std::filesystem::path& root, SoundpackLocation location);
  void addPack(Soundpack pack, SoundpackLocation location);

  std::vector<std::filesystem::path> bundled_roots_;
  std::filesystem::path app_data_dir_;
  std::filesystem::path imported_root_;
  std::filesystem::path settings_path_;
  std::vector<SoundpackEntry> entries_;
  std::vector<std::string> scan_errors_;
  std::string selected_soundpack_id_;
};

}  // namespace keebtype
