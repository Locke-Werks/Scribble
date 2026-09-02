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
    /// Least speech, in clock time across a whole file, before a voice is
    /// allowed to become a corpus identity of its own.
    ///
    /// Diarization has no notion of whether a cluster is worth having. It will
    /// hand back a speaker for a two-word interjection off-mic as readily as
    /// for the person running the meeting, and since two speakers in one file
    /// may not share an identity, every one of those blips is forced to mint a
    /// new one. That is where a three-person recording turns into thirty-seven
    /// speakers, and no threshold fixes it, because the fragments are not wrong
    /// about being different from each other. They are just not worth a name.
    ///
    /// The floor gates minting only. A brief speaker can still join an identity
    /// that already exists, which is what recovers a long speaker that
    /// diarization split and left a two-second tail of. It simply cannot create
    /// one. Its words stay in the transcript either way, under the file-local
    /// label, because losing speech and declining to name a voice are different
    /// things.
    ///
    /// 0 disables the floor and restores the older behaviour.
    double min_speaker_speech = 8.0;

    /// Cosine similarity above which a file's speaker joins an existing identity.
    float match_threshold = 0.65f;
    /// Pairs landing between review and match get reported for a human to
    /// merge rather than silently split or silently joined.
    float review_threshold = 0.50f;
    /// Distance threshold for the offline reclustering pass.
    float cluster_threshold = 0.65f;

    // -- enrolled identities -------------------------------------------------
    /// Similarity above which a file's speaker joins an identity backed by
    /// human-supplied reference audio. Deliberately below match_threshold: the
    /// far side of that comparison is a clean recording of a known person
    /// rather than a centroid averaged out of whatever the corpus contained.
    float enroll_match_threshold = 0.55f;
    /// Let one enrolled identity take several speakers out of the same file.
    ///
    /// Diarization splitting one person into four clusters is the ordinary
    /// case, not the pathological one, and without this the other three are
    /// forced to mint new identities because two speakers in a file may not
    /// share one. A reference clip of a known person outranks a per-file
    /// clustering threshold, so where one exists the extra clusters collapse
    /// back into the person they came from. Turning this off restores the
    /// stricter behaviour at the cost of that mint.
    bool enroll_collapse = true;
    /// Analysis window over a reference clip, and the least speech a clip must
    /// carry to be worth enrolling.
    double enroll_window = 4.0;
    double enroll_min_clip = 3.0;

    /// Recluster when a batch finishes, and re-render whatever it moved.
    ///
    /// Incremental matching is order-dependent, so a batch always leaves the
    /// store slightly arbitrary until it is reconciled. Doing it automatically
    /// removes the one step everybody forgets. The re-render is not optional:
    /// reclustering can move a speaker to a different identity, which makes
    /// every transcript already on disk stale.
    bool recluster_after_batch = true;

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
