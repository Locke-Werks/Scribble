# Using Scribble

You pointed it at a folder. It chewed through 200 files. Now there are 47 people
called `SPEAKER_0031` and you are staring at a toolbar full of buttons whose
names made sense to me at the time.

Cool. Let's fix that.

## The whole thing in six commands

```powershell
scribble run D:\recordings     # do the work
scribble recluster             # ONCE. at the end. we'll get to it.
scribble dupes                 # "are these two the same guy"
scribble merge 7 3             # they were
scribble name 3 "Dave"         # hello Dave
scribble render                # put Dave's name in every file he's in
```

That order is load bearing. Do it backwards and you just do it twice. If you
want to know why, it's [down there](#why-that-order). If you don't, fine, copy
the block and go.

## The buttons nobody understands

### Enroll voice

Teaches it a voice you already know, from clips of that person and nobody else.

Everything else in this program infers who people are. This is the one place you
get to just tell it. Pick or name a person, drop in some clips, and it reports
what it found in each one before it commits anything.

Two numbers come back per clip. **Speech** is what was left after the silence
got thrown out, so a thirty-second clip with twenty-five seconds of room tone
reports five seconds and gets rejected. **One voice** is how much the clip agrees
with itself: high means one person throughout, low means a second voice, a music
bed, or two recordings spliced together. A low number there is almost always
somebody else talking, and a clip with two people in it produces a confident
profile of nobody, which is worse than no profile at all.

Reach it from the toolbar for a new person, or right-click somebody in the
speakers panel to attach clips to them.

Two or three short clips from different recordings beat one long clip from one.
The profile keeps them separately and matches against the closest, so a voice
enrolled on a phone and in a room recognises both. One clip averaged from both
recognises neither.

**This is the fix for nine speakers in a three-person recording.** It is not a
tuning knob, it is a different kind of evidence, and it is the only thing that
breaks the tie described [below](#one-person-split-into-six-ids).

### Start

Transcribes the queue. Text shows up as Whisper decodes, with no speaker
attached. Then diarization runs, because it needs the whole file before it can
tell you anything, and the speaker colours snap in afterwards.

Yes, the text appears before the names. No, that's not a bug. The alternative is
you watch a blank rectangle for four minutes while a model counts people. I made
a choice.

### Recluster

Regroups every voiceprint in the corpus, all at once.

Here's the thing it fixes. While a batch runs, each file's speakers get matched
against whatever's already in the store. Whoever showed up first defines what
that voice "is", and everyone after gets measured against that. Same 200 files
in a different order, different groupings. Not wrong. Just arbitrary. And
arbitrary is a real bad property for something you might have to defend to
another human being.

Recluster throws out arrival order and clusters everything simultaneously. Names
you already typed survive, because I'm not a monster.

**Run it once, after a batch.** Not after every file. It is not a progress bar.
It is not a refresh button. It's a reconciliation and it means something.

### Review duplicates

Shows you pairs of speakers that are close, but not close enough that I was
willing to merge them behind your back.

Nine times out of ten it's one person on two different devices. Somebody on a
phone and the same somebody on a room mic produce genuinely different
voiceprints, and there is no threshold that fixes this, because the threshold
low enough to merge those two is also low enough to merge two different people
who are both muffled. You cannot have both. Nobody can. Anyone selling you a
number here is lying.

So the software gives up gracefully and hands you a shortlist. Merge or dismiss.
Dismissals stick. You will not see that pair again, because being asked the same
question forty times is how features get ignored.

Pairs that appear in the same file never show up here. Diarization already
established those are two different humans, and no similarity score overrules
"they were talking to each other".

### Re-render

Rewrites the transcript files from the database. No GPU, no transcription, about
two seconds.

Speaker names are not baked into your SRTs. They get resolved when the file is
written. Rename somebody, hit this, done. That's the whole feature, and it's the
only reason renaming isn't a four hour reprocess.

### Clear database

Tools > Clear database. Deletes every file, transcript segment and speaker.

It tells you exactly what it's about to destroy, and writes a timestamped
backup next to the database first, so the panic afterwards is survivable. The
transcript files already on disk are untouched, but the identities behind them
are gone, so re-rendering after a clear gives you unattributed text.

`scribble clear` does the same thing from a terminal. `--yes` skips the prompt.

Use it when a bad batch has polluted the speaker store, which is a thing that
happens. Forty hallucinated segments will happily mint six speakers who do not
exist, and those then sit there matching against real people forever.

## Automatic reconciliation

`recluster_after_batch` is on by default, and it is the setting that stops you
having to remember step 2.

When a batch finishes, it reclusters, and if that moved anything it re-renders
the affected transcripts. Both halves matter: reclustering alone would leave
every file on disk describing identities that have since changed.

Turn it off in Settings if you'd rather drive it yourself, or if your corpus is
big enough that you'd rather choose when the reconciliation pass runs.

## Why that order

1. **Recluster** while everyone's still anonymous
2. **Review duplicates**, merge the real ones
3. **Name** the people you recognise
4. **Re-render** to push names into files

Recluster can move a file's speaker to a different identity. The moment it does,
every transcript sitting on disk is stale. Re-render is what fixes that.

Name everyone first and then recluster? Nothing breaks. Names follow their
clusters. You'll just re-render twice and feel slightly stupid. Ask me how I
know.

## Where your stuff went

| What | Where | Why |
|---|---|---|
| Transcripts | `Documents\Scribble` | somewhere findable |
| Speaker database | `%LOCALAPPDATA%\Scribble\scribble.db` | not Program Files, which is read-only, which I learned the hard way |
| Models | `%LOCALAPPDATA%\Scribble\models` | 4 GB, survives reinstall |
| cuDNN if you said yes | `%LOCALAPPDATA%\Scribble\runtime` | same |
| Scratch audio | `%LOCALAPPDATA%\Scribble\work` | deleted when a file finishes |

Override with `--out`, `--db`, or `scribble.toml`.

**Back up `scribble.db`.** I'm going to say this once and then it's on you.

It is not a cache. It holds every transcript, every voiceprint, and every name
you typed. The SRT files are just output. This is the actual work. Delete it and
your transcripts still exist, but the fact that speaker 4 on Tuesday is speaker 9
on Friday is gone forever, along with every name, and there is no rebuilding it
short of running the entire corpus again and redoing all the naming by hand.

## From a terminal

```
scribble run <path>...        transcribe files and folders
scribble speakers             list everyone in the corpus
scribble name <id> <name>     name one
scribble merge <from> <into>  fold one into another
scribble enroll <who> <clip>... teach a known voice from reference audio
scribble enrollments [id]     who is enrolled, and from what
scribble unenroll <id> [clip] drop reference clips
scribble dupes                pairs that might be the same person
scribble dismiss <id> <id>    stop asking me about this pair
scribble clear                delete everything, after backing it up
scribble recluster            regroup everything, keep names
scribble render               rewrite transcripts from the database
scribble models               what's downloadable
scribble gpu [install]        GPU for isolation and diarization
```

Flags worth knowing:

| Flag | What |
|---|---|
| `--model <name>` | `large-v3` default. `large-v3-turbo` if you're impatient. |
| `--formats srt,md,json` | srt, vtt, md, json, txt, tsv. Take your pick. |
| `--language en` | Skip detection. Do this if you know. It's faster and it's righter. |
| `--isolate always` | Nuke the music and background first |
| `--backend parakeet` | Faster on English. Needs the GPU runtime to be worth anything. |
| `--overwrite` | Redo files already marked done |
| `--threshold 0.7` | Fussier about matching speakers |
| `--min-speech 8` | Seconds a voice must speak to earn an identity. 0 names everyone. |
| `--range 12-30` | Enrol only those seconds of the clip |
| `--no-diarize` | Just words. No speakers. No opinions. |

`run` walks folders recursively and skips what it already finished, so pointing
it at the same folder twice is free. It only redoes a file if the size or
timestamp changed, or you asked with `--overwrite`.

`enroll` takes a name or an id. A name that already exists joins that person, a
name that does not creates them, and an id is always exact:

```powershell
scribble enroll "Will" clips\will-phone.wav clips\will-room.wav
scribble enroll 12 --range 90-140 meetings\standup.m4a
scribble enrollments
```

Enrolling does not touch transcripts already on disk. It changes how files are
matched from that point on, so re-run the files you want it applied to.

## When it looks wrong

### One person split into six IDs

Or nine speakers out of a three-person recording, which is the same failure
being loud about it.

Diarization over-split the file, and then a rule that is right almost all the
time made it permanent. Two speakers inside one file are never allowed to be the
same identity, because diarization already said they were different people. So
when it splits one person into four clusters, one of them gets the right
identity and the other three are *forced* to invent new ones. Reclustering will
not save you: it enforces the same rule.

Two things fix it, and they fix different halves.

**Raise `min_speaker_speech`.** It is 8 seconds by default: a voice with less
speech than that in a file does not get to become a speaker. Their words are
still transcribed, they just carry the file-local label instead of an identity.
This kills the blips, the crosstalk and the off-mic interjections, which is most
of the count. It does not merge anybody, and a brief speaker can still join a
person who already exists, so a real speaker's short tail is not lost.

**Enrol the people who actually recur.** That handles the other half: the real
speaker diarization split five ways. A reference clip outranks a per-file
clustering threshold, so those extra clusters collapse back into whoever they
came from instead of minting junk. Nothing else fixes that, because all five are
in one file and the rules otherwise forbid them from sharing an identity.

Beyond those: set `num_speakers` if you genuinely know the count, which bypasses
thresholding entirely and beats any tuning. Raise `diar_cluster_threshold` above
0.5 so the file splits less eagerly. Then merge what's left by hand.

**Two people welded into one ID.** Raise `match_threshold`. To undo the damage,
speakers panel, right click, split that file's speaker out into a fresh
identity.

**The transcript is fluent, confident, and completely made up.** Same sentence
forty times, possibly in Norwegian, definitely not anything anybody said.

That's Whisper hallucinating on audio with no speech in it. It is a language
model with an audio encoder taped to the front, and when you feed it four
minutes of room tone it does what language models do: it makes something up,
grammatically perfect, with total confidence. VAD is on by default precisely to
stop this. If you're seeing it, `vad_filter` got turned off or the VAD model
didn't download.

**Everything failed with a CUDA error.** `scribble gpu` will tell you what's
missing. Or set `onnx_accel = "cpu"` and move on with your life. On a machine
with real core count you'll barely notice.

**Every file split into `[track 1/2]`.** You turned on `split_channels`. That is
for recordings where each channel is a genuinely separate microphone on a
genuinely separate person. Stereo is not that. You just made two half
transcripts of one conversation and paid double for the privilege. Turn it off.

**Speaker names are wrong in a file you already fixed.** You renamed and didn't
re-render. See above. At length.

## Making it better

In descending order of how much they actually matter:

1. **`min_speaker_speech`.** The default 8 seconds is the difference between a
   speaker list you can read and one you scroll. Lower it only if a one-sentence
   contribution genuinely needs a name of its own.
2. **`hotwords`.** Names, places, jargon, that acronym your industry won't shut
   up about. Whisper has no hotword parameter so these ride in on the initial
   prompt. Highest return per second of effort in the entire program, and it
   lands on exactly the words anyone reads a transcript to find.
3. **`isolate = "auto"`.** Strips music and background. Massive on broadcast and
   field recordings. Auto checks the noise floor first so it doesn't run on
   clean speech and make it slightly worse for no reason.
4. **Enrol the regulars.** If the same five people are in four hundred files,
   twenty seconds of reference audio each is the difference between five
   identities and ninety. It also stops those five drifting, because an enrolled
   profile is an anchor rather than a running average.
5. **Don't turn on `split_channels`** unless you genuinely recorded each person
   on their own mic. If you did, congratulations, that's free perfect
   diarization and you should absolutely enable it.
6. **`large-v3`**, unless throughput matters more, then `large-v3-turbo` gets
   you most of the way at several times the speed.
7. **Leave `condition_on_previous_text` off.** It's off. Leave it. Turning it on
   lets one hallucinated loop poison the rest of a long recording in exchange
   for marginally better consistency. Bad trade.

## Why is this slow

Transcription is GPU work and it's fine.

Diarization is a small model run over hundreds of tiny windows, so it's bound by
launch overhead, not compute. On a machine with a lot of CPU cores it is
genuinely faster on the CPU. `auto` figures this out from your actual hardware
instead of assuming a GPU always wins, because that assumption is wrong on
exactly the machines that can afford a nice GPU. Irony noted.

Isolation is one big model over the whole file. GPU every time, no contest.

First run downloads models. `large-v3` is 3 GB by itself. That's once, not per
batch, and they live outside the install directory so reinstalling doesn't cost
you the download again.
