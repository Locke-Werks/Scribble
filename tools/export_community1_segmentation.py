"""Export the pyannote community-1 segmentation model to ONNX.

Scribble diarizes with sherpa-onnx, which loads a pyannote segmentation
model from ONNX. The bundled default is the 3.0 export. community-1 is better
at counting speakers, which is what corpus-wide identity depends on, but it
ships only as PyTorch behind a gated Hugging Face repo, so it cannot be
redistributed here and has to be exported locally.

This touches pyannote internals to reach the segmentation model inside the
pipeline. Those internals are not a stable API, so treat a failure here as
"the layout moved", not as a bug in Scribble. The 3.0 export keeps
working either way.

Usage:
    pip install "pyannote.audio>=4.0" onnx onnxruntime torch
    huggingface-cli login          # and accept the model conditions first
    python tools/export_community1_segmentation.py --out community-1-segmentation.onnx

Then point the config at it:
    segmentation_model = "C:/path/to/community-1-segmentation.onnx"

Check the licence terms on the model page before using the result for anything
beyond your own transcripts.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

PIPELINE_ID = "pyannote/speaker-diarization-community-1"


def find_segmentation_model(pipeline):
    """Locate the segmentation model inside an instantiated pipeline.

    Attribute names have moved between pyannote releases, so several are tried
    rather than pinning one and breaking on the next version.
    """
    candidates = ["_segmentation", "segmentation_model", "_segmentation_model", "segmentation"]
    for name in candidates:
        obj = getattr(pipeline, name, None)
        if obj is None:
            continue
        # Some versions wrap the model in an inference helper.
        model = getattr(obj, "model", obj)
        if hasattr(model, "specifications"):
            return model
    raise SystemExit(
        "cannot find the segmentation model on the pipeline. pyannote's internal "
        "layout has changed; inspect dir(pipeline) and update this script."
    )


def collect_metadata(model) -> dict[str, str]:
    spec = model.specifications
    sample_rate = int(model.audio.sample_rate)

    duration = float(spec.duration)
    window_size = int(round(duration * sample_rate))

    receptive_field = model.receptive_field
    receptive_field_size = int(round(float(receptive_field.duration) * sample_rate))
    receptive_field_shift = int(round(float(receptive_field.step) * sample_rate))

    powerset_max_classes = int(getattr(spec, "powerset_max_classes", 0) or 0)
    num_classes = len(spec.classes)

    # In powerset mode `classes` enumerates speaker combinations, so the real
    # speaker count comes from the powerset parameters rather than len(classes).
    num_speakers = int(getattr(spec, "num_powerset_classes", 0) or 0)
    if not num_speakers:
        num_speakers = len(getattr(spec, "speakers", []) or [])
    if not num_speakers:
        num_speakers = _infer_speakers(num_classes, powerset_max_classes)

    return {
        "sample_rate": str(sample_rate),
        "window_size": str(window_size),
        "receptive_field_size": str(receptive_field_size),
        "receptive_field_shift": str(receptive_field_shift),
        "num_speakers": str(num_speakers),
        "powerset_max_classes": str(powerset_max_classes),
        "num_classes": str(num_classes),
    }


def _infer_speakers(num_classes: int, max_classes: int) -> int:
    """Invert the powerset size formula to recover the speaker count."""
    from math import comb

    for speakers in range(1, 12):
        total = sum(comb(speakers, k) for k in range(max_classes + 1))
        if total == num_classes:
            return speakers
    raise SystemExit(
        f"cannot infer the speaker count from num_classes={num_classes} and "
        f"powerset_max_classes={max_classes}. Set it by hand."
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default="community-1-segmentation.onnx", type=Path)
    parser.add_argument("--token", default=None, help="Hugging Face token")
    parser.add_argument("--opset", default=14, type=int)
    args = parser.parse_args()

    import torch
    from pyannote.audio import Pipeline

    print(f"loading {PIPELINE_ID}")
    pipeline = Pipeline.from_pretrained(PIPELINE_ID, token=args.token)
    if pipeline is None:
        raise SystemExit(
            "pipeline did not load. Accept the model conditions on the Hugging Face "
            "page and make sure your token has read access."
        )

    model = find_segmentation_model(pipeline)
    model.eval()

    metadata = collect_metadata(model)
    print("metadata:")
    for key, value in metadata.items():
        print(f"  {key} = {value}")

    window_size = int(metadata["window_size"])
    dummy = torch.zeros(1, 1, window_size, dtype=torch.float32)

    with torch.no_grad():
        shape = tuple(model(dummy).shape)
    print(f"output shape for one window: {shape}")

    print(f"exporting to {args.out}")
    torch.onnx.export(
        model,
        dummy,
        str(args.out),
        input_names=["x"],
        output_names=["logits"],
        # Only the batch axis varies. The window length is fixed by the model,
        # and sherpa-onnx feeds exactly window_size samples per call.
        dynamic_axes={"x": {0: "batch"}, "logits": {0: "batch"}},
        opset_version=args.opset,
        do_constant_folding=True,
    )

    import onnx

    proto = onnx.load(str(args.out))
    # sherpa-onnx reads every one of these by name and refuses the model if any
    # is missing, so they are written after export rather than assumed.
    del proto.metadata_props[:]
    for key, value in metadata.items():
        entry = proto.metadata_props.add()
        entry.key = key
        entry.value = value
    onnx.save(proto, str(args.out))

    import onnxruntime as ort

    session = ort.InferenceSession(str(args.out), providers=["CPUExecutionProvider"])
    out = session.run(None, {"x": dummy.numpy()})
    print(f"onnxruntime check passed, output {out[0].shape}")

    print(f"\nwrote {args.out}")
    print("point scribble.toml at it:")
    print(f'  segmentation_model = "{args.out.resolve().as_posix()}"')
    return 0


if __name__ == "__main__":
    sys.exit(main())
