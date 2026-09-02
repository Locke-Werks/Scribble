#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace scribble {

namespace fs = std::filesystem;

constexpr int kSampleRate = 16000;

bool is_media_file(const fs::path &path);

struct MediaInfo {
    double duration = 0.0;
    int channels = 1;
    int sample_rate = 0;
    std::string codec;
    std::string layout;
    bool has_audio = false;

    /// True when the channels look like separate microphones rather than a
    /// spatial mix. A 5.1 or quad layout is one performance captured from
    /// several angles, so it gets downmixed like anything else.
    bool looks_multitrack() const;
};

/// Runs ffprobe. Returns false and fills `error` when the file has no usable
/// audio stream.
bool probe_media(const fs::path &ffprobe, const fs::path &input, MediaInfo *info,
                 std::string *error);

/// Decodes to 16 kHz mono PCM, the input every model in the pipeline expects.
/// `channel` selects one track out of a multi-track file instead of downmixing.
/// Returns false and fills `error` on failure.
bool decode_to_wav(const fs::path &ffmpeg, const fs::path &input, const fs::path &output,
                   int channel, std::string *error);

/// Decodes a time range to 16 kHz mono. `end` at or below `start` means "to the
/// end". The seek is an input option and the length an output option, which is
/// the one spelling of a range that means the same thing across ffmpeg
/// versions, and it also keeps a clip near the end of a long recording from
/// decoding everything before it.
bool decode_range_to_wav(const fs::path &ffmpeg, const fs::path &input, const fs::path &output,
                         double start, double end, std::string *error);

/// Decodes at an arbitrary rate and channel count. Source separation runs at
/// the model's own rate on stereo, so it needs the audio before the pipeline
/// collapses it to 16 kHz mono.
bool decode_audio(const fs::path &ffmpeg, const fs::path &input, const fs::path &output,
                  int channel, int sample_rate, int channels, std::string *error);

/// Correlation between the two channels of a stereo file. Near 1.0 means it is
/// one mono source duplicated, so splitting would double the work for nothing.
double channel_correlation(const fs::path &ffmpeg, const fs::path &input, double seconds);

/// Walks the given paths and returns every media file found, sorted and
/// deduplicated.
std::vector<fs::path> discover_media(const std::vector<fs::path> &paths, bool recursive);

}  // namespace scribble
