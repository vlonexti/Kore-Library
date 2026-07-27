#pragma once

#include <filesystem>

namespace Kore {

/// %LOCALAPPDATA%\KoreLibrary, created on first use. Falls back to the temp
/// directory if the known-folder lookup fails.
std::filesystem::path KoreDataDirectory();

} // namespace Kore
