# Using ScribeEveryone

You pointed it at a folder. It chewed through 200 files. Now there are 47 people
called `SPEAKER_0031` and you are staring at a toolbar with four buttons whose
names made sense to me at the time.

Cool. Let's fix that.

## The whole thing in six commands

```powershell
scribe run D:\recordings     # do the work
scribe recluster             # ONCE. at the end. we'll get to it.
scribe dupes                 # "are these two the same guy"
scribe merge 7 3             # they were
scribe name 3 "Dave"         # hello Dave
scribe render                # put Dave's name in every file he's in
```

That order is load bearing. Do it backwards and you just do it twice. If you
want to know why, it's [down there](#why-that-order). If you don't, fine, copy
the block and go.

## The four buttons nobody understands

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
| Transcripts | `Documents\ScribeEveryone` | somewhere findable |
| Speaker database | `%LOCALAPPDATA%\ScribeEveryone\scribe.db` | not Program Files, which is read-only, which I learned the hard way |
| Models | `%LOCALAPPDATA%\ScribeEveryone\models` | 4 GB, survives reinstall |
| cuDNN if you said yes | `%LOCALAPPDATA%\ScribeEveryone\runtime` | same |
| Scratch audio | `%LOCALAPPDATA%\ScribeEveryone\work` | deleted when a file finishes |

Override with `--out`, `--db`, or `scribe.toml`.

**Back up `scribe.db`.** I'm going to say this once and then it's on you.

It is not a cache. It holds every transcript, every voiceprint, and every name
you typed. The SRT files are just output. This is the actual work. Delete it and
your transcripts still exist, but the fact that speaker 4 on Tuesday is speaker 9
on Friday is gone forever, along with every name, and there is no rebuilding it
short of running the entire corpus again and redoing all the naming by hand.

## From a terminal

```
scribe run <path>...        transcribe files and folders
scribe speakers             list everyone in the corpus
scribe name <id> <name>     name one
scribe merge <from> <into>  fold one into another
scribe dupes                pairs that might be the same person
scribe dismiss <id> <id>    stop asking me about this pair
scribe recluster            regroup everything, keep names
scribe render               rewrite transcripts from the database
scribe models               what's downloadable
scribe gpu [install]        GPU for isolation and diarization
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
| `--no-diarize` | Just words. No speakers. No opinions. |

`run` walks folders recursively and skips what it already finished, so pointing
it at the same folder twice is free. It only redoes a file if the size or
timestamp changed, or you asked with `--overwrite`.

## When it looks wrong

**One person split into six IDs.** Merge them, or drop `match_threshold` a bit.
Different mics, different rooms, different days. This is the normal failure and
it's not going away.

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

**Everything failed with a CUDA error.** `scribe gpu` will tell you what's
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

1. **`hotwords`.** Names, places, jargon, that acronym your industry won't shut
   up about. Whisper has no hotword parameter so these ride in on the initial
   prompt. Highest return per second of effort in the entire program, and it
   lands on exactly the words anyone reads a transcript to find.
2. **`isolate = "auto"`.** Strips music and background. Massive on broadcast and
   field recordings. Auto checks the noise floor first so it doesn't run on
   clean speech and make it slightly worse for no reason.
3. **Don't turn on `split_channels`** unless you genuinely recorded each person
   on their own mic. If you did, congratulations, that's free perfect
   diarization and you should absolutely enable it.
4. **`large-v3`**, unless throughput matters more, then `large-v3-turbo` gets
   you most of the way at several times the speed.
5. **Leave `condition_on_previous_text` off.** It's off. Leave it. Turning it on
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
