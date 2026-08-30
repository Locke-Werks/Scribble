<div align="center">

<img src="assets/scribeeveryone.ico" width="96" alt="ScribeEveryone">

# ScribeEveryone

**Transcribes a pile of audio and video, and gives every voice in it one identity that holds across the whole corpus.**

[![license](https://img.shields.io/badge/license-GPLv3-d6262a?style=flat-square)](LICENSE)
[![platform](https://img.shields.io/badge/platform-Windows%2011-d6262a?style=flat-square)](#requirements)

</div>

---

Batch transcription with diarization. Entirely offline. No API keys, no upload,
no monthly bill, no terms of service that quietly changed while you were asleep.

The part that isn't standard: speakers get resolved against a corpus-wide store
instead of per file.

Every other tool hands you `SPEAKER_00` and `SPEAKER_01` inside one recording,
then cheerfully starts over at zero on the next one. Which is fine for one file
and completely useless for four hundred, because the question you actually have
is never "how many people are in this clip". It's "where else does this person
show up".

So: first time a voice appears, it gets a durable identity. Every later file
matches into it. Name it once and every transcript that person appears in says
their name. No enrollment step, no sitting there recording reference clips of
your coworkers like a weirdo. The corpus enrols itself.

**Start here: [docs/usage.md](docs/usage.md).** It explains what the four
intimidating toolbar buttons do, in the order you'll need them. It exists
because the person who designed this app couldn't remember what Recluster did.

## What it does

- Reads anything ffmpeg can demux, which is everything
- Transcribes with whisper.cpp on the GPU, streaming text as it decodes
- Diarizes with a pyannote segmentation model through sherpa-onnx
- Pulls a voiceprint per speaker per file and matches it against the store
- Writes SRT, WebVTT, Markdown, JSON, plain text, TSV
- Keeps it all in one SQLite file, so renaming somebody costs zero inference
- Resumes. Files already done get skipped unless they changed on disk.
- Strips music and background when that helps, and skips it when it doesn't

## Requirements

- Windows 11, x64
- An NVIDIA GPU if you value your time. CPU works. CPU is slow.
- ~4 GB of disk for models, fetched on first run
- ffmpeg and ffprobe, which the installer ships so you don't have to care

`curl` and `tar` do the downloading and unpacking and both ship with Windows, so
there's nothing else to install.

Whisper reaches the GPU through cuBLAS and needs nothing extra. Diarization,
voiceprints, isolation and Parakeet go through onnxruntime, which wants
**cuDNN 9**. NVIDIA's licence says nobody gets to redistribute that, so instead
of shipping it, this thing fetches it from NVIDIA for you:

```powershell
scribe gpu           # what's missing, how big
scribe gpu install
```

About 1.2 GB. Lands in local app data. Asked for, not assumed. Until you do it,
those stages run on CPU and say so out loud. Transcription doesn't care either
way.

## Use

```powershell
scribe run D:\recordings
scribe speakers
scribe name 12 "Will"
scribe render
```

`run` walks folders recursively and skips what it already did. `speakers` lists
everybody with file count and total speech time. `name` labels one. `render`
rewrites the transcripts with the new name and transcribes nothing, because
names are resolved when a file gets written rather than baked in like a fossil.

Then the two that need a sentence:

```powershell
scribe dupes
scribe recluster
```

`dupes` reports identity pairs that are close but not close enough for me to
merge them without asking. Usually one person on two different devices, which no
threshold anywhere fixes, so you get a shortlist instead of a guess. Confirm
with `scribe merge <from> <into>`.

`recluster` regroups every voiceprint in one pass. Incremental matching assigns
identities in arrival order, so the same corpus ingested in a different order
groups differently. This removes that, and keeps every name you typed. Run it
once after a big batch. Once.

There's a GUI with the same functions and a live transcript view, if you'd
rather click.

## Configuration

`scribe.toml` next to where you run it, or in `%LOCALAPPDATA%\ScribeEveryone`.

Unknown keys are rejected, not ignored. A typo stops the program with a message
instead of silently doing something else for an hour and making you wonder why
the output looks weird.

```toml
model             = "large-v3"
formats           = ["srt", "md", "json"]
hotwords          = ["Kubernetes", "Grafana", "Thorvaldsen"]
isolate           = "auto"
match_threshold   = 0.65
review_threshold  = 0.50
```

`hotwords` is the single highest return per second of effort in here. Whisper
has no hotword parameter so the list rides in on the initial prompt, and it
fixes proper nouns, which are usually the only words anybody actually opens a
transcript to find.

[scribe.example.toml](scribe.example.toml) has everything, with commentary.

## Accuracy

Ranked by what actually moves word error rate, not by what sounds impressive:

1. **`hotwords`**, as above.
2. **`isolate = "auto"`.** Strips music and background with a UVR model before
   transcription. Biggest single win on broadcast, field recordings, anything
   with a music bed. Slightly harmful on clean speech, which is why `auto`
   measures the noise floor first instead of running it on everything and
   calling that a feature.
3. **Multi-track recordings, if you genuinely have them.** Separate mics on
   separate people is free perfect diarization. Turn `split_channels` on for
   those. Leave it off otherwise, because stereo is not multi-track and
   splitting it gets you two half transcripts of one conversation.
4. **`condition_on_previous_text = false`**, the default. Carrying context
   between windows lets one hallucinated loop propagate through the rest of a
   long recording. Slightly less consistency, dramatically less fiction.
5. **`large-v3`.** `large-v3-turbo` is most of the accuracy at several times the
   speed if you're processing a lot.

### Parakeet

```powershell
scribe run D:\recordings --backend parakeet
```

NVIDIA Parakeet TDT 0.6B v3 beats Whisper on the Open ASR Leaderboard for
English and European languages. It's a transducer, so it has no windowing of its
own: audio gets cut into utterances with silero VAD first, and every chunk is
padded before decoding, because a VAD trims to confident speech and would
otherwise eat the first and last word of every single segment. Found that out
the fun way.

Whisper stays the default. More forgiving of bad audio, far more languages.

### Where the GPU actually helps

`onnx_accel` and `isolate_accel` both default to `auto`, which decides per stage
from your hardware instead of assuming a GPU is always the answer.

Ryzen 9 7950X, RTX 4090, 5.5 minutes of audio:

| Stage | GPU | CPU |
|---|---|---|
| Vocal isolation | 38s | 113s |
| Diarization and voiceprints | 153s | 112s |
| Parakeet transcription | dead heat | |

Yes, the middle row says the CPU beat a 4090. That's a 16 core part, and those
models are small ones run over hundreds of short windows, so launch overhead
dominates and a big CPU eats it alive. On a four core laptop the GPU wins that
row comfortably. Which is why `auto` counts your cores rather than treating one
guy's benchmark as scripture. Override with `onnx_accel = "cuda"` or `"cpu"`.

## Better diarization, if you're feeling ambitious

The bundled segmentation model is the ONNX export of pyannote 3.0. pyannote
`community-1` counts speakers noticeably better, which matters a lot here since
speaker counting is the foundation the whole identity system sits on. It ships
only as PyTorch behind a gated Hugging Face repo and cannot be redistributed.

`tools/export_community1_segmentation.py` exports it locally. Drops straight in:

```toml
segmentation_model = "C:/models/community-1-segmentation.onnx"
```

## Documentation

- **[docs/usage.md](docs/usage.md)** what the buttons do, in what order
- [docs/speakers.md](docs/speakers.md) how one voice becomes one identity
- [docs/building.md](docs/building.md) building, packaging, and every trap

## Licence

GPLv3. See [LICENSE](LICENSE).
