<div align="center">

<img src="assets/scribeeveryone.ico" width="96" alt="ScribeEveryone">

# ScribeEveryone

**Transcribes a pile of audio and video, and gives every voice in it one identity that holds across the whole corpus.**

[![license](https://img.shields.io/badge/license-GPLv3-d6262a?style=flat-square)](LICENSE)
[![platform](https://img.shields.io/badge/platform-Windows%2011-d6262a?style=flat-square)](#requirements)

</div>

---

Batch transcription with diarization, running entirely offline. No Python, no
API keys, no upload.

The part that is not standard: speakers are resolved against a corpus-wide
store rather than per file. Whisper and any diarizer will hand you
`SPEAKER_00` and `SPEAKER_01` inside one recording and start over from zero on
the next. Here the first appearance of a voice mints a durable identity, every
later file matches into it, and naming that identity once relabels every
transcript it appears in. There is no enrollment step: the corpus enrolls
itself.

## What it does

- Reads audio and video containers, anything ffmpeg can demux
- Transcribes with whisper.cpp on the GPU, streaming each utterance as it is decoded
- Diarizes with a pyannote segmentation model through sherpa-onnx
- Extracts a voiceprint per speaker per file and matches it against the store
- Writes SRT, WebVTT, Markdown, JSON, plain text and TSV
- Keeps everything in one SQLite file, so renaming a speaker and re-rendering costs no inference
- Resumes: files already done are skipped unless their size or timestamp changed

## Requirements

- Windows 11, x64
- ffmpeg and ffprobe on PATH, or beside the executable
- An NVIDIA GPU for useful throughput. CPU works and is slow.
- Roughly 4 GB of disk for models, downloaded on first run

`curl` and `tar` are used to fetch models and both ship with Windows 11, so
there is nothing else to install.

Whisper reaches the GPU through cuBLAS and needs nothing extra. Diarization,
voiceprints, isolation and Parakeet run on onnxruntime, whose CUDA provider
additionally needs **cuDNN 9**. NVIDIA's licence does not permit redistributing
it, so ScribeEveryone downloads it from NVIDIA on request rather than shipping
it:

```powershell
scribe gpu           # what is missing and how big
scribe gpu install
```

Roughly 1.2 GB, kept in `%LOCALAPPDATA%\ScribeEveryone\runtime`. Until then
those stages run on CPU, which is announced at startup rather than failing.
Whisper transcription is unaffected either way.

## Building

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Needs Visual Studio 2022, the CUDA toolkit, and Qt 6 for the GUI. Pass
`-DSCRIBE_BUILD_GUI=OFF` to build only the command line tool, or
`-DSCRIBE_CUDA=OFF` for a CPU-only build. See [docs/building.md](docs/building.md).

`scripts/package.ps1` produces a signed installer.

## Use

```powershell
scribe run D:\recordings
scribe speakers
scribe name 12 "Will"
scribe render
```

`run` walks folders recursively. `speakers` lists every identity found with its
file count and total speech time. `name` labels one, and `render` rewrites the
transcripts with the new name without transcribing anything again.

Two more worth knowing:

```powershell
scribe dupes
scribe recluster
```

`dupes` reports identity pairs whose voiceprints sit in the uncertain band,
close but under the match threshold. The usual cause is one person recorded on
different equipment, which no threshold setting fixes, so the pairs are handed
to you rather than guessed at. Merge with `scribe merge <from> <into>`.

`recluster` regroups every stored voiceprint in one pass. Incremental matching
assigns identities in arrival order, so the same corpus ingested in a different
order groups differently. Reclustering removes that dependence and keeps the
names you have already entered. Run it after a large batch.

## Configuration

`scribe.toml` in the working directory, or `%LOCALAPPDATA%\ScribeEveryone\scribe.toml`.
Unknown keys are rejected rather than ignored.

```toml
model             = "large-v3"
formats           = ["srt", "md", "json"]
hotwords          = ["Kubernetes", "Grafana", "Thorvaldsen"]
match_threshold   = 0.65
review_threshold  = 0.50
diarize           = true
isolate           = "auto"
```

`hotwords` is the cheapest accuracy gain available. Whisper has no hotword
parameter, so the list rides in on the initial prompt, and it is what fixes
proper nouns, which are usually the words a transcript is read for.

## Accuracy notes

Ranked by how much they actually move word error rate:

1. **Do not downmix multi-track recordings.** Separate microphone tracks are
   free perfect diarization and better transcription. ScribeEveryone detects
   them and transcribes each track separately, unless the channels turn out to
   be one mono source duplicated.
2. **`isolate = "auto"`.** Strips music and background with a UVR model before
   transcription. The largest single gain on broadcast, field and music-bedded
   audio, and a small loss on clean speech, so `auto` measures the noise floor
   and only runs it where it will help. Isolation happens before the 16 kHz
   downmix, because the model works at its own rate on stereo.
3. **`hotwords`**, as above.
4. **`condition_on_previous_text = false`**, the default. Carrying context
   between windows means one hallucinated loop propagates through the rest of a
   long recording. The trade is a little less cross-window consistency.
5. **`large-v3`** is the right default. `large-v3-turbo` is several times
   faster at close to the same accuracy if throughput matters more.

### Parakeet

```powershell
scribe run D:\recordings --backend parakeet
```

NVIDIA Parakeet TDT 0.6B v3 beats Whisper on the Open ASR Leaderboard for
English and European languages and is considerably faster, given cuDNN. It is a
transducer rather than an encoder-decoder, so it has no windowing of its own:
audio is cut into utterances with silero VAD first, and each chunk is padded
before decoding because a VAD trims to confident speech and would otherwise
clip the first and last word of every segment.

Whisper remains the default. It is more robust on poor audio and covers far
more languages.

### Where the GPU actually helps

`onnx_accel` and `isolate_accel` both default to `auto`, which decides per
stage from the hardware present rather than using the GPU for everything.

Vocal isolation is one large model over the whole recording and the GPU wins on
any machine. Segmentation and voiceprints are small models run over many short
windows, so they are launch-overhead bound and the answer depends on how much
CPU you have.

Measured on a Ryzen 9 7950X with an RTX 4090, over 5.5 minutes of audio:

| Stage | GPU | CPU |
|---|---|---|
| Vocal isolation | 38s | 113s |
| Diarization and voiceprints | 153s | 112s |
| Parakeet transcription | no measurable difference | |

That CPU is a 16-core part, which is why it wins the middle row. On a four or
eight core machine the GPU wins it comfortably, so `auto` sends small-model work
to the GPU there and keeps it on the CPU on a machine like the one above. Set
`onnx_accel = "cuda"` or `"cpu"` to override.

## Better diarization

The bundled segmentation model is the ONNX export of pyannote 3.0.
pyannote `community-1` counts speakers noticeably better, which is what
corpus-wide identity depends on, but it ships only as PyTorch behind a gated
Hugging Face repo and cannot be redistributed here.

`tools/export_community1_segmentation.py` exports it locally. The result drops
straight in:

```toml
segmentation_model = "C:/models/community-1-segmentation.onnx"
```

## Licence

GPLv3. See [LICENSE](LICENSE).
