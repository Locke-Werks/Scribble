# Speaker identity

Diarization tells you how many people are in one recording and which of them is
talking when. It does not tell you that speaker 2 in Tuesday's meeting is
speaker 0 in Friday's. Every tool restarts its labels at zero on every file.

ScribeEveryone keeps a store of voiceprints that spans the whole corpus. The
first appearance of a voice creates a durable identity, later files match into
it, and naming that identity once relabels every transcript it appears in.
There is no enrollment step and no reference recordings to collect.

## How a file is resolved

1. Diarization produces turns labelled `speaker_00`, `speaker_01` and so on,
   meaningful only inside this file.
2. For each of those, the longest non-overlapping turns are embedded and
   averaged, weighted by duration, into one voiceprint. Turns shorter than
   `embed_min_segment` are skipped: brief interjections carry too little signal
   and drag the centroid off the speaker.
3. Every voiceprint is compared against every stored identity by cosine
   similarity.
4. Candidate pairs are assigned greedily, most confident first.
5. Anything left unmatched above `match_threshold` mints a new identity.

## The two constraints that make it work

**Greedy by confidence, not by file order.** Assigning in the order speakers
happen to appear lets a mediocre match claim an identity that a later, better
match needed. Sorting all candidate pairs by similarity and taking the best
first avoids that.

**Two speakers in one file can never be the same identity.** Diarization has
already decided they are different people. Honouring that as a hard constraint
removes most bad merges before they can happen. It applies during matching and
again during reclustering.

## Updating a voiceprint

When a file's speaker matches an existing identity, the new voiceprint is
folded into the stored centroid weighted by how many observations already back
it. An identity built from twenty files barely moves when a twenty-first
arrives, so one bad recording cannot drag an established centroid off its
speaker.

## Reclustering

Incremental matching is order-dependent. Ingest the same corpus in a different
order and the groupings differ, because early arrivals define the centroids
that later ones are measured against.

`scribe recluster` removes that dependence. It pulls every stored voiceprint,
clusters them in one pass with average-linkage agglomerative clustering under
the same cannot-link constraint, and rewrites the assignments.

Average linkage is deliberate. Single linkage chains distinct voices together
through a string of borderline pairs, which is the classic way corpus-wide
diarization collapses into one enormous speaker.

Clusters reclaim the identity their members already carried, preferring one
that has been named. Reclustering never costs you a name you have entered.

Run it after a large batch, not after every file.

## Duplicates

Two identities that are really one person is the common failure, and it is not
fixable by lowering the threshold. The usual cause is domain mismatch: the same
voice over a phone line and through a studio microphone genuinely does produce
voiceprints that sit below any threshold safe enough to use.

`scribe dupes` lists identity pairs whose similarity falls between
`review_threshold` and `match_threshold`, skipping any pair that appears
together in a single file, since those are provably different people. Merge
with `scribe merge <from> <into>`. The surviving identity keeps whichever name
exists.

The opposite error, two people merged into one identity, is handled by
splitting one file's speaker back out into a fresh identity from the GUI's
speaker panel.

## Thresholds

| Setting | Default | Raise it when | Lower it when |
|---|---|---|---|
| `match_threshold` | 0.65 | different people are being merged | one person keeps splitting into several identities |
| `review_threshold` | 0.50 | `dupes` reports too much noise | real duplicates are not being surfaced |
| `cluster_threshold` | 0.65 | reclustering over-merges | reclustering leaves obvious duplicates |

`review_threshold` must stay below `match_threshold`, otherwise the uncertain
band is empty and no duplicate is ever reported. The config rejects that
combination at startup.

Tune on a subset where you know the answer before running a large corpus. The
right value depends on the recording conditions more than on the model.
