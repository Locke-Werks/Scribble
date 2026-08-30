#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace scribe {

namespace fs = std::filesystem;

/// Reads a 16 kHz mono 16-bit PCM WAV, the only shape this pipeline produces,
/// into normalised floats. Anything else is rejected rather than silently
/// resampled, because a wrong sample rate reaching Whisper produces plausible
/// nonsense instead of an error.
bool read_wav_mono16k(const fs::path &path, std::vector<float> *samples, std::string *error);

bool write_wav_mono16k(const fs::path &path, const std::vector<float> &samples,
                       std::string *error);

/// Noise floor relative to typical speech energy. A quiet floor under loud
/// speech scores low. Used to decide whether a file is worth isolating.
float estimate_noise_floor(const std::vector<float> &samples);

}  // namespace scribe
