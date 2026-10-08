#!/usr/bin/env python3
"""Draws a spectrogram contact sheet of every rendered voice (internal tool).

Usage:
    build/release/tools/vox_voice_report --wav-dir build/voices > /dev/null
    python3 tools/analysis/voice_spectrograms.py build/voices docs/images/voice-spectrograms.png

Needs numpy, scipy, and matplotlib.
"""

from __future__ import annotations

import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from scipy.io import wavfile  # noqa: E402
from scipy.signal import spectrogram  # noqa: E402


def load(path: Path) -> tuple[int, np.ndarray]:
    rate, data = wavfile.read(path)
    return rate, np.asarray(data, dtype=np.float64)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    folder, output = Path(sys.argv[1]), Path(sys.argv[2])
    files = [folder / "input.wav"] + sorted(p for p in folder.glob("*.wav") if p.name != "input.wav")
    columns = 5
    rows = (len(files) + columns - 1) // columns
    fig, axes = plt.subplots(rows, columns, figsize=(columns * 3.2, rows * 1.9), sharex=True, sharey=True)
    for ax in axes.flat:
        ax.set_axis_off()
    for ax, path in zip(axes.flat, files):
        rate, audio = load(path)
        freqs, times, power = spectrogram(audio, fs=rate, nperseg=1024, noverlap=768, window="hann")
        db = 10.0 * np.log10(power + 1e-12)
        ax.set_axis_on()
        ax.pcolormesh(times, freqs / 1000.0, db, shading="auto", cmap="magma", vmin=db.max() - 80, vmax=db.max())
        ax.set_ylim(0, 8)
        ax.set_title(path.stem, fontsize=8)
        ax.tick_params(labelsize=6)
    fig.supxlabel("time (s)", fontsize=8)
    fig.supylabel("frequency (kHz)", fontsize=8)
    fig.tight_layout()
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=60)
    # A 128-colour palette keeps the image small enough to keep in git.
    from PIL import Image

    Image.open(output).convert("RGB").quantize(colors=128).save(output, optimize=True)
    print(f"Wrote {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
