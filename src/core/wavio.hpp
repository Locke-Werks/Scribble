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

/// Planar audio at whatever rate and channel count the file carries.
struct WavData {
    int channels = 1;
    int sample_rate = 0;
    std::vector<std::vector<float>> planar;  ///< planar[c] is channel c

    std::size_t frames() const { return planar.empty() ? 0 : planar.front().size(); }
};

/// Reads any 16-bit PCM or 32-bit float WAV. Needed by source separation,
/// which runs at the model's own rate on stereo input, before the pipeline
/// collapses everything to 16 kHz mono.
bool read_wav(const fs::path &path, WavData *out, std::string *error);

bool write_wav(const fs::path &path, const WavData &data, std::string *error);

/// Noise floor relative to typical speech energy. A quiet floor under loud
/// speech scores low. Used to decide whether a file is worth isolating.
float estimate_noise_floor(const std::vector<float> &samples);

}  // namespace scribe
