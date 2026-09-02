#pragma once

#include <cstdint>
#include <vector>

#include "db.hpp"
#include "types.hpp"

namespace scribble {

/// Cosine similarity on L2-normalised vectors. Inputs need not be normalised.
float cosine(const std::vector<float> &a, const std::vector<float> &b);

void l2_normalise(std::vector<float> &v);

/// Weighted mean of several voiceprints, renormalised.
std::vector<float> centroid_of(const std::vector<std::vector<float>> &vectors,
                               const std::vector<double> &weights);

/// A human-supplied identity, held as its individual reference clips.
///
/// Scoring takes the best clip rather than the mean of them. One person
/// recorded down a phone and again in a room occupies two separate places in
/// embedding space, and the midpoint between those places resembles neither
/// recording, so averaging a multi-condition enrolment makes it worse than a
/// single-condition one. Taking the maximum means each extra clip can only
/// widen what the profile recognises.
struct EnrolledProfile {
    std::int64_t global_id = -1;
    std::vector<std::vector<float>> clips;
};

/// Best similarity between a voiceprint and any clip backing the profile.
float score_against(const EnrolledProfile &profile, const std::vector<float> &voiceprint);

/// Loads every enrolled identity in the store, ready for matching.
std::vector<EnrolledProfile> load_enrolled_profiles(Database &db);

/// Matches this file's speakers against the global store.
///
/// Two constraints make this work rather than almost work. Assignment is
/// greedy by descending similarity, so the most confident match wins the
/// identity. And two speakers in the same file can never resolve to the same
/// global identity: diarization already decided they are different people, and
/// honouring that kills most bad merges before they happen.
///
/// Enrolment is the one thing allowed to override that second constraint, and
/// it has to be, because the constraint is what turns over-segmentation into
/// permanent damage. When diarization splits one person across four clusters
/// in a three-person recording, exactly one of those clusters can claim the
/// right identity and the other three are forced to mint new ones. A reference
/// clip of a known person is better evidence than a per-file clustering
/// threshold, so where one is available it wins and the extra clusters collapse
/// back into the person they came from.
struct MatchInput {
    std::int64_t file_id = -1;
    std::vector<LocalSpeaker> locals;
    float threshold = 0.65f;
    /// Bar for joining an enrolled identity. Lower than `threshold` on purpose:
    /// the far side of that comparison is clean audio of a known person rather
    /// than a centroid inferred from whatever the corpus happened to contain,
    /// so a score that is ambiguous against one is not ambiguous against the
    /// other.
    float enrolled_threshold = 0.55f;
    /// Let one enrolled identity take several of this file's speakers.
    bool collapse_enrolled = true;
    /// Enrolled identities, loaded once per batch rather than once per file.
    const std::vector<EnrolledProfile> *profiles = nullptr;
    /// Speech a local speaker must carry before it may mint a new identity.
    /// Matching is unaffected: a brief speaker can join somebody who already
    /// exists, it just cannot invent anybody. 0 disables the floor.
    double min_speaker_speech = 0.0;
};

std::vector<SpeakerResolution> resolve_against_store(Database &db, const MatchInput &input);

/// Offline clustering over every stored voiceprint.
///
/// Incremental matching assigns identities in arrival order, so the same
/// corpus ingested in a different order produces different groupings. This
/// pass removes that dependence: it clusters everything at once, then hands
/// each cluster back the existing identity its members already carried,
/// preferring one that has been named so human work is never discarded.
struct ClusterOptions {
    float threshold = 0.65f;
    /// Average linkage. Single linkage chains distinct voices together through
    /// borderline pairs, which is the classic way corpus-wide diarization
    /// collapses into one speaker.
    bool average_linkage = true;
};

struct ClusterAssignment {
    std::vector<int> cluster_of;  ///< index into the input vector
    int n_clusters = 0;
};

/// Agglomerative clustering with a cannot-link constraint: two entries sharing
/// a `file_id` are never merged.
ClusterAssignment constrained_agglomerative(const std::vector<std::vector<float>> &vectors,
                                            const std::vector<std::int64_t> &file_ids,
                                            const ClusterOptions &opts);

/// Global identity pairs sitting in the uncertain band. Domain mismatch, a
/// phone recording against a studio mic, is the usual reason one person ends
/// up as two identities, and no threshold fixes that. Surfacing the pairs
/// hands the merge to a human instead of making them hunt for it.
std::vector<DuplicateCandidate> duplicate_candidates(Database &db, float low, float high);

}  // namespace scribble
