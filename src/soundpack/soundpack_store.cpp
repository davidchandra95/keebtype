#include "keebtype/soundpack/soundpack_store.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace keebtype {
namespace {

std::string pathString(const std::filesystem::path& path) {
  return path.lexically_normal().string();
}

std::string sanitizePathSegment(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  for (const unsigned char ch : value) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
      result.push_back(static_cast<char>(ch));
    } else {
      result.push_back('_');
    }
  }
  if (result.empty() || result == "." || result == "..") {
    return "soundpack";
  }
  return result;
}

bool copyRegularDirectory(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::string* error) {
  std::error_code ec;
  if (!std::filesystem::exists(source, ec) || !std::filesystem::is_directory(source, ec)) {
    if (error != nullptr) {
      *error = "soundpack import source is not a directory: " + pathString(source);
    }
    return false;
  }

  std::filesystem::remove_all(destination, ec);
  if (ec) {
    if (error != nullptr) {
      *error = "failed to remove temporary import directory: " + ec.message();
    }
    return false;
  }
  std::filesystem::create_directories(destination, ec);
  if (ec) {
    if (error != nullptr) {
      *error = "failed to create temporary import directory: " + ec.message();
    }
    return false;
  }

  std::filesystem::recursive_directory_iterator it(
      source,
      std::filesystem::directory_options::skip_permission_denied,
      ec);
  if (ec) {
    if (error != nullptr) {
      *error = "failed to scan import source: " + pathString(source);
    }
    return false;
  }

  for (const auto& entry : it) {
    const auto status = entry.symlink_status(ec);
    if (ec) {
      if (error != nullptr) {
        *error = "failed to read import entry status: " + pathString(entry.path());
      }
      return false;
    }
    if (std::filesystem::is_symlink(status)) {
      if (error != nullptr) {
        *error = "soundpack import rejects symlinks: " + pathString(entry.path());
      }
      return false;
    }

    const auto relative = std::filesystem::relative(entry.path(), source, ec);
    if (ec) {
      if (error != nullptr) {
        *error = "failed to compute import entry path: " + pathString(entry.path());
      }
      return false;
    }
    const auto target = destination / relative;

    if (std::filesystem::is_directory(status)) {
      std::filesystem::create_directories(target, ec);
      if (ec) {
        if (error != nullptr) {
          *error = "failed to create imported directory: " + pathString(target);
        }
        return false;
      }
      continue;
    }

    if (!std::filesystem::is_regular_file(status)) {
      if (error != nullptr) {
        *error = "soundpack import rejects non-file entry: " + pathString(entry.path());
      }
      return false;
    }

    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
      if (error != nullptr) {
        *error = "failed to create imported file parent: " + pathString(target.parent_path());
      }
      return false;
    }
    std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      if (error != nullptr) {
        *error = "failed to copy imported file: " + pathString(entry.path());
      }
      return false;
    }
  }

  return true;
}

bool samePath(const std::filesystem::path& left, const std::filesystem::path& right) {
  std::error_code ec;
  const auto left_canonical = std::filesystem::weakly_canonical(left, ec);
  if (ec) {
    return left.lexically_normal() == right.lexically_normal();
  }
  const auto right_canonical = std::filesystem::weakly_canonical(right, ec);
  if (ec) {
    return left.lexically_normal() == right.lexically_normal();
  }
  return left_canonical == right_canonical;
}

}  // namespace

SoundpackStore::SoundpackStore(
    std::vector<std::filesystem::path> bundled_roots,
    std::filesystem::path app_data_dir)
    : bundled_roots_(std::move(bundled_roots)),
      app_data_dir_(std::move(app_data_dir)),
      imported_root_(app_data_dir_ / "soundpacks"),
      settings_path_(app_data_dir_ / "settings.json") {}

void SoundpackStore::reload() {
  entries_.clear();
  scan_errors_.clear();
  loadSettings();

  for (const auto& root : bundled_roots_) {
    scanPackRoot(root, SoundpackLocation::Bundled);
  }
  scanPackRoot(imported_root_, SoundpackLocation::Imported);

  std::stable_sort(entries_.begin(), entries_.end(), [](const SoundpackEntry& left, const SoundpackEntry& right) {
    if (left.location != right.location) {
      return left.location == SoundpackLocation::Bundled;
    }
    return left.pack.name < right.pack.name;
  });
}

std::optional<SoundpackEntry> SoundpackStore::findById(const std::string& id) const {
  const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const SoundpackEntry& entry) {
    return entry.pack.id == id;
  });
  if (it == entries_.end()) {
    return std::nullopt;
  }
  return *it;
}

std::optional<SoundpackEntry> SoundpackStore::selectedOrDefault() const {
  if (!selected_soundpack_id_.empty()) {
    if (auto selected = findById(selected_soundpack_id_)) {
      return selected;
    }
  }
  return defaultEntryExcluding("");
}

std::optional<SoundpackEntry> SoundpackStore::defaultEntryExcluding(const std::string& excluded_id) const {
  const auto bundled_default = std::find_if(entries_.begin(), entries_.end(), [&](const SoundpackEntry& entry) {
    return entry.pack.id != excluded_id && entry.location == SoundpackLocation::Bundled && entry.pack.is_default;
  });
  if (bundled_default != entries_.end()) {
    return *bundled_default;
  }

  const auto bundled = std::find_if(entries_.begin(), entries_.end(), [&](const SoundpackEntry& entry) {
    return entry.pack.id != excluded_id && entry.location == SoundpackLocation::Bundled;
  });
  if (bundled != entries_.end()) {
    return *bundled;
  }

  const auto any = std::find_if(entries_.begin(), entries_.end(), [&](const SoundpackEntry& entry) {
    return entry.pack.id != excluded_id;
  });
  if (any != entries_.end()) {
    return *any;
  }

  return std::nullopt;
}

SoundpackStoreResult SoundpackStore::persistSelectedSoundpackId(const std::string& id) {
  SoundpackStoreResult result;
  std::error_code ec;
  std::filesystem::create_directories(app_data_dir_, ec);
  if (ec) {
    result.error = "failed to create settings directory: " + ec.message();
    return result;
  }

  nlohmann::json settings;
  settings["selected_soundpack_id"] = id;

  std::ofstream file(settings_path_, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    result.error = "failed to open settings file for writing: " + pathString(settings_path_);
    return result;
  }
  file << settings.dump(2) << "\n";
  if (!file.good()) {
    result.error = "failed to write settings file: " + pathString(settings_path_);
    return result;
  }

  selected_soundpack_id_ = id;
  result.ok = true;
  return result;
}

SoundpackImportResult SoundpackStore::importSoundpackFolder(const std::filesystem::path& source_dir) {
  SoundpackImportResult result;

  auto loaded = loadSoundpack(source_dir);
  if (!loaded.ok()) {
    result.error = loaded.error;
    return result;
  }

  reload();
  if (findById(loaded.pack->id).has_value()) {
    result.error = "soundpack id already exists: " + loaded.pack->id;
    return result;
  }

  std::error_code ec;
  std::filesystem::create_directories(imported_root_, ec);
  if (ec) {
    result.error = "failed to create imported soundpack directory: " + ec.message();
    return result;
  }

  const auto destination = imported_root_ / sanitizePathSegment(loaded.pack->id);
  if (std::filesystem::exists(destination, ec)) {
    result.error = "import destination already exists: " + pathString(destination);
    return result;
  }

  const auto temporary = imported_root_ / (sanitizePathSegment(loaded.pack->id) + ".tmp");
  std::string copy_error;
  if (!copyRegularDirectory(source_dir, temporary, &copy_error)) {
    std::filesystem::remove_all(temporary, ec);
    result.error = copy_error;
    return result;
  }

  auto copied = loadSoundpack(temporary);
  if (!copied.ok()) {
    std::filesystem::remove_all(temporary, ec);
    result.error = "imported copy failed validation: " + copied.error;
    return result;
  }

  std::filesystem::rename(temporary, destination, ec);
  if (ec) {
    std::filesystem::remove_all(temporary, ec);
    result.error = "failed to finalize imported soundpack: " + ec.message();
    return result;
  }

  reload();
  auto imported = findById(copied.pack->id);
  if (!imported.has_value()) {
    result.error = "imported soundpack is missing after reload: " + copied.pack->id;
    return result;
  }

  result.entry = std::move(imported);
  return result;
}

SoundpackStoreResult SoundpackStore::deleteImportedSoundpack(const std::string& id) {
  SoundpackStoreResult result;
  auto entry = findById(id);
  if (!entry.has_value()) {
    result.error = "soundpack does not exist: " + id;
    return result;
  }
  if (entry->location != SoundpackLocation::Imported) {
    result.error = "only imported soundpacks can be deleted";
    return result;
  }

  if (!samePath(entry->pack.root.parent_path(), imported_root_)) {
    result.error = "refusing to delete soundpack outside imported directory: " + pathString(entry->pack.root);
    return result;
  }

  std::error_code ec;
  std::filesystem::remove_all(entry->pack.root, ec);
  if (ec) {
    result.error = "failed to delete imported soundpack: " + ec.message();
    return result;
  }

  reload();
  result.ok = true;
  return result;
}

void SoundpackStore::loadSettings() {
  selected_soundpack_id_.clear();
  if (!std::filesystem::exists(settings_path_)) {
    return;
  }

  std::ifstream file(settings_path_);
  if (!file.is_open()) {
    scan_errors_.push_back("failed to open settings file: " + pathString(settings_path_));
    return;
  }

  nlohmann::json parsed;
  try {
    file >> parsed;
  } catch (const std::exception& err) {
    scan_errors_.push_back(std::string("invalid settings JSON: ") + err.what());
    return;
  }

  const auto it = parsed.find("selected_soundpack_id");
  if (it != parsed.end() && it->is_string()) {
    selected_soundpack_id_ = it->get<std::string>();
  }
}

void SoundpackStore::scanPackRoot(const std::filesystem::path& root, SoundpackLocation location) {
  std::error_code ec;
  if (!std::filesystem::exists(root, ec)) {
    return;
  }

  if (std::filesystem::exists(root / "config.json", ec)) {
    auto loaded = loadSoundpack(root);
    if (loaded.ok()) {
      addPack(std::move(*loaded.pack), location);
    } else {
      scan_errors_.push_back(loaded.error);
    }
    return;
  }

  if (!std::filesystem::is_directory(root, ec)) {
    return;
  }

  std::filesystem::directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec);
  if (ec) {
    scan_errors_.push_back("failed to scan soundpack directory: " + pathString(root));
    return;
  }

  for (const auto& entry : it) {
    if (!entry.is_directory(ec)) {
      continue;
    }
    const auto pack_root = entry.path();
    if (!std::filesystem::exists(pack_root / "config.json", ec)) {
      continue;
    }
    auto loaded = loadSoundpack(pack_root);
    if (loaded.ok()) {
      addPack(std::move(*loaded.pack), location);
    } else {
      scan_errors_.push_back(loaded.error);
    }
  }
}

void SoundpackStore::addPack(Soundpack pack, SoundpackLocation location) {
  if (findById(pack.id).has_value()) {
    scan_errors_.push_back("duplicate soundpack id skipped: " + pack.id);
    return;
  }
  entries_.push_back(SoundpackEntry{std::move(pack), location});
}

}  // namespace keebtype
