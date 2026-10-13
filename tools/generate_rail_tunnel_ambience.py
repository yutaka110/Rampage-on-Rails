"""Build a phase-aligned tunnel version of the project's seamless rail loop."""
from array import array
import math
from pathlib import Path
import sys
import wave


def main():
    audio = Path(__file__).resolve().parents[1] / "Resources" / "audio"
    with wave.open(str(audio / "title_rail_ambience.wav"), "rb") as source:
        params = source.getparams()
        if params.nchannels != 1 or params.sampwidth != 2:
            raise ValueError("Expected the project's mono 16-bit PCM rail loop")
        samples = array("h", source.readframes(params.nframes))
    if sys.byteorder != "little":
        samples.byteswap()
    dry = [sample / 32768.0 for sample in samples]
    alpha = 1.0 - math.exp(-2.0 * math.pi * 2600.0 / params.framerate)
    state = 0.0
    filtered = []
    # Warm up for a whole period; do not introduce a filter transient at wrap.
    for cycle in range(2):
        for sample in dry:
            state += alpha * (sample - state)
            if cycle:
                filtered.append(state)
    taps = [(round(seconds * params.framerate), gain) for seconds, gain in
            [(0.070, 0.13), (0.113, 0.09), (0.177, 0.05)]]
    wet = [0.78 * sample + sum(filtered[(index - delay) % len(filtered)] * gain
                             for delay, gain in taps)
           for index, sample in enumerate(filtered)]
    peak = max(abs(sample) for sample in wet)
    gain = min(1.0, 0.92 / max(peak, 0.00001))
    encoded = array("h", (round(sample * gain * 32767.0) for sample in wet))
    if sys.byteorder != "little":
        encoded.byteswap()
    destination = audio / "title_rail_ambience_tunnel.wav"
    # Preserve frame count and format so both playback cursors stay aligned.
    with wave.open(str(destination), "wb") as output:
        output.setparams(params)
        output.writeframes(encoded.tobytes())
    print(f"{destination.name}: {params.nframes} frames, {params.framerate} Hz, "
          f"peak={peak * gain:.4f}, loop_edge_delta={abs(wet[-1] - wet[0]) * gain:.6f}")


if __name__ == "__main__":
    main()
