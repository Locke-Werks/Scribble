# Speaker identity

## The problem, stated plainly

Diarization tells you how many people are in **one** recording and which of them
is talking when. It does not tell you that speaker 2 in Tuesday's meeting is
speaker 0 in Friday's. Every tool restarts its labels at zero on every file.

For one file that is fine. For four hundred files it is worthless, because the
question you actually have is never "how many people are in this clip", it is
"where else does this person show up".

ScribeEveryone keeps a store of voiceprints spanning the whole corpus. First
appearance of a voice creates a durable identity, later files match into it, and
naming it once relabels every transcript it appears in. No enrollment step, no
reference recordings to collect beforehand. The corpus enrols itself as it goes.

## How one file gets resolved

1. Diarization produces turns labelled `speaker_00`, `speaker_01`, meaningful
   only inside this file.
2. For each of those, the longest non-overlapping turns get embedded and
   averaged by duration into one voiceprint. Turns shorter than
   `embed_min_segment` are skipped, because two seconds of "yeah" tells you
   about the phoneme, not the person.
3. Each voiceprint is compared against every stored identity by cosine
   similarity.
4. Candidate pairs are assigned greedily, most confident first.
5. Anything still unmatched above `match_threshold` mints a new identity.

## The two constraints that make it work instead of almost work

**Greedy by confidence, not by file order.** Assigning in the order speakers
happen to appear lets a mediocre match claim an identity that a better match
needed two rows later. Sorting all candidate pairs by similarity and taking the
best first costs nothing and removes a whole category of wrong.

**Two speakers in one file can never be the same identity.** Diarization already
decided they are different people. Treating that as a hard constraint kills most
bad merges before they can happen, and it applies during matching and again
during reclustering. If the segmentation model says there are three people in
the room, you get three identities out, even if two of them sound similar.

## Updating a voiceprint

When a file's speaker matches an existing identity, the new voiceprint is folded
into the stored centroid, weighted by how many observations already back it.

An identity built from twenty files barely moves when a twenty-first arrives, so
one bad recording cannot drag an established centroid off its speaker. A brand
new identity is much more malleable, which is correct: it has seen one example
and should update hard on the second.

## Reclustering

Incremental matching is order-dependent. Ingest the same corpus in a different
order and the groupings differ, because early arrivals define the centroids that
later ones get measured against. Nothing is wrong exactly, it is just arbitrary,
and arbitrary is a bad property for something you might have to explain.

`scribe recluster` pulls every stored voiceprint, clusters them in one pass with
average-linkage agglomerative clustering under the same cannot-link constraint,
and rewrites the assignments.

**Average linkage is deliberate.** Single linkage chains distinct voices
together through a string of borderline pairs, and the failure mode is
spectacular: run it on a big corpus and you get one enormous speaker who is
apparently in every file, because A is close to B is close to C is close to
everyone. Average linkage requires the whole group to agree.

Clusters reclaim the identity their members already carried, preferring one that
has been named. Reclustering never costs you a name you typed.

Run it once at the end of a large batch. It is not something to do after every
file.

## Duplicates

Two identities that are really one person is the common failure, and lowering
the threshold does not fix it.

The usual cause is domain mismatch. The same voice over a phone line and through
a studio microphone genuinely produces voiceprints that sit below any threshold
you could safely use, because the threshold low enough to merge them is also low
enough to merge two different people who are both muffled. There is no number
that gets both cases right, so the software stops pretending there is.

`scribe dupes` lists identity pairs whose similarity falls between
`review_threshold` and `match_threshold`. It skips any pair that appears
together in a single file, because those are provably different people. Merge
with `scribe merge <from> <into>`. The survivor keeps whichever name exists, so
merging an unnamed duplicate into a named one never loses your work.

Dismissals are remembered. A pair you rejected stays rejected instead of turning
up on every review.

The opposite error, two people merged into one identity, is fixed from the GUI's
speaker panel: right click, split that file's speaker back out into a fresh
identity.

## Thresholds

| Setting | Default | Raise it when | Lower it when |
|---|---|---|---|
| `match_threshold` | 0.65 | different people are being merged | one person keeps splitting into several |
| `review_threshold` | 0.50 | `dupes` is mostly noise | real duplicates are not being surfaced |
| `cluster_threshold` | 0.65 | reclustering over-merges | reclustering leaves obvious duplicates |

`review_threshold` must stay below `match_threshold`. Otherwise the uncertain
band is empty and no duplicate is ever reported, which looks exactly like
everything being fine. The config rejects that combination at startup rather
than letting you discover it in a month.

Tune on a subset where you already know the answer before running a large
corpus. The right value depends on your recording conditions far more than on
the model, so anyone quoting you a universal number is guessing.
