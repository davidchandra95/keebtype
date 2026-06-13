#include "keebtype/platform/paths.hpp"

#include <cstdint>
#include <cstdlib>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace keebtype {
namespace {

std::filesystem::path absoluteIfPossible(const std::filesystem::path& path) {
  std::error_code error;
  auto absolute = std::filesystem::absolute(path, error);
  if (error) {
    return path;
  }
  return absolute.lexically_normal();
}

void addCandidate(std::vector<std::filesystem::path>* candidates, const std::filesystem::path& root) {
  if (root.empty()) {
    return;
  }
  candidates->push_back((root / "soundpacks" / "cherrymx-brown-abs").lexically_normal());
}

void addSoundpackRootCandidate(
    std::vector<std::filesystem::path>* candidates,
    const std::filesystem::path& root) {
  if (root.empty()) {
    return;
  }
  candidates->push_back((root / "soundpacks").lexically_normal());
}

void addMacBundleCandidates(std::vector<std::filesystem::path>* candidates, const std::filesystem::path& exe_dir) {
#if defined(__APPLE__)
  const auto contents_dir = exe_dir.parent_path();
  if (contents_dir.filename() == "Contents") {
    addCandidate(candidates, contents_dir / "Resources");
  }
#else
  (void)candidates;
  (void)exe_dir;
#endif
}

void addMacBundleRootCandidates(
    std::vector<std::filesystem::path>* candidates,
    const std::filesystem::path& exe_dir) {
#if defined(__APPLE__)
  const auto contents_dir = exe_dir.parent_path();
  if (contents_dir.filename() == "Contents") {
    addSoundpackRootCandidate(candidates, contents_dir / "Resources");
  }
#else
  (void)candidates;
  (void)exe_dir;
#endif
}

std::filesystem::path envPath(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return {};
  }
  return std::filesystem::path(value);
}

}  // namespace

std::filesystem::path executablePath(const char* argv0) {
#if defined(_WIN32)
  std::wstring buffer(MAX_PATH, L'\0');
  DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (size > 0 && size < buffer.size()) {
    buffer.resize(size);
    return std::filesystem::path(buffer).lexically_normal();
  }
#elif defined(__APPLE__)
  std::vector<char> buffer(PATH_MAX);
  std::uint32_t size = static_cast<std::uint32_t>(buffer.size());
  if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
    return absoluteIfPossible(buffer.data());
  }
#else
  std::vector<char> buffer(PATH_MAX);
  const auto size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (size > 0) {
    buffer[static_cast<std::size_t>(size)] = '\0';
    return absoluteIfPossible(buffer.data());
  }
#endif

  if (argv0 != nullptr && argv0[0] != '\0') {
    return absoluteIfPossible(argv0);
  }
  return {};
}

std::filesystem::path userDataDir() {
#if defined(_WIN32)
  const auto app_data = envPath("APPDATA");
  if (!app_data.empty()) {
    return (app_data / "Keebtype").lexically_normal();
  }
  const auto user_profile = envPath("USERPROFILE");
  if (!user_profile.empty()) {
    return (user_profile / "AppData" / "Roaming" / "Keebtype").lexically_normal();
  }
#elif defined(__APPLE__)
  const auto home = envPath("HOME");
  if (!home.empty()) {
    return (home / "Library" / "Application Support" / "Keebtype").lexically_normal();
  }
#else
  const auto xdg_config_home = envPath("XDG_CONFIG_HOME");
  if (!xdg_config_home.empty()) {
    return (xdg_config_home / "Keebtype").lexically_normal();
  }
  const auto home = envPath("HOME");
  if (!home.empty()) {
    return (home / ".config" / "Keebtype").lexically_normal();
  }
#endif

  return (std::filesystem::current_path() / ".keebtype").lexically_normal();
}

std::vector<std::filesystem::path> candidateBundledSoundpackRoots(const char* argv0) {
  std::vector<std::filesystem::path> candidates;
  addSoundpackRootCandidate(&candidates, std::filesystem::current_path());

  const auto exe = executablePath(argv0);
  if (!exe.empty()) {
    auto dir = exe.parent_path();
    addMacBundleRootCandidates(&candidates, dir);
    for (int depth = 0; depth < 4 && !dir.empty(); ++depth) {
      addSoundpackRootCandidate(&candidates, dir);
      dir = dir.parent_path();
    }
  }

  return candidates;
}

std::vector<std::filesystem::path> candidateSoundpackRoots(const char* argv0) {
  std::vector<std::filesystem::path> candidates;
  addCandidate(&candidates, std::filesystem::current_path());

  const auto exe = executablePath(argv0);
  if (!exe.empty()) {
    auto dir = exe.parent_path();
    addMacBundleCandidates(&candidates, dir);
    for (int depth = 0; depth < 4 && !dir.empty(); ++depth) {
      addCandidate(&candidates, dir);
      dir = dir.parent_path();
    }
  }

  return candidates;
}

}  // namespace keebtype
