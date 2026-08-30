#include "speakers.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace scribe {
namespace {

struct Candidate {
    float similarity;
    int local_index;
    std::int64_t global_id;
};

}  // namespace

float cosine(const std::vector<float> &a, const std::vector<float> &b) {
    if (a.empty() || a.size() != b.size()) {
        return 0.0f;
    }
    double dot = 0.0;
    double na = 0.0;
    double nb = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * b[i];
        na += static_cast<double>(a[i]) * a[i];
        nb += static_cast<double>(b[i]) * b[i];
    }
    if (na <= 1e-12 || nb <= 1e-12) {
        return 0.0f;
    }
    return static_cast<float>(dot / (std::sqrt(na) * std::sqrt(nb)));
}

void l2_normalise(std::vector<float> &v) {
    double norm = 0.0;
    for (float x : v) {
        norm += static_cast<double>(x) * x;
    }
    norm = std::sqrt(norm);
    if (norm <= 1e-12) {
        return;
    }
    for (float &x : v) {
        x = static_cast<float>(x / norm);
    }
}

std::vector<float> centroid_of(const std::vector<std::vector<float>> &vectors,
                               const std::vector<double> &weights) {
    std::vector<float> out;
    if (vectors.empty()) {
        return out;
    }
    out.assign(vectors.front().size(), 0.0f);

    double total = 0.0;
    for (size_t i = 0; i < vectors.size(); ++i) {
        if (vectors[i].size() != out.size()) {
            continue;
        }
        double w = i < weights.size() ? weights[i] : 1.0;
        if (w <= 0.0) {
            w = 1.0;
        }
        for (size_t j = 0; j < out.size(); ++j) {
            out[j] += static_cast<float>(vectors[i][j] * w);
        }
        total += w;
    }
    if (total > 0.0) {
        for (float &x : out) {
            x = static_cast<float>(x / total);
        }
    }
    l2_normalise(out);
    return out;
}

std::vector<SpeakerResolution> resolve_against_store(Database &db, const MatchInput &input) {
    std::vector<SpeakerResolution> results(input.locals.size());
    for (size_t i = 0; i < input.locals.size(); ++i) {
        results[i].local_label = input.locals[i].label;
        results[i].total_duration = input.locals[i].total_duration;
    }

    const auto existing = db.globals();

    std::vector<Candidate> candidates;
    candidates.reserve(input.locals.size() * existing.size());
    for (size_t i = 0; i < input.locals.size(); ++i) {
        if (input.locals[i].centroid.empty()) {
            continue;
        }
        for (const auto &g : existing) {
            auto centroid = db.global_centroid(g.id);
            if (centroid.empty()) {
                continue;
            }
            float sim = cosine(input.locals[i].centroid, centroid);
            if (sim >= input.threshold) {
                candidates.push_back({sim, static_cast<int>(i), g.id});
            }
        }
    }

    // Most confident match wins the identity. Assigning in file order instead
    // lets a mediocre match claim a voice that a later, better one needed.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.similarity > b.similarity; });

    std::vector<bool> local_taken(input.locals.size(), false);
    std::unordered_set<std::int64_t> global_taken;

    for (const auto &c : candidates) {
        if (local_taken[static_cast<size_t>(c.local_index)]) {
            continue;
        }
        // Diarization already decided these are different people, so two
        // speakers in one file can never collapse into one identity. This
        // single constraint removes most of the bad merges.
        if (global_taken.count(c.global_id)) {
            continue;
        }
        local_taken[static_cast<size_t>(c.local_index)] = true;
        global_taken.insert(c.global_id);

        auto &res = results[static_cast<size_t>(c.local_index)];
        res.global_id = c.global_id;
        res.similarity = c.similarity;
        res.minted = false;
    }

    for (size_t i = 0; i < input.locals.size(); ++i) {
        const auto &local = input.locals[i];
        auto &res = results[i];

        if (res.global_id < 0) {
            if (local.centroid.empty()) {
                continue;
            }
            res.global_id = db.create_global(local.centroid, local.total_duration);
            res.similarity = 1.0f;
            res.minted = true;
        } else {
            // Fold the new voiceprint into the identity, weighted by how many
            // observations already back it, so one bad file cannot drag an
            // established centroid off its speaker.
            auto stored = db.global(res.global_id);
            auto centroid = db.global_centroid(res.global_id);
            if (stored && !centroid.empty()) {
                double n = std::max(1, stored->n_locals);
                std::vector<std::vector<float>> parts{centroid, local.centroid};
                std::vector<double> weights{n, 1.0};
                auto updated = centroid_of(parts, weights);
                db.update_global_centroid(res.global_id, updated, stored->n_locals + 1,
                                          stored->total_duration + local.total_duration);
            }
        }

        db.assign_local(local.id, res.global_id, res.similarity);

        auto stored = db.global(res.global_id);
        res.display = stored ? stored->display() : std::string{};
    }

    return results;
}

ClusterAssignment constrained_agglomerative(const std::vector<std::vector<float>> &vectors,
                                            const std::vector<std::int64_t> &file_ids,
                                            const ClusterOptions &opts) {
    ClusterAssignment out;
    const int n = static_cast<int>(vectors.size());
    out.cluster_of.assign(static_cast<size_t>(n), -1);
    if (n == 0) {
        return out;
    }

    std::vector<std::vector<float>> unit = vectors;
    for (auto &v : unit) {
        l2_normalise(v);
    }

    std::vector<std::vector<float>> sim(static_cast<size_t>(n),
                                        std::vector<float>(static_cast<size_t>(n), 0.0f));
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            float s = cosine(unit[static_cast<size_t>(i)], unit[static_cast<size_t>(j)]);
            sim[static_cast<size_t>(i)][static_cast<size_t>(j)] = s;
            sim[static_cast<size_t>(j)][static_cast<size_t>(i)] = s;
        }
    }

    std::vector<bool> active(static_cast<size_t>(n), true);
    std::vector<int> size(static_cast<size_t>(n), 1);
    std::vector<int> version(static_cast<size_t>(n), 0);
    std::vector<std::vector<int>> members(static_cast<size_t>(n));
    std::vector<std::set<std::int64_t>> files(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        members[static_cast<size_t>(i)].push_back(i);
        if (static_cast<size_t>(i) < file_ids.size()) {
            files[static_cast<size_t>(i)].insert(file_ids[static_cast<size_t>(i)]);
        }
    }

    struct Pair {
        float similarity;
        int a;
        int b;
        int va;
        int vb;
        bool operator<(const Pair &o) const { return similarity < o.similarity; }
    };

    std::priority_queue<Pair> heap;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (sim[static_cast<size_t>(i)][static_cast<size_t>(j)] >= opts.threshold) {
                heap.push({sim[static_cast<size_t>(i)][static_cast<size_t>(j)], i, j, 0, 0});
            }
        }
    }

    const auto shares_file = [&](int a, int b) {
        const auto &fa = files[static_cast<size_t>(a)];
        const auto &fb = files[static_cast<size_t>(b)];
        for (auto id : fa) {
            if (fb.count(id)) {
                return true;
            }
        }
        return false;
    };

    while (!heap.empty()) {
        Pair p = heap.top();
        heap.pop();

        if (!active[static_cast<size_t>(p.a)] || !active[static_cast<size_t>(p.b)]) {
            continue;
        }
        // Lazy invalidation: a cluster that has since merged has a newer
        // version, so this entry describes a similarity that no longer exists.
        if (version[static_cast<size_t>(p.a)] != p.va ||
            version[static_cast<size_t>(p.b)] != p.vb) {
            continue;
        }
        if (p.similarity < opts.threshold) {
            break;
        }
        if (shares_file(p.a, p.b)) {
            continue;
        }

        const int a = p.a;
        const int b = p.b;
        const auto sa = static_cast<float>(size[static_cast<size_t>(a)]);
        const auto sb = static_cast<float>(size[static_cast<size_t>(b)]);

        for (int d = 0; d < n; ++d) {
            if (d == a || d == b || !active[static_cast<size_t>(d)]) {
                continue;
            }
            float merged;
            if (opts.average_linkage) {
                // Single linkage chains distinct voices together through a
                // string of borderline pairs, which is how corpus-wide
                // diarization collapses into one speaker.
                merged = (sa * sim[static_cast<size_t>(a)][static_cast<size_t>(d)] +
                          sb * sim[static_cast<size_t>(b)][static_cast<size_t>(d)]) /
                         (sa + sb);
            } else {
                merged = std::min(sim[static_cast<size_t>(a)][static_cast<size_t>(d)],
                                  sim[static_cast<size_t>(b)][static_cast<size_t>(d)]);
            }
            sim[static_cast<size_t>(a)][static_cast<size_t>(d)] = merged;
            sim[static_cast<size_t>(d)][static_cast<size_t>(a)] = merged;
        }

        auto &ma = members[static_cast<size_t>(a)];
        const auto &mb = members[static_cast<size_t>(b)];
        ma.insert(ma.end(), mb.begin(), mb.end());
        files[static_cast<size_t>(a)].insert(files[static_cast<size_t>(b)].begin(),
                                             files[static_cast<size_t>(b)].end());
        size[static_cast<size_t>(a)] += size[static_cast<size_t>(b)];
        active[static_cast<size_t>(b)] = false;
        version[static_cast<size_t>(a)]++;

        for (int d = 0; d < n; ++d) {
            if (d == a || !active[static_cast<size_t>(d)]) {
                continue;
            }
            float s = sim[static_cast<size_t>(a)][static_cast<size_t>(d)];
            if (s >= opts.threshold && !shares_file(a, d)) {
                heap.push({s, std::min(a, d), std::max(a, d),
                           version[static_cast<size_t>(std::min(a, d))],
                           version[static_cast<size_t>(std::max(a, d))]});
            }
        }
    }

    int next = 0;
    for (int i = 0; i < n; ++i) {
        if (!active[static_cast<size_t>(i)]) {
            continue;
        }
        for (int m : members[static_cast<size_t>(i)]) {
            out.cluster_of[static_cast<size_t>(m)] = next;
        }
        ++next;
    }
    out.n_clusters = next;
    return out;
}

std::vector<DuplicateCandidate> duplicate_candidates(Database &db, float low, float high) {
    std::vector<DuplicateCandidate> out;
    const auto globals = db.globals();
    if (globals.size() < 2) {
        return out;
    }

    std::unordered_map<std::int64_t, std::vector<float>> centroids;
    std::unordered_map<std::int64_t, std::set<std::int64_t>> files;
    for (const auto &g : globals) {
        auto c = db.global_centroid(g.id);
        if (!c.empty()) {
            centroids[g.id] = std::move(c);
        }
    }
    for (const auto &local : db.all_local_speakers()) {
        if (local.global_id >= 0) {
            files[local.global_id].insert(local.file_id);
        }
    }

    for (size_t i = 0; i < globals.size(); ++i) {
        for (size_t j = i + 1; j < globals.size(); ++j) {
            const auto &a = globals[i];
            const auto &b = globals[j];
            auto ita = centroids.find(a.id);
            auto itb = centroids.find(b.id);
            if (ita == centroids.end() || itb == centroids.end()) {
                continue;
            }

            // Two identities that speak in the same file are provably
            // different people, so however close their voiceprints sit they
            // are never a duplicate.
            const auto &fa = files[a.id];
            const auto &fb = files[b.id];
            bool overlap = false;
            for (auto id : fa) {
                if (fb.count(id)) {
                    overlap = true;
                    break;
                }
            }
            if (overlap) {
                continue;
            }

            // A pair a human has already ruled out stays out. Re-offering it on
            // every review is how the duplicates list becomes noise that gets
            // ignored wholesale.
            if (db.pair_dismissed(a.id, b.id)) {
                continue;
            }

            float sim = cosine(ita->second, itb->second);
            if (sim >= low && sim < high) {
                DuplicateCandidate dc;
                dc.left = a.id;
                dc.right = b.id;
                dc.left_display = a.display();
                dc.right_display = b.display();
                dc.similarity = sim;
                dc.left_files = a.n_files;
                dc.right_files = b.n_files;
                out.push_back(std::move(dc));
            }
        }
    }

    std::sort(out.begin(), out.end(),
              [](const DuplicateCandidate &a, const DuplicateCandidate &b) {
                  return a.similarity > b.similarity;
              });
    return out;
}

}  // namespace scribe
