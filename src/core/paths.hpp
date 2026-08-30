#pragma once

#include <filesystem>

namespace scribe {

namespace fs = std::filesystem;

/// %LOCALAPPDATA%\ScribeEveryone on Windows, ~/.local/share/ScribeEveryone
/// elsewhere. Models live here rather than beside the exe so an install under
/// Program Files stays read-only and a reinstall does not discard several
/// gigabytes of downloaded weights.
fs::path app_data_dir();

fs::path default_model_dir();
fs::path default_config_path();

/// Locates ffmpeg and ffprobe. Prefers the copies shipped beside the exe, then
/// falls back to PATH. Returns an empty path when neither is present.
fs::path find_ffmpeg();
fs::path find_ffprobe();

/// Directory holding the running executable.
fs::path executable_dir();

}  // namespace scribe
