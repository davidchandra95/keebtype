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
