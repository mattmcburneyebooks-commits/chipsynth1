# ChipSynth

A simple retro video game sound VST3 synth (also builds as a standalone app so you can test without a DAW).

**Sound engine:** NES-style pulse (12.5/25/50/75% duty), 4-bit stepped triangle, LFSR noise + "metal" short noise, ADSR, vibrato, pitch sweep, arpeggiator (held notes / major / minor / octaves), bit crusher + downsampler.

It is monophonic like a real chip channel. Playing a chord with Arpeggio set to "Held Notes" cycles through the notes, the classic chiptune trick.

## Easiest way to build (no tools to install): GitHub Actions
1. Make a free GitHub repo and push this folder to it.
2. Open the repo's **Actions** tab, wait for "Build ChipSynth" to finish (about 10 min).
3. Download the artifact for your OS (Windows / macOS / Linux). It contains the `.vst3` and a Standalone app.

## Install the VST3
- **Windows:** copy `ChipSynth.vst3` to `C:\Program Files\Common Files\VST3\`
- **macOS:** copy to `~/Library/Audio/Plug-Ins/VST3/`, then run `xattr -cr ~/Library/Audio/Plug-Ins/VST3/ChipSynth.vst3` (the build is unsigned)
- **Linux:** copy to `~/.vst3/`

Rescan plugins in your DAW. It shows up as an instrument.

## Build locally instead
Needs CMake 3.22+ and a C++ compiler (Visual Studio / Xcode / g++).

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

Output lands in `build/ChipSynth_artefacts/Release/`.

## Sound recipes
| Sound | Waveform | Settings |
|---|---|---|
| Coin / pickup | Pulse 50% | Decay 150ms, Sustain 0, Sweep +30, play a short high note |
| Jump | Pulse 25% | Decay 200ms, Sustain 0, Sweep +20 |
| Laser / shoot | Pulse 12.5% | Decay 250ms, Sustain 0, Sweep -40 |
| Power-up | Pulse 25% | Arp Major, Arp Speed 30, Sweep +6, Decay 500ms |
| Explosion | Noise | Decay 1500ms, Sustain 0, Sweep -12, low note, Bit Depth 6 |
| Bass | Triangle | Sustain 1, Release 50ms |
| Lead | Pulse 25% | Vibrato Depth 0.3, Sustain 0.7 |
| Crunchy lo-fi | any | Bit Depth 6, Downsample 4 |
