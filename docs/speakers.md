# Speaker identity

## The actual problem

Diarization tells you how many people are in **one** recording and who's talking
when. It does not tell you that speaker 2 on Tuesday is speaker 0 on Friday.
Every tool restarts labels at zero on every file.

For one file, fine. For four hundred files that's worthless, because "how many
people are in this clip" was never the question. The question is always "where
else does this person turn up", and every existing tool shrugs at it.

So this one keeps a store of voiceprints across the whole corpus. First
appearance of a voice mints an identity, later files match into it, naming it
once relabels everything. No enrollment step. No collecting reference clips
beforehand like you're building a police lineup. The corpus enrols itself as it
goes.

## What happens to one file

1. Diarization spits out turns labelled `speaker_00`, `speaker_01`. Meaningful
   inside this file and nowhere else.
2. For each one, the longest non-overlapping turns get embedded and averaged by
   duration into a single voiceprint. Turns under `embed_min_segment` are
   thrown out, because two seconds of "yeah" tells you about the vowel, not the
   person.
3. Every voiceprint gets compared to every stored identity by cosine similarity.
4. Pairs get assigned greedily, most confident first.
5. Anything left unmatched above `match_threshold` becomes a new identity.

## The two rules that make this work instead of almost work

**Greedy by confidence, not by file order.** If you assign in the order speakers
happen to appear, a mediocre match grabs an identity that a much better match
needed two rows later. Sorting all candidates by similarity and taking the best
first costs nothing and deletes an entire category of wrong.

**Two speakers in one file can never be the same identity.** Diarization already
decided they're different people. Treating that as a hard constraint kills most
bad merges before they can happen. It applies during matching and again during
reclustering. If the model says three people were in the room, you get three
identities, even if two of them sound like brothers.

## Voiceprints get updated, carefully

When a file's speaker matches an existing identity, the new voiceprint folds
into the stored centroid weighted by how many observations already back it.

An identity built from twenty files barely budges when the twenty-first arrives,
so one garbage recording can't drag an established centroid off its person. A
brand new identity moves a lot on its second observation, which is correct,
because it has seen exactly one example and shouldn't be precious about it.

## Reclustering

Incremental matching is order-dependent. Feed the same corpus in a different
order and the groupings change, because whoever arrived first defined the
centroids everyone else got measured against. Nothing is technically wrong. It's
just arbitrary, and arbitrary is a miserable property for something you might
have to explain to somebody who matters.

`scribble recluster` pulls every voiceprint, clusters them in one pass with
average-linkage agglomerative clustering under the same cannot-link rule, and
rewrites the assignments.

**Average linkage, specifically.** Single linkage chains distinct voices
together through a trail of borderline pairs, and the failure mode is
spectacular: run it on a decent-sized corpus and you get one enormous speaker
who is apparently present in every file ever recorded, because A is near B is
near C is near everybody. Average linkage makes the whole group agree first.

Clusters reclaim whatever identity their members already had, preferring one
that's been named. Reclustering never eats a name you typed. I checked. Twice.

Run it once at the end of a batch. Not after every file.

## Duplicates

Two identities that are secretly one person is the common failure, and no, you
cannot fix it by lowering the threshold.

The cause is domain mismatch. The same voice down a phone line and through a
studio mic produces genuinely different voiceprints, and the threshold low
enough to merge those two is also low enough to merge two different people who
both sound muffled. There is no number that gets both right. Not a tuning
problem, a physics problem. Anybody who tells you otherwise is selling
something.

So `scribble dupes` lists pairs sitting between `review_threshold` and
`match_threshold` and lets you decide. It skips any pair that appears together
in one file, because those are provably different humans. `scribble merge <from>
<into>` when it's real. The survivor keeps whichever name exists, so merging an
unnamed duplicate into a named one never loses your work.

Dismissals are remembered. You will not be asked about the same pair again,
because a review list that repeats itself is a review list nobody reads.

The opposite failure, two people welded into one identity, gets fixed from the
GUI speaker panel: right click, split that file's speaker out into a fresh
identity.

## Thresholds

| Setting | Default | Raise when | Lower when |
|---|---|---|---|
| `match_threshold` | 0.65 | different people are merging | one person keeps splitting |
| `review_threshold` | 0.50 | `dupes` is mostly noise | real duplicates aren't surfacing |
| `cluster_threshold` | 0.65 | reclustering over-merges | reclustering leaves obvious dupes |

`review_threshold` has to stay below `match_threshold`. Otherwise the uncertain
band is empty, no duplicate is ever reported, and everything looks perfect
forever. The config refuses to start rather than let you find that out in a
month.

Tune on a subset where you already know the answer. The right value depends on
your recording conditions far more than on the model, so treat any universal
number, including the defaults up there, as a starting guess.
