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

## Building

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Needs Visual Studio 2022, the CUDA toolkit, and Qt 6 for the GUI. Pass
`-DSCRIBE_BUILD_GUI=OFF` to build only the command line tool, or
`-DSCRIBE_CUDA=OFF` for a CPU-only build. See [docs/building.md](docs/building.md).

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
2. **`hotwords`**, as above.
3. **`condition_on_previous_text = false`**, the default. Carrying context
   between windows means one hallucinated loop propagates through the rest of a
   long recording. The trade is a little less cross-window consistency.
4. **`large-v3`** is the right default. `large-v3-turbo` is several times
   faster at close to the same accuracy if throughput matters more.

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
