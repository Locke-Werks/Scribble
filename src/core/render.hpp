#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "types.hpp"

namespace scribe {

namespace fs = std::filesystem;

/// Everything a renderer needs beyond the segments themselves.
struct RenderContext {
    std::string source_path;
    std::string source_name;
    int track = 0;
    int track_count = 1;
    double duration = 0.0;
    std::string language;
    std::string model;

    /// Resolves a global speaker id to its display name. Names are looked up at
    /// render time rather than baked into the segments, so renaming a speaker
    /// and re-rendering picks the new name up without touching the transcript.
    std::function<std::string(std::int64_t, const std::string &)> speaker_name;

    std::string label_for(const Segment &seg) const;
};

bool is_supported_format(const std::string &format);

std::string render(const std::string &format, const std::vector<Segment> &segments,
                   const RenderContext &ctx);

/// Writes one transcript. Returns false and fills `error` on failure.
bool write_transcript(const std::string &format, const std::vector<Segment> &segments,
                      const RenderContext &ctx, const fs::path &path, std::string *error);

/// Output path for one file and format, mirroring the input tree under
/// `out_dir` when `mirror_tree` is set.
fs::path output_path(const fs::path &out_dir, const fs::path &source, int track,
                     int track_count, const std::string &format, bool mirror_tree,
                     const fs::path &mirror_root);

/// Longest common parent of every input, used as the root when mirroring.
fs::path common_root(const std::vector<fs::path> &paths);

}  // namespace scribe
