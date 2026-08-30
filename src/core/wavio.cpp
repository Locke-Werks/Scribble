#include "wavio.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

#include "media.hpp"

namespace scribe {
namespace {

#pragma pack(push, 1)
struct WavHeader {
    char riff[4];
    std::uint32_t riff_size;
    char wave[4];
    char fmt[4];
    std::uint32_t fmt_size;
    std::uint16_t format;
    std::uint16_t channels;
    std::uint32_t sample_rate;
    std::uint32_t byte_rate;
    std::uint16_t block_align;
    std::uint16_t bits;
    char data[4];
    std::uint32_t data_size;
};
#pragma pack(pop)

bool read_u32(std::ifstream &in, std::uint32_t *value) {
    in.read(reinterpret_cast<char *>(value), 4);
    return static_cast<bool>(in);
}

}  // namespace

bool read_wav_mono16k(const fs::path &path, std::vector<float> *samples, std::string *error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) {
            *error = "cannot open " + path.string();
        }
        return false;
    }

    char riff[4] = {};
    std::uint32_t riff_size = 0;
    char wave[4] = {};
    in.read(riff, 4);
    read_u32(in, &riff_size);
    in.read(wave, 4);
    if (!in || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0) {
        if (error) {
            *error = path.string() + " is not a RIFF/WAVE file";
        }
        return false;
    }

    std::uint16_t format = 0;
    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t bits = 0;
    bool have_fmt = false;

    // Chunks are walked rather than assumed at fixed offsets: ffmpeg emits a
    // LIST chunk between fmt and data, so a struct-shaped read lands in the
    // wrong place and produces silence.
    for (;;) {
        char id[4] = {};
        std::uint32_t size = 0;
        in.read(id, 4);
        if (!in || !read_u32(in, &size)) {
            break;
        }

        if (std::memcmp(id, "fmt ", 4) == 0) {
            std::vector<char> fmt(size);
            in.read(fmt.data(), size);
            if (!in || size < 16) {
                break;
            }
            std::memcpy(&format, fmt.data() + 0, 2);
            std::memcpy(&channels, fmt.data() + 2, 2);
            std::memcpy(&sample_rate, fmt.data() + 4, 4);
            std::memcpy(&bits, fmt.data() + 14, 2);
            have_fmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            if (!have_fmt) {
                if (error) {
                    *error = "data chunk before fmt chunk in " + path.string();
                }
                return false;
            }
            if (channels != 1 || sample_rate != static_cast<std::uint32_t>(kSampleRate) ||
                bits != 16 || format != 1) {
                if (error) {
                    *error = "expected 16 kHz mono 16-bit PCM, got " +
                             std::to_string(sample_rate) + " Hz, " + std::to_string(channels) +
                             " channels, " + std::to_string(bits) + " bit";
                }
                return false;
            }

            size_t count = size / sizeof(std::int16_t);
            std::vector<std::int16_t> pcm(count);
            in.read(reinterpret_cast<char *>(pcm.data()),
                    static_cast<std::streamsize>(count * sizeof(std::int16_t)));
            auto got = static_cast<size_t>(in.gcount()) / sizeof(std::int16_t);
            pcm.resize(got);

            samples->resize(got);
            for (size_t i = 0; i < got; ++i) {
                (*samples)[i] = static_cast<float>(pcm[i]) / 32768.0f;
            }
            return true;
        } else {
            in.seekg(size + (size & 1), std::ios::cur);
            if (!in) {
                break;
            }
        }
    }

    if (error) {
        *error = "no data chunk in " + path.string();
    }
    return false;
}

bool write_wav_mono16k(const fs::path &path, const std::vector<float> &samples,
                       std::string *error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) {
            *error = "cannot write " + path.string();
        }
        return false;
    }

    const auto data_bytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));

    WavHeader h{};
    std::memcpy(h.riff, "RIFF", 4);
    h.riff_size = 36 + data_bytes;
    std::memcpy(h.wave, "WAVE", 4);
    std::memcpy(h.fmt, "fmt ", 4);
    h.fmt_size = 16;
    h.format = 1;
    h.channels = 1;
    h.sample_rate = kSampleRate;
    h.bits = 16;
    h.block_align = 2;
    h.byte_rate = kSampleRate * 2;
    std::memcpy(h.data, "data", 4);
    h.data_size = data_bytes;

    out.write(reinterpret_cast<const char *>(&h), sizeof(h));

    std::vector<std::int16_t> pcm(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        float v = std::clamp(samples[i], -1.0f, 1.0f);
        pcm[i] = static_cast<std::int16_t>(std::lround(v * 32767.0f));
    }
    out.write(reinterpret_cast<const char *>(pcm.data()),
              static_cast<std::streamsize>(pcm.size() * sizeof(std::int16_t)));

    if (!out) {
        if (error) {
            *error = "write failed for " + path.string();
        }
        return false;
    }
    return true;
}

float estimate_noise_floor(const std::vector<float> &samples) {
    constexpr int kFrame = kSampleRate / 50;
    if (samples.size() < static_cast<size_t>(kFrame) * 20) {
        return 0.0f;
    }

    size_t frames = samples.size() / kFrame;
    std::vector<float> energy(frames);
    for (size_t f = 0; f < frames; ++f) {
        double sum = 0.0;
        for (int i = 0; i < kFrame; ++i) {
            sum += std::abs(samples[f * kFrame + static_cast<size_t>(i)]);
        }
        energy[f] = static_cast<float>(sum / kFrame);
    }

    std::vector<float> sorted = energy;
    std::sort(sorted.begin(), sorted.end());
    float median = sorted[sorted.size() / 2];
    float floor = sorted[sorted.size() / 10];

    if (median <= 1e-6f) {
        return 1.0f;
    }
    return std::min(1.0f, floor / median);
}

}  // namespace scribe
