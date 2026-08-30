#include "media.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

#include "proc.hpp"
#include "util.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace scribe {
namespace {

const std::set<std::string> &media_extensions() {
    static const std::set<std::string> exts = {
        // audio
        ".wav", ".flac", ".mp3", ".m4a", ".aac", ".ogg", ".opus", ".wma", ".aiff",
        ".aif", ".alac", ".ape", ".mka", ".m4b", ".amr", ".caf", ".wv", ".dts",
        // video
        ".mp4", ".mkv", ".mov", ".avi", ".webm", ".m4v", ".wmv", ".flv", ".mpg",
        ".mpeg", ".ts", ".m2ts", ".mts", ".vob", ".3gp", ".ogv", ".asf", ".rm",
    };
    return exts;
}

std::string first_line_of(const std::string &text) {
    size_t pos = text.find('\n');
    return trim(pos == std::string::npos ? text : text.substr(0, pos));
}

}  // namespace


bool MediaInfo::looks_multitrack() const {
    if (channels < 2) {
        return false;
    }
    if (!layout.empty() && layout != "stereo" && layout != "unknown") {
        return false;
    }
    return true;
}

bool is_media_file(const fs::path &path) {
    return media_extensions().count(to_lower(path.extension().string())) > 0;
}

bool probe_media(const fs::path &ffprobe, const fs::path &input, MediaInfo *info,
                 std::string *error) {
    std::vector<std::string> args = {
        "-v", "error",
        "-select_streams", "a:0",
        "-show_entries", "stream=codec_name,sample_rate,channels,channel_layout,duration",
        "-show_entries", "format=duration",
        "-of", "default=noprint_wrappers=1:nokey=0",
        input.string(),
    };

    std::string out;
    int code = 1;
    if (!run_process(ffprobe, args, &out, &code, error)) {
        return false;
    }
    if (code != 0) {
        if (error) {
            *error = "ffprobe failed: " + first_line_of(out);
        }
        return false;
    }

    double stream_duration = 0.0;
    double format_duration = 0.0;
    bool seen_duration = false;

    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        line = trim(line);
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (value == "N/A") {
            continue;
        }

        if (key == "codec_name") {
            info->codec = value;
            info->has_audio = true;
        } else if (key == "sample_rate") {
            info->sample_rate = std::atoi(value.c_str());
        } else if (key == "channels") {
            info->channels = std::max(1, std::atoi(value.c_str()));
        } else if (key == "channel_layout") {
            info->layout = value;
        } else if (key == "duration") {
            // The stream entry prints before the format entry, so the first
            // duration seen is the stream's. Container duration is the
            // fallback because many streams do not carry one.
            double parsed = std::atof(value.c_str());
            if (!seen_duration) {
                stream_duration = parsed;
                seen_duration = true;
            } else {
                format_duration = parsed;
            }
        }
    }

    info->duration = stream_duration > 0.0 ? stream_duration : format_duration;

    if (!info->has_audio) {
        if (error) {
            *error = "no audio stream";
        }
        return false;
    }
    return true;
}

bool decode_to_wav(const fs::path &ffmpeg, const fs::path &input, const fs::path &output,
                   int channel, std::string *error) {
    return decode_audio(ffmpeg, input, output, channel, kSampleRate, 1, error);
}

bool decode_audio(const fs::path &ffmpeg, const fs::path &input, const fs::path &output,
                  int channel, int sample_rate, int channels, std::string *error) {
    std::error_code ec;
    fs::create_directories(output.parent_path(), ec);

    // Plain aresample, not soxr. Many ffmpeg builds ship without the soxr
    // resampler and fail outright when it is requested, and the quality
    // difference resampling speech for ASR is not measurable.
    std::string filter = "aresample=" + std::to_string(sample_rate);
    if (channel >= 0) {
        filter = "pan=mono|c0=c" + std::to_string(channel) + "," + filter;
    }

    fs::path tmp = output;
    tmp += ".part.wav";

    std::vector<std::string> args = {
        "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
        "-i", input.string(),
        "-vn", "-sn", "-dn",
        "-map", "0:a:0",
        "-af", filter,
        "-ac", std::to_string(channels),
        "-ar", std::to_string(sample_rate),
        "-c:a", "pcm_s16le",
        "-f", "wav",
        tmp.string(),
    };

    std::string out;
    int code = 1;
    if (!run_process(ffmpeg, args, &out, &code, error)) {
        fs::remove(tmp, ec);
        return false;
    }
    if (code != 0) {
        fs::remove(tmp, ec);
        if (error) {
            *error = "ffmpeg failed: " + first_line_of(out);
        }
        return false;
    }

    fs::remove(output, ec);
    fs::rename(tmp, output, ec);
    if (ec) {
        if (error) {
            *error = "cannot move decoded audio into place: " + ec.message();
        }
        return false;
    }
    return true;
}

double channel_correlation(const fs::path &ffmpeg, const fs::path &input, double seconds) {
    // Downsampled and time-limited: this only decides whether two tracks are
    // the same signal, which does not need full bandwidth or the whole file.
    std::vector<std::string> args = {
        "-nostdin", "-hide_banner", "-loglevel", "error",
        "-t", std::to_string(seconds),
        "-i", input.string(),
        "-vn", "-map", "0:a:0",
        "-ar", "8000",
        "-c:a", "pcm_s16le",
        "-f", "s16le",
        "pipe:1",
    };

    std::string raw;
    int code = 1;
    if (!run_process(ffmpeg, args, &raw, &code, nullptr) || code != 0 || raw.size() < 400) {
        return 0.0;
    }

    const auto *pcm = reinterpret_cast<const std::int16_t *>(raw.data());
    size_t frames = raw.size() / (sizeof(std::int16_t) * 2);
    if (frames < 100) {
        return 0.0;
    }

    double sum_a = 0, sum_b = 0;
    for (size_t i = 0; i < frames; ++i) {
        sum_a += pcm[i * 2];
        sum_b += pcm[i * 2 + 1];
    }
    double mean_a = sum_a / static_cast<double>(frames);
    double mean_b = sum_b / static_cast<double>(frames);

    double cov = 0, var_a = 0, var_b = 0;
    for (size_t i = 0; i < frames; ++i) {
        double da = pcm[i * 2] - mean_a;
        double db = pcm[i * 2 + 1] - mean_b;
        cov += da * db;
        var_a += da * da;
        var_b += db * db;
    }
    if (var_a < 1e-6 || var_b < 1e-6) {
        // One side is silent. Treating that as identical avoids splitting a
        // file into a real track and an empty one.
        return 1.0;
    }
    return std::abs(cov / std::sqrt(var_a * var_b));
}

std::vector<fs::path> discover_media(const std::vector<fs::path> &paths, bool recursive) {
    std::set<fs::path> found;
    std::error_code ec;

    for (const auto &p : paths) {
        if (fs::is_regular_file(p, ec)) {
            if (is_media_file(p)) {
                found.insert(fs::absolute(p, ec).lexically_normal());
            }
        } else if (fs::is_directory(p, ec)) {
            if (recursive) {
                for (auto it = fs::recursive_directory_iterator(
                         p, fs::directory_options::skip_permission_denied, ec);
                     it != fs::recursive_directory_iterator(); it.increment(ec)) {
                    if (ec) {
                        ec.clear();
                        continue;
                    }
                    if (it->is_regular_file(ec) && is_media_file(it->path())) {
                        found.insert(fs::absolute(it->path(), ec).lexically_normal());
                    }
                }
            } else {
                for (auto it = fs::directory_iterator(
                         p, fs::directory_options::skip_permission_denied, ec);
                     it != fs::directory_iterator(); it.increment(ec)) {
                    if (ec) {
                        ec.clear();
                        continue;
                    }
                    if (it->is_regular_file(ec) && is_media_file(it->path())) {
                        found.insert(fs::absolute(it->path(), ec).lexically_normal());
                    }
                }
            }
        }
    }

    return {found.begin(), found.end()};
}

}  // namespace scribe
