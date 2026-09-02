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

That second rule has a cost, and it is worth being honest about it, because it
is the reason a three-person recording can produce nine speakers.

Diarization over-splits. Not rarely, routinely: one person shifts posture, moves
off-mic, gets excited, and the per-file clustering decides that was somebody
else. Now the file has nine local speakers for three people. At most three of
them can claim the right identity, because no two may share one. The other six
are *forced* to mint new identities. The constraint that stops bad merges is
also the thing that turns one bad diarization into six permanent junk speakers,
and reclustering cannot undo it afterwards, because it enforces the same rule.

There is exactly one piece of evidence strong enough to overrule it, and the
corpus cannot produce that evidence on its own. See enrolment.

## The speech floor

Everything above assumes a diarized speaker is worth having. Most of them are
not.

Diarization returns a cluster for a two-word interjection off-mic with the same
confidence it returns one for the person running the meeting. It is not wrong:
those really are different bits of audio. It just has no opinion about which
ones matter, and the cannot-link rule then converts each one into a permanent
identity, because it may not share an existing one.

`min_speaker_speech` puts a floor under that, in clock time across the whole
file. Below it, a voice is not allowed to become a speaker of its own.

What the floor does **not** do is as important as what it does.

- **It does not gate matching.** A brief speaker can still join an identity that
  already exists. This is what recovers the two-second tail of a long speaker
  that diarization split off: the tail is under the floor, but it belongs to
  somebody who is already there, so it is attributed rather than discarded.
- **It does not gate transcription.** The words stay in the transcript under the
  file-local label. Declining to name a voice and throwing away what it said are
  different things, and only the first is wanted.
- **It does not gate reclustering into a hole.** Speakers under the floor with no
  identity are held out of the offline pass too, because clustering assigns an
  identity to every row it is handed, which would give back exactly what the
  floor declined to create.

The default is 8 seconds, on the reasoning that somewhere around there is where
a person stops being background and starts being a participant. On a real
27-minute three-person recording with 37 diarized speakers, this is what the
floor costs and buys:

```
floor   identities   left unnamed   their segments (still transcribed)
   0s           37              0   0
   3s           23             14   2
   5s           17             20   3
   8s            9             28   8
  15s            8             29   9
```

Eight and ten give the same answer, so the number is sitting on a plateau rather
than on a knife edge. Twenty-eight speakers disappear and eight of forty-six
transcript segments lose their attribution while keeping their text.

Nine is still not three. The floor removes the noise; it cannot merge the real
speaker that diarization split five ways, because those five are all in one file
and may not share an identity. That is what enrolment is for.

## Enrolment

Everything above infers who people are. Enrolment is you telling it.

You hand an identity reference audio of a known person. Scribble decodes it,
drops the silence, embeds it in overlapping windows, and keeps the result as a
profile attached to that identity. From then on, files match against the profile
as well as against the inferred centroids.

Three things change for an enrolled identity, and each of them is the answer to
a specific failure above.

**Clips are kept separately, and matching takes the best one.** Not the mean of
them. A voice on a phone and the same voice in a room sit in two different
places in embedding space, and the midpoint between those places resembles
neither recording: averaging a two-condition enrolment produces something worse
than either clip alone. Scoring against the nearest clip means every clip you
add can only widen what the profile recognises. Enrol somebody under each
condition they turn up in and it recognises all of them.

**Enrolled identities are matched first, at their own threshold.**
`enroll_match_threshold` sits below `match_threshold` (0.55 against 0.65) on
purpose. The far side of that comparison is clean audio of a known person rather
than a centroid averaged out of whatever the corpus happened to contain, so a
score that is ambiguous against one is not ambiguous against the other. Matching
enrolled profiles in their own pass, ahead of everything else, also means a
person you vouched for cannot lose their own speaker to an identity the corpus
invented at a marginally higher score.

**An enrolled identity may take more than one speaker out of a file.** This is
the cannot-link rule being overruled, and it is the whole reason enrolment fixes
the nine-speakers problem. A reference clip of a known person is better evidence
than a per-file clustering threshold, so where one exists, the extra clusters
collapse back into whoever they came from instead of minting junk:

```
3-person file, diarization emits 9 clusters

without enrolment              with Will enrolled
  speaker_00 -> Will   0.72      speaker_00 -> Will  0.72
  speaker_03 -> [new]  0.69      speaker_03 -> Will  0.69   collapsed
  speaker_05 -> [new]  0.71      speaker_05 -> Will  0.71   collapsed
  speaker_07 -> [new]  0.66      speaker_07 -> Will  0.66   collapsed
```

Set `enroll_collapse = false` to keep the strict rule and accept the mint.

An enrolled profile is also an anchor rather than a running average. Observations
never fold into it, because what makes it worth having is that it is exactly the
audio you vouched for, and averaging recordings into it walks it back towards
the inferred centroid it was created to replace. Reclustering leaves its
speakers alone for the same reason: there is no arrival-order arbitrariness to
reconcile in something a human asserted. `scribble recluster` reports those as
held.

### What makes a good clip

One person, three seconds of speech minimum, from a recording that sounds like
the recordings you are going to run.

Scribble measures two things and shows you both rather than deciding for you.
**Speech** is what survived the silence gate, so a thirty-second clip with
twenty-five seconds of room tone reports five. **One voice** is the lowest
similarity between any analysis window and the clip's own average: a clip
holding one person sits high, and a clip with a second voice, a music bed or a
hard cut between two recordings drops. A low number there is usually a second
person, and enrolling a clip with two people in it is worse than not enrolling
at all, because it produces a confident profile of nobody.

Longer is not better past the point where you have covered the person's range.
Two or three clips from different recordings beat one long clip from one.

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
| `enroll_match_threshold` | 0.55 | an enrolled person is absorbing other voices | an enrolled person keeps getting missed |

`min_speaker_speech` is seconds rather than a similarity, and is the first thing
to reach for when the speaker count is absurd. Thresholds decide who is who;
the floor decides who is worth the question.

`review_threshold` has to stay below `match_threshold`. Otherwise the uncertain
band is empty, no duplicate is ever reported, and everything looks perfect
forever. The config refuses to start rather than let you find that out in a
month.

Tune on a subset where you already know the answer. The right value depends on
your recording conditions far more than on the model, so treat any universal
number, including the defaults up there, as a starting guess.
