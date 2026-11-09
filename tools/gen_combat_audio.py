#!/usr/bin/env python3
"""Generate deterministic placeholder combat SFX for M30-T10.

The project currently ships synthesized placeholder audio. These cues make
light/heavy swings, impacts, block/parry/guard-break and dodge distinguishable
without adding external assets or a runtime dependency.
"""

from __future__ import annotations

import argparse
import math
import random
import struct
import wave
from pathlib import Path

SAMPLE_RATE = 22050
PEAK = 0.72


def _sine(freq: float, t: float) -> float:
    return math.sin(math.tau * freq * t)


def _fade(samples: list[float], seconds: float = 0.006) -> None:
    count = min(len(samples) // 2, max(1, int(SAMPLE_RATE * seconds)))
    for i in range(count):
        gain = i / count
        samples[i] *= gain
        samples[-1 - i] *= gain


def _normalize(samples: list[float]) -> None:
    peak = max((abs(value) for value in samples), default=0.0)
    if peak <= 0.0:
        return
    scale = PEAK / peak
    for i, value in enumerate(samples):
        samples[i] = value * scale


def _write_wav(path: Path, samples: list[float]) -> None:
    _fade(samples)
    _normalize(samples)
    payload = bytearray()
    for value in samples:
        clamped = max(-1.0, min(1.0, value))
        payload.extend(struct.pack("<h", int(round(clamped * 32767.0))))
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(SAMPLE_RATE)
        handle.writeframes(payload)


def _light_swing(rng: random.Random) -> list[float]:
    duration = 0.16
    count = int(SAMPLE_RATE * duration)
    low = 0.0
    samples: list[float] = []
    for i in range(count):
        t = i / SAMPLE_RATE
        u = t / duration
        envelope = math.sin(math.pi * u) ** 1.4
        noise = rng.uniform(-1.0, 1.0)
        low += 0.18 * (noise - low)
        high = noise - low
        sweep = 520.0 + 900.0 * u
        samples.append(envelope * (high * 0.34 + _sine(sweep, t) * 0.045))
    return samples


def _heavy_swing(rng: random.Random) -> list[float]:
    duration = 0.30
    count = int(SAMPLE_RATE * duration)
    low = 0.0
    samples: list[float] = []
    for i in range(count):
        t = i / SAMPLE_RATE
        u = t / duration
        envelope = math.sin(math.pi * u) ** 0.9
        noise = rng.uniform(-1.0, 1.0)
        low += 0.055 * (noise - low)
        sweep = 125.0 - 55.0 * u
        samples.append(envelope * (low * 0.62 + _sine(sweep, t) * 0.12))
    return samples


def _hit(rng: random.Random) -> list[float]:
    duration = 0.16
    count = int(SAMPLE_RATE * duration)
    samples: list[float] = []
    for i in range(count):
        t = i / SAMPLE_RATE
        u = t / duration
        thump = _sine(82.0 - 30.0 * u, t) * math.exp(-13.0 * t)
        click = rng.uniform(-1.0, 1.0) * math.exp(-42.0 * t) * 0.30
        crunch = _sine(170.0, t) * math.exp(-22.0 * t) * 0.10
        samples.append(thump * 0.82 + click + crunch)
    return samples


def _block(rng: random.Random) -> list[float]:
    duration = 0.15
    count = int(SAMPLE_RATE * duration)
    samples: list[float] = []
    for i in range(count):
        t = i / SAMPLE_RATE
        ring = (
            _sine(470.0, t) * 0.32
            + _sine(735.0, t) * 0.20
            + _sine(1120.0, t) * 0.10
        ) * math.exp(-28.0 * t)
        click = rng.uniform(-1.0, 1.0) * math.exp(-55.0 * t) * 0.18
        samples.append(ring + click)
    return samples


def _parry(rng: random.Random) -> list[float]:
    duration = 0.25
    count = int(SAMPLE_RATE * duration)
    samples: list[float] = []
    for i in range(count):
        t = i / SAMPLE_RATE
        ring = (
            _sine(1180.0, t) * 0.35
            + _sine(1760.0, t) * 0.22
            + _sine(2360.0, t) * 0.12
        ) * math.exp(-15.0 * t)
        shimmer = rng.uniform(-1.0, 1.0) * math.exp(-32.0 * t) * 0.06
        samples.append(ring + shimmer)
    return samples


def _guard_break(rng: random.Random) -> list[float]:
    duration = 0.34
    count = int(SAMPLE_RATE * duration)
    samples: list[float] = []
    for i in range(count):
        t = i / SAMPLE_RATE
        u = t / duration
        low = _sine(72.0 - 18.0 * u, t) * math.exp(-7.0 * t) * 0.55
        crack = rng.uniform(-1.0, 1.0) * math.exp(-18.0 * t) * 0.38
        metal = _sine(390.0, t) * math.exp(-9.0 * t) * 0.16
        samples.append(low + crack + metal)
    return samples


def _dodge(rng: random.Random) -> list[float]:
    duration = 0.18
    count = int(SAMPLE_RATE * duration)
    low = 0.0
    samples: list[float] = []
    for i in range(count):
        t = i / SAMPLE_RATE
        u = t / duration
        envelope = math.sin(math.pi * u) ** 1.2
        noise = rng.uniform(-1.0, 1.0)
        low += 0.10 * (noise - low)
        high = noise - low
        samples.append(envelope * (high * 0.28 + _sine(700.0 + 500.0 * u, t) * 0.03))
    return samples


CUES = {
    "melee_swing_light": _light_swing,
    "melee_swing_heavy": _heavy_swing,
    "melee_hit": _hit,
    "melee_block": _block,
    "melee_parry": _parry,
    "melee_guard_break": _guard_break,
    "melee_dodge": _dodge,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "assets" / "audio",
        help="audio directory (default: assets/audio)",
    )
    args = parser.parse_args()
    for index, (name, factory) in enumerate(CUES.items()):
        samples = factory(random.Random(3000 + index))
        path = args.output / f"{name}.wav"
        _write_wav(path, samples)
        print(f"{path} {len(samples) / SAMPLE_RATE:.3f}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
