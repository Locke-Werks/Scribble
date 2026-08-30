<div align="center">

<img src="assets/scribeeveryone.ico" width="96" alt="ScribeEveryone">

# ScribeEveryone

**Transcribes a pile of audio and video, and gives every voice in it one identity that holds across the whole corpus.**

[![license](https://img.shields.io/badge/license-GPLv3-d6262a?style=flat-square)](LICENSE)
[![platform](https://img.shields.io/badge/platform-Windows%2011-d6262a?style=flat-square)](#requirements)

</div>

---

Batch transcription with diarization, running entirely offline. No API keys, no
upload, no monthly bill, no terms of service that quietly changed last Tuesday.

The part that is not standard: speakers are resolved against a corpus-wide
store rather than per file.

Every other tool hands you `SPEAKER_00` and `SPEAKER_01` inside one recording
and then starts over from zero on the next one, which is useless the moment you
have more than one recording. Here the first appearance of a voice mints a
durable identity, every later file matches into it, and naming that identity
once relabels every transcript it appears in. There is no enrollment step. The
corpus enrols itself.

**New here? [docs/usage.md](docs/usage.md) is the page you want.** It explains
what those four intimidating toolbar buttons actually do, in the order you will
need them.

## What it does

- Reads anything ffmpeg can demux, which is everything
- Transcribes with whisper.cpp on the GPU, streaming text as it decodes
- Diarizes with a pyannote segmentation model through sherpa-onnx
- Extracts a voiceprint per speaker per file and matches it against the store
- Writes SRT, WebVTT, Markdown, JSON, plain text and TSV
- Keeps it all in one SQLite file, so renaming a speaker costs no inference
- Resumes: files already done are skipped unless they changed on disk
- Strips music and background first, when that will help and not when it will not

## Requirements

- Windows 11, x64
- An NVIDIA GPU if you value your time. CPU works. CPU is slow.
- About 4 GB of disk for models, fetched on first run
- ffmpeg and ffprobe, which the installer ships

`curl` and `tar` do the downloading and unpacking, and both come with Windows,
so there is nothing else to install.

Whisper reaches the GPU through cuBLAS and needs nothing extra. Diarization,
voiceprints, isolation and Parakeet go through onnxruntime, which wants
**cuDNN 9**. NVIDIA's licence does not let anyone redistribute that, so
ScribeEveryone fetches it from NVIDIA on your behalf instead of shipping it:

```powershell
scribe gpu           # what is missing and how big
scribe gpu install
```

About 1.2 GB, lands in your local app data, asked for rather than assumed.
Until then those stages run on CPU and say so. Transcription is unaffected.

## Use

```powershell
scribe run D:\recordings
scribe speakers
scribe name 12 "Will"
scribe render
```

`run` walks folders recursively and skips what it already finished. `speakers`
lists every identity with its file count and total speech time. `name` labels
one. `render` rewrites the transcripts with the new name, without transcribing
anything again, because names are resolved when a file is written rather than
baked in.

Then the two that need a sentence of explanation:

```powershell
scribe dupes
scribe recluster
```

`dupes` reports identity pairs that are close but not close enough to have been
merged automatically. The usual cause is one person recorded on two different
devices, which no threshold setting fixes, so the pairs get handed to you
instead of guessed at. `scribe merge <from> <into>` when it is a real match.

`recluster` regroups every stored voiceprint in one pass. Incremental matching
assigns identities in arrival order, so the same corpus ingested in a different
order groups differently. Reclustering removes that, and keeps every name you
have already typed. Run it once after a large batch.

There is a GUI too, with the same functions and a live transcript view, if you
would rather click things.

## Configuration

`scribe.toml` next to where you run it, or in `%LOCALAPPDATA%\ScribeEveryone`.
Unknown keys are rejected rather than ignored, because a silently dropped
setting produces a confusing afternoon instead of an error message.

```toml
model             = "large-v3"
formats           = ["srt", "md", "json"]
hotwords          = ["Kubernetes", "Grafana", "Thorvaldsen"]
isolate           = "auto"
match_threshold   = 0.65
review_threshold  = 0.50
```

`hotwords` is the highest return per second of effort in the entire program.
Whisper has no hotword parameter, so the list rides in on the initial prompt,
and it fixes proper nouns, which are usually the only words anybody actually
cares about getting right.

See [scribe.example.toml](scribe.example.toml) for everything, with commentary.

## Accuracy

Ranked by how much they move word error rate, not by how clever they sound:

1. **`hotwords`**, as above.
2. **`isolate = "auto"`.** Strips music and background with a UVR model before
   transcription. The largest single gain on broadcast, field and music-bedded
   audio, and a small loss on clean speech, which is why `auto` measures the
   noise floor and only runs it where it will help.
3. **Multi-track recordings, if you actually have them.** Separate microphones
   on separate people are free perfect diarization. Turn `split_channels` on for
   those and leave it off otherwise, because an ordinary stereo mix is not
   multi-track and splitting one gives you two half transcripts of the same
   conversation.
4. **`condition_on_previous_text = false`**, the default. Carrying context
   between windows means one hallucinated loop propagates through the rest of a
   long recording. Slightly less consistency, considerably less fiction.
5. **`large-v3`.** `large-v3-turbo` is most of the accuracy at several times the
   speed if you are processing a lot.

### Parakeet

```powershell
scribe run D:\recordings --backend parakeet
```

NVIDIA Parakeet TDT 0.6B v3 beats Whisper on the Open ASR Leaderboard for
English and European languages. It is a transducer, so it has no windowing of
its own: audio gets cut into utterances with silero VAD first, and each chunk is
padded before decoding, because a VAD trims to confident speech and would
otherwise clip the first and last word of every segment.

Whisper stays the default. It is more forgiving of bad audio and covers far more
languages.

### Where the GPU actually helps

`onnx_accel` and `isolate_accel` both default to `auto`, which decides per stage
from your hardware rather than assuming a GPU is always the answer.

Measured on a Ryzen 9 7950X with an RTX 4090, over 5.5 minutes of audio:

| Stage | GPU | CPU |
|---|---|---|
| Vocal isolation | 38s | 113s |
| Diarization and voiceprints | 153s | 112s |
| Parakeet transcription | no measurable difference | |

Yes, the middle row says the CPU won. That is a 16-core part, and those models
are small ones run over hundreds of short windows, so launch overhead dominates
and a big CPU eats it. On a four or eight core machine the GPU wins that row
comfortably, which is why `auto` counts your cores instead of taking my
benchmark as gospel. Override with `onnx_accel = "cuda"` or `"cpu"`.

## Better diarization, if you want to go there

The bundled segmentation model is the ONNX export of pyannote 3.0. pyannote
`community-1` counts speakers noticeably better, which matters because speaker
counting is what corpus-wide identity is built on, but it ships only as PyTorch
behind a gated Hugging Face repo and cannot be redistributed here.

`tools/export_community1_segmentation.py` exports it locally. The result drops
straight in:

```toml
segmentation_model = "C:/models/community-1-segmentation.onnx"
```

## Documentation

- **[docs/usage.md](docs/usage.md)** what the buttons do and the order to press them
- [docs/speakers.md](docs/speakers.md) how one voice becomes one identity
- [docs/building.md](docs/building.md) building, packaging, and the traps

## Licence

GPLv3. See [LICENSE](LICENSE).
