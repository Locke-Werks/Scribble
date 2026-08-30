#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace scribe {

namespace fs = std::filesystem;

/// When to run the source-separation front end. Isolation is the largest
/// single accuracy win on broadcast, field and music-bedded audio, and a small
/// loss on already-clean speech, so it is gated rather than always on.
enum class IsolateMode { Never, Auto, Always };

enum class AsrBackend { Whisper, Parakeet };

/// Where inference runs. Whisper and ONNX are configured separately because
/// the diarization models are small enough that CPU is often the right call
/// while the GPU stays saturated with Whisper.
enum class Accel { Auto, Cpu, Cuda };

/// What to do when GPU acceleration for the onnxruntime stages is wanted but
/// cuDNN and its companions are not installed.
enum class GpuRuntimeMode {
    Prompt,  ///< ask before downloading, and run on CPU if declined
    Auto,    ///< download without asking
    Never,   ///< stay on CPU and say so once
};

struct Config {
    // -- paths ------------------------------------------------------------
    fs::path out_dir{"out"};
    fs::path work_dir{"work"};
    fs::path db_path{"scribe.db"};
    fs::path model_dir;  ///< empty resolves to %LOCALAPPDATA%\ScribeEveryone\models
    bool mirror_tree = true;   ///< reproduce input folder structure under out_dir
    bool keep_work = false;    ///< keep decoded wavs after a file completes

    // -- speech recognition -----------------------------------------------
    AsrBackend backend = AsrBackend::Whisper;
    std::string model = "large-v3";
    Accel asr_accel = Accel::Auto;
    std::string language;        ///< empty auto-detects
    bool translate = false;
    int beam_size = 5;
    int n_threads = 0;           ///< 0 picks hardware_concurrency
    /// Whisper's repetition failure mode lives here. Off is the right default
    /// across a large heterogeneous corpus: slightly less context carried
    /// between windows, no runaway loops on silence and music.
    bool condition_on_previous_text = false;
    bool temperature_fallback = true;
    float entropy_threshold = 2.4f;
    float logprob_threshold = -1.0f;
    float no_speech_threshold = 0.6f;
    bool word_timestamps = true;
    bool suppress_non_speech = true;
    /// Proper nouns, jargon and product names. The cheapest large accuracy
    /// gain available on exactly the words that matter.
    std::vector<std::string> hotwords;
    std::string initial_prompt;

    // -- source separation --------------------------------------------------
    IsolateMode isolate = IsolateMode::Auto;
    std::string isolate_model = "uvr-mdxnet";
    /// Noise-floor ratio above which Auto decides a file needs isolating.
    float isolate_auto_threshold = 0.28f;

    // -- diarization --------------------------------------------------------
    bool diarize = true;
    /// Auto, and genuinely automatic: see onnx_small_model_provider in
    /// paths.hpp. Segmentation and voiceprint models are small and run over
    /// many short windows, so whether the GPU wins depends on how much CPU the
    /// machine has, not on whether a GPU exists.
    Accel onnx_accel = Accel::Auto;
    /// Isolation is one large model over the whole recording, where the GPU
    /// wins on any machine, so Auto here means the GPU whenever it is usable.
    Accel isolate_accel = Accel::Auto;
    /// Diarization, voiceprints, isolation and Parakeet reach the GPU through
    /// onnxruntime, which needs cuDNN. NVIDIA's licence does not allow shipping
    /// it, so it is fetched on request. Whisper is unaffected either way.
    GpuRuntimeMode gpu_runtime = GpuRuntimeMode::Prompt;
    std::string segmentation_model = "pyannote-segmentation-3.0";
    std::string embedding_model = "wespeaker-resnet293";
    int num_speakers = -1;   ///< -1 lets clustering decide
    int min_speakers = -1;
    int max_speakers = -1;
    float diar_cluster_threshold = 0.5f;

    // -- corpus-wide speaker identity ---------------------------------------
    /// Shortest turn allowed to contribute to a speaker's voiceprint. Brief
    /// interjections carry too little signal and poison the centroid.
    double embed_min_segment = 3.0;
    int embed_max_segments = 8;
    /// Cosine above which a file-local speaker joins an existing global one.
    float match_threshold = 0.65f;
    /// Pairs landing between review and match get reported for a human to
    /// merge rather than silently split or silently joined.
    float review_threshold = 0.50f;
    /// Distance threshold for the offline reclustering pass.
    float cluster_threshold = 0.65f;

    // -- multi-track --------------------------------------------------------
    bool split_channels = true;
    int max_split_channels = 8;
    /// Above this correlation the two channels are one mono source duplicated,
    /// so splitting would just double the work for nothing.
    float channel_dup_correlation = 0.98f;

    // -- output -------------------------------------------------------------
    std::vector<std::string> formats{"srt", "md", "json"};
    bool overwrite = false;

    // -- runtime ------------------------------------------------------------
    bool recursive = true;
    int decode_lookahead = 1;  ///< files to decode ahead of the GPU stage

    // -- loading ------------------------------------------------------------
    /// Reads scribe.toml if present. Unknown keys are an error, not a warning:
    /// a silently ignored setting is worse than a failed start.
    static Config load(const fs::path &file, std::string *error);
    void save(const fs::path &file) const;

    /// Turns relative paths absolute against `root` and fills model_dir.
    void resolve(const fs::path &root);

    std::string validate() const;  ///< empty when the config is usable
};

fs::path default_model_dir();
fs::path default_config_path();

}  // namespace scribe
