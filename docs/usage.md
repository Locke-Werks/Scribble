# Using ScribeEveryone

You pointed it at a folder, it made some files, and now there are twelve people
called `SPEAKER_0007` and you have no idea which button fixes that. This page is
for that exact moment.

## The thirty second version

```powershell
scribe run D:\recordings     # transcribe everything
scribe recluster             # regroup the voices, once, at the end
scribe dupes                 # "are these two the same person?"
scribe merge 7 3             # yes they were
scribe name 3 "Dave"         # Dave, finally
scribe render                # push Dave's name into every transcript
```

That order is not decorative. Doing it backwards means doing it twice. Skip to
[the order matters](#the-order-matters-and-here-is-why) if you want to know why,
or just trust it and get on with your life.

## What the four scary buttons do

The toolbar has four things that are not obvious. In the order you will
actually need them:

### Start

Transcribes everything in the queue. Whisper streams text as it decodes, so
lines appear immediately with no speaker attached. Diarization needs the whole
file, so it runs afterwards and the speaker colours snap in a moment later.

That gap is deliberate. The alternative is staring at a blank pane for four
minutes while a model decides how many people are in your audio.

### Recluster

Regroups every voiceprint in the corpus in one pass.

Here is the problem it solves. During a batch, each file's speakers get matched
against the store as they arrive. Whoever shows up first defines what that voice
"looks like", and everyone after is measured against that. Feed the same fifty
files in a different order and you get different groupings. Not wrong exactly,
just arbitrary, and arbitrary is not what you want out of an evidence pipeline.

Recluster throws away the arrival order and clusters everything at once. Names
you have already typed survive: each group reclaims the identity its members
carried, preferring one that has a name on it.

Run it **once, after a batch**. Not after every file. It is not a progress bar,
it is a reconciliation.

### Review duplicates

Lists pairs of speakers whose voiceprints are close but not close enough to have
been merged automatically. You get to decide.

The usual cause is the same person recorded on different equipment. A phone call
and a room mic produce genuinely different voiceprints for the same human, and
no threshold anywhere fixes that, because the alternative is merging your two
quietest speakers who happen to both be muffled.

So the software stops guessing and hands you a shortlist. Merge or dismiss.
Dismissals stick, so a pair you rejected stays rejected.

Pairs that appear together in the same file are never offered, because
diarization already established they are two different people and no amount of
similarity changes that.

### Re-render

Rewrites the transcript files from the database. No transcription, no GPU, a
couple of seconds.

Speaker names are not baked into your SRTs. They are resolved when the file is
written. So: rename someone, hit Re-render, and every transcript they appear in
now says their name. That is the entire feature and it is the reason renaming is
cheap instead of a four hour reprocess.

## The order matters, and here is why

1. **Recluster** while everyone is still anonymous
2. **Review duplicates**, merge the real ones
3. **Name** the people you recognise
4. **Re-render** to push the names into the files

Recluster can move a file's speaker to a different identity. Any transcript
already sitting on disk is then stale. Re-render is what fixes that.

If you name everyone first and then recluster, nothing breaks, the names follow
their clusters. You will just have to re-render again. Do it in the order above
and you re-render once.

## Where your stuff goes

| What | Where | Why there |
|---|---|---|
| Transcripts | `Documents\ScribeEveryone` | Somewhere you can actually find |
| Speaker database | `%LOCALAPPDATA%\ScribeEveryone\scribe.db` | Not Program Files, which is read-only |
| Models | `%LOCALAPPDATA%\ScribeEveryone\models` | Several GB, survives reinstall |
| cuDNN, if you said yes | `%LOCALAPPDATA%\ScribeEveryone\runtime` | Same reason |
| Scratch audio | `%LOCALAPPDATA%\ScribeEveryone\work` | Deleted when a file finishes |

Override any of them in `scribe.toml` or with `--out` and `--db`.

**The database is the corpus.** It holds every transcript, every voiceprint and
every name you have typed. The SRT files are output; this is the actual work.
Delete it and you have not deleted your transcripts, you have deleted the fact
that speaker 4 in Tuesday's meeting is speaker 9 in Friday's, plus every name.
Back it up before you do anything brave.

## Doing it from a terminal

```
scribe run <path>...        transcribe files and folders
scribe speakers             list every speaker in the corpus
scribe name <id> <name>     name one
scribe merge <from> <into>  fold one into another
scribe dupes                pairs that might be the same person
scribe dismiss <id> <id>    stop offering a pair
scribe recluster            regroup everything, keeping names
scribe render               rewrite transcripts from the database
scribe models               what is downloadable
scribe gpu [install]        GPU for isolation and diarization
```

Useful flags:

| Flag | Does |
|---|---|
| `--model <name>` | `large-v3` by default, `large-v3-turbo` if you are impatient |
| `--formats srt,md,json` | Pick your poison from srt, vtt, md, json, txt, tsv |
| `--language en` | Skip detection. Worth it if you know. |
| `--isolate always` | Strip music and background first |
| `--backend parakeet` | English and European, faster, needs the GPU runtime to be worth it |
| `--overwrite` | Redo files already marked done |
| `--threshold 0.7` | Fussier about matching speakers |
| `--no-diarize` | Just the words, no speakers |

`run` walks folders recursively and skips files it already finished, so pointing
it at the same folder twice is safe and cheap. It only redoes a file if its size
or timestamp changed, or you passed `--overwrite`.

## When it looks wrong

**One person split across several IDs.** Lower `match_threshold` a bit, or just
merge them by hand. Different recording equipment is the usual culprit and no
setting fixes that cleanly.

**Two people merged into one ID.** Raise `match_threshold`. To fix the damage,
open the speakers panel, right click, and split that file's speaker back out
into a fresh identity.

**The transcript is confident, fluent, and completely invented.** It repeated
the same sentence forty times, probably in a language you do not speak. That is
Whisper hallucinating on audio with no speech in it, and it is a known party
trick of the model, not a bug in your file. Voice activity detection is on by
default specifically to prevent this. If you see it, check `vad_filter` is not
turned off and that the VAD model actually downloaded.

**Everything failed with a CUDA error.** Run `scribe gpu` to see what it thinks
is missing. Or set `onnx_accel = "cpu"` and get on with your day; on a machine
with a lot of cores you will barely notice.

**Every file split into `[track 1/2]`.** You turned on `split_channels`. That is
for recordings where each channel is a genuinely separate microphone on a
separate person. An ordinary stereo mix is not that, and splitting one gives you
two half transcripts of the same conversation. Turn it back off.

**Speakers are wrong in a file you already fixed.** You renamed but did not
re-render. See above, at length.

## Making it more accurate

Roughly in order of how much they actually move the needle:

1. **`hotwords`.** A list of the proper nouns, jargon and names in your audio.
   Whisper has no hotword parameter so they ride in on the initial prompt. This
   is the cheapest real gain available and it lands on exactly the words a
   transcript gets read for. Names, places, product names, that one acronym.
2. **`isolate = "auto"`.** Strips music and background before transcription.
   Enormous on broadcast, field recordings and anything with a music bed. Auto
   measures the noise floor first so it does not run on clean speech, where it
   would cost you a little.
3. **Do not enable `split_channels`** unless you genuinely recorded each person
   on their own microphone. If you did, it is free perfect diarization and you
   should absolutely turn it on.
4. **`large-v3`** unless throughput matters more than accuracy, in which case
   `large-v3-turbo` is most of the way there and several times faster.
5. **Leave `condition_on_previous_text` off.** It is off by default. Turning it
   on lets one hallucinated loop propagate through the rest of a long recording,
   which is a bad trade for slightly better cross-window consistency.

## Things that are slow and why

Transcription is GPU work and fast. The other stages are not always.

Diarization is a small model run over hundreds of short windows, so it is bound
by launch overhead rather than compute. On a machine with a lot of CPU cores it
is genuinely faster on the CPU. `auto` works this out from your hardware instead
of assuming a GPU is always better, because that assumption is wrong on exactly
the machines that can afford a good GPU.

Isolation is one big model over the whole recording and the GPU wins every time.

First run downloads models. `large-v3` alone is 3 GB. That is once, not per
batch, and they live outside the install directory so reinstalling does not cost
you the download again.
