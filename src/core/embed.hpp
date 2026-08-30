#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "config.hpp"
#include "db.hpp"
#include "diarize.hpp"
#include "types.hpp"

namespace scribble {

namespace fs = std::filesystem;

/// Produces the voiceprints that corpus-wide identity is built on.
///
/// The diarizer computes embeddings internally but does not hand them back, and
/// its clustering is per file, so this extracts its own vectors from the turns
/// that carry the most usable speech.
class Embedder {
public:
    ~Embedder();

    Embedder(const Embedder &) = delete;
    Embedder &operator=(const Embedder &) = delete;

    static std::unique_ptr<Embedder> create(const Config &cfg, const fs::path &model_file,
                                            std::string *error);

    int dim() const;

    /// One voiceprint per speaker found in `turns`. `file_id` is stamped onto
    /// each result so the cannot-link constraint has what it needs later.
    bool build_voiceprints(const std::vector<float> &samples,
                           const std::vector<DiarizedTurn> &turns, std::int64_t file_id,
                           std::vector<LocalSpeaker> *out, std::string *error);

    /// Embeds one span directly. Used when re-checking a single speaker.
    bool embed_span(const std::vector<float> &samples, double start, double end,
                    std::vector<float> *out, std::string *error);

private:
    Embedder();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace scribble
