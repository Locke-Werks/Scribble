#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "config.hpp"

namespace scribe {

namespace fs = std::filesystem;

enum class ModelKind { Whisper, Segmentation, Embedding };

struct ModelSpec {
    std::string name;
    ModelKind kind = ModelKind::Whisper;
    std::string url;
    std::string filename;   ///< final name on disk
    bool archive = false;   ///< url points at a tar.bz2 rather than the file
    std::string member;     ///< path inside the archive when `archive` is set
    std::string description;
    std::int64_t approx_bytes = 0;
};

/// Everything the app knows how to fetch. Anything not listed can still be used
/// by passing an absolute path as the model name.
const std::vector<ModelSpec> &model_catalogue();

const ModelSpec *find_model(const std::string &name, ModelKind kind);

/// Reports download progress. `total` is 0 when the server sends no length.
using DownloadProgress =
    std::function<void(const std::string &name, std::int64_t done, std::int64_t total)>;

/// Returns the local path for a model, downloading it on first use.
///
/// An absolute path or an existing file in `model_dir` is taken as is, so a
/// hand-placed model, including a segmentation model exported outside this
/// project, can be dropped in without touching the catalogue.
bool resolve_model(const std::string &name, ModelKind kind, const fs::path &model_dir,
                   const DownloadProgress &progress, fs::path *out, std::string *error);

}  // namespace scribe
