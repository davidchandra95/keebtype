#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace keebtype {

std::filesystem::path executablePath(const char* argv0);
std::vector<std::filesystem::path> candidateSoundpackRoots(const char* argv0);

}  // namespace keebtype

