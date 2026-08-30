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

/// Matches this file's speakers against the global store.
///
/// Two constraints make this work rather than almost work. Assignment is
/// greedy by descending similarity, so the most confident match wins the
/// identity. And two speakers in the same file can never resolve to the same
/// global identity: diarization already decided they are different people, and
/// honouring that kills most bad merges before they happen.
struct MatchInput {
    std::int64_t file_id = -1;
    std::vector<LocalSpeaker> locals;
    float threshold = 0.65f;
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
