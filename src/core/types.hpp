#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace scribe {

/// A word with its own timing, from Whisper's cross-attention DTW.
struct Word {
    double start = 0.0;
    double end = 0.0;
    std::string text;
    float probability = 0.0f;
};

/// One utterance. Speaker fields fill in after diarization, which runs as a
/// second pass, so a segment is emitted to the UI with text first and gets
/// labelled a moment later.
struct Segment {
    int index = 0;
    double start = 0.0;
    double end = 0.0;
    std::string text;
    std::vector<Word> words;

    /// Per-file diarization label, e.g. "speaker_00". Empty until diarized.
    std::string local_label;

    /// Corpus-wide identity. -1 until resolved against the speaker store.
    std::int64_t global_id = -1;

    float avg_logprob = 0.0f;
    float no_speech = 0.0f;
};

/// The outcome of matching one file-local speaker against the global store.
struct SpeakerResolution {
    std::string local_label;
    std::int64_t global_id = -1;
    std::string display;      ///< assigned name, else SPEAKER_0042
    float similarity = 0.0f;  ///< cosine against the matched centroid
    bool minted = false;      ///< true when this voice was not seen before
    double total_duration = 0.0;
};

/// A pair of global speakers close enough to be worth a human glance.
/// Domain mismatch (phone vs studio) is the usual cause of a voice landing
/// under threshold and getting two identities.
struct DuplicateCandidate {
    std::int64_t left = -1;
    std::int64_t right = -1;
    std::string left_display;
    std::string right_display;
    float similarity = 0.0f;
    int left_files = 0;
    int right_files = 0;
};

struct GlobalSpeaker {
    std::int64_t id = -1;
    std::string name;   ///< empty until a human names it
    std::string notes;
    int n_locals = 0;
    int n_files = 0;
    double total_duration = 0.0;
    std::string created_at;

    std::string display() const;
};

/// One unit of work. A multi-track file yields one job per track, because
/// separate tracks are free perfect diarization and downmixing throws that away.
struct MediaJob {
    std::int64_t file_id = -1;
    std::string source_path;
    int track = 0;         ///< 0 when the file was decoded as a single mono mix
    int track_count = 1;
    double duration = 0.0;
    int channels = 1;
};

enum class Stage {
    Queued,
    Probing,
    Extracting,
    Isolating,
    Transcribing,
    Diarizing,
    Embedding,
    Resolving,
    Writing,
    Done,
    Failed,
    Skipped,
};

const char *stage_name(Stage s);

enum class LogLevel { Debug, Info, Warn, Error };

const char *log_level_name(LogLevel l);

}  // namespace scribe
