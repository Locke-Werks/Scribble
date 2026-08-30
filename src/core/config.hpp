#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace scribble {

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
    //
    // Empty means "not configured", and resolve() fills each one with a
    // per-user location. They are not defaulted to relative paths, because the
    // working directory of an installed application is its install directory,
    // and under Program Files that is read-only: the database would fail to
    // open on the first launch after installing.
    fs::path out_dir;   ///< defaults to Documents\Scribble
    fs::path work_dir;  ///< defaults to the local app data directory
    fs::path db_path;   ///< defaults to the local app data directory
    fs::path model_dir;  ///< empty resolves to %LOCALAPPDATA%\Scribble\models
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
    /// Runs voice activity detection before Whisper decodes anything. Without
    /// it a recording that is mostly ambience produces confident, fluent,
    /// entirely invented speech repeated for the length of the file, because
    /// Whisper is a language model and silence is out of its distribution.
    bool vad_filter = true;
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
    /// Off by default. This is for recordings where each channel is a separate
    /// microphone on a separate person, which is rare, and splitting an
    /// ordinary stereo mix instead doubles the work and produces two half
    /// transcripts of the same conversation.
    bool split_channels = false;
    int max_split_channels = 8;
    /// How unlike each other two channels must be before they are treated as
    /// separate microphones. Ordinary stereo content sits high here because
    /// both channels carry the same sources at different levels. Genuinely
    /// independent microphones sit low, since each one is dominated by whoever
    /// is closest to it.
    float channel_independence = 0.35f;

    // -- output -------------------------------------------------------------
    std::vector<std::string> formats{"srt", "md", "json"};
    bool overwrite = false;

    // -- runtime ------------------------------------------------------------
    bool recursive = true;
    int decode_lookahead = 1;  ///< files to decode ahead of the GPU stage

    // -- loading ------------------------------------------------------------
    /// Reads scribble.toml if present. Unknown keys are an error, not a warning:
    /// a silently ignored setting is worse than a failed start.
    static Config load(const fs::path &file, std::string *error);
    void save(const fs::path &file) const;

    /// Turns relative paths absolute against `root` and fills model_dir.
    void resolve(const fs::path &root);

    std::string validate() const;  ///< empty when the config is usable
};

fs::path default_model_dir();
fs::path default_config_path();

}  // namespace scribble
