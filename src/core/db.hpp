#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "types.hpp"

struct sqlite3;

namespace scribble {

namespace fs = std::filesystem;

struct FileRecord {
    std::int64_t id = -1;
    std::string path;
    int track = 0;
    int track_count = 1;
    std::int64_t size = 0;
    double mtime = 0.0;
    double duration = 0.0;
    int channels = 1;
    std::string language;
    std::string model;
    std::string status;  ///< pending | running | done | failed | skipped
    std::string error;
    std::string updated_at;
};

/// A file-local speaker plus its voiceprint. One row per diarized speaker per
/// file, which is the unit that global clustering operates on.
struct LocalSpeaker {
    std::int64_t id = -1;
    std::int64_t file_id = -1;
    std::string label;
    std::vector<float> centroid;
    int n_segments = 0;
    double total_duration = 0.0;
    std::int64_t global_id = -1;
    float similarity = 0.0f;
};

/// One piece of reference audio for a known person, plus the voiceprint taken
/// from it. Clips are kept individually rather than folded into the identity's
/// centroid so matching can score against the nearest one.
struct EnrollmentClip {
    std::int64_t id = -1;
    std::int64_t global_id = -1;
    std::string source;    ///< file the clip came from, for display
    double start = 0.0;    ///< offset into that file
    double end = 0.0;
    double duration = 0.0;  ///< speech actually used, after the silence gate
    std::vector<float> centroid;
    int n_windows = 0;
    /// Lowest similarity between any analysis window and the clip's own mean.
    /// A clip holding one voice sits high; a clip with two people in it, or
    /// with a stretch of music, drops.
    float coherence = 0.0f;
    std::string created_at;
};

class Database {
public:
    explicit Database(const fs::path &path);
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    void begin();
    void commit();
    void rollback();

    // -- files --------------------------------------------------------------
    /// Inserts or returns the existing row. `changed` reports whether size or
    /// mtime moved, which is what invalidates a previous transcription.
    std::int64_t upsert_file(const FileRecord &rec, bool *changed);
    std::optional<FileRecord> file(std::int64_t id) const;
    std::vector<FileRecord> files(const std::string &status_filter = {}) const;
    void set_file_status(std::int64_t id, const std::string &status,
                         const std::string &error = {});
    void update_file_meta(std::int64_t id, double duration, int channels,
                          const std::string &language, const std::string &model);

    // -- segments -----------------------------------------------------------
    void clear_segments(std::int64_t file_id);
    void insert_segments(std::int64_t file_id, const std::vector<Segment> &segments);
    std::vector<Segment> segments(std::int64_t file_id) const;

    // -- speakers -----------------------------------------------------------
    void clear_local_speakers(std::int64_t file_id);
    std::int64_t insert_local_speaker(const LocalSpeaker &sp);
    std::vector<LocalSpeaker> local_speakers(std::int64_t file_id) const;
    std::vector<LocalSpeaker> all_local_speakers() const;
    void assign_local(std::int64_t local_id, std::int64_t global_id, float similarity);

    std::int64_t create_global(const std::vector<float> &centroid, double duration);
    void update_global_centroid(std::int64_t global_id, const std::vector<float> &centroid,
                                int n_locals, double duration);
    std::vector<GlobalSpeaker> globals() const;
    std::optional<GlobalSpeaker> global(std::int64_t id) const;
    std::vector<float> global_centroid(std::int64_t id) const;
    void rename_global(std::int64_t id, const std::string &name);
    void set_global_notes(std::int64_t id, const std::string &notes);
    /// Folds `from` into `into` and deletes the empty row. Returns rows moved.
    int merge_globals(std::int64_t from, std::int64_t into);
    /// Pulls one file's speaker out into a fresh identity, for when clustering
    /// wrongly joined two voices.
    std::int64_t split_local(std::int64_t local_id);
    void delete_empty_globals();
    /// Global speakers whose files overlap the given file.
    std::vector<std::int64_t> globals_in_file(std::int64_t file_id) const;

    /// Recomputes how many speakers and how much speech an identity carries,
    /// from the rows that actually point at it, without touching the centroid.
    /// An enrolled identity accumulates files and speech time for display while
    /// its voiceprint stays exactly what the reference clips made it.
    void refresh_global_usage(std::int64_t id);

    // -- enrolment ----------------------------------------------------------
    /// Creates an identity from a name alone, before any audio has matched it.
    std::int64_t create_named_global(const std::string &name);
    std::int64_t add_enrollment(const EnrollmentClip &clip);
    std::vector<EnrollmentClip> enrollments(std::int64_t global_id) const;
    /// Every clip in the store, ordered by identity. Used to build the match
    /// profiles once per batch rather than once per file.
    std::vector<EnrollmentClip> all_enrollments() const;
    void delete_enrollment(std::int64_t clip_id);
    int delete_enrollments_for(std::int64_t global_id);
    /// Recomputes the identity's centroid from its clips and sets or clears the
    /// enrolled flag to match whether any remain.
    void refresh_enrollment(std::int64_t global_id);
    bool is_enrolled(std::int64_t global_id) const;

    // -- destructive ---------------------------------------------------------
    /// Deletes every file, segment, speaker and dismissal, leaving an empty
    /// schema. Transcripts already written to disk are untouched, but the
    /// identities behind them are gone, so re-rendering after this produces
    /// unattributed text.
    void clear_all();

    /// Copies the database through SQLite's backup API, which is safe while
    /// connections are open, unlike copying the file and hoping the WAL agrees.
    bool backup_to(const fs::path &dest, std::string *error);

    /// Row counts, for telling someone what they are about to destroy.
    struct Counts {
        int files = 0;
        int segments = 0;
        int speakers = 0;
        int named_speakers = 0;
        int enrolled_speakers = 0;
        int enrollment_clips = 0;
    };
    Counts counts() const;

    // -- duplicate review ---------------------------------------------------
    /// Records that a human compared two identities and ruled them out, so the
    /// pair stops being offered on every subsequent review.
    void dismiss_pair(std::int64_t a, std::int64_t b);
    bool pair_dismissed(std::int64_t a, std::int64_t b) const;
    void undismiss_pair(std::int64_t a, std::int64_t b);
    std::vector<std::pair<std::int64_t, std::int64_t>> dismissed_pairs() const;

    // -- outputs ------------------------------------------------------------
    void record_output(std::int64_t file_id, const std::string &format,
                       const std::string &path);
    std::vector<std::string> outputs(std::int64_t file_id) const;

    sqlite3 *handle() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace scribble
