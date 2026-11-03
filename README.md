# Sustenato Pedal
<p align="center"><image src="rc/sustenato_pedal.png" height="500px"></image></p>
     
A VST Plugin that provides infinite piano-like sustain, using transient detection, autocorrelation, a phase vocoder and more.

## This fork: hold-and-play auto catch, MIDI pedal, Linux build

Changes from upstream:

- **Auto Catch** (on by default): hold the pedal down and just play. Every new
  note is detected and caught as its own sustained layer once it settles, so
  tonguing C, E, G with the pedal held leaves the whole chord ringing. Turn it
  off for the original "play, then press" behaviour.
- **Latch** (on by default): press the pedal, play your notes, let go, and
  everything keeps ringing. The next press clears it and starts a new set (a
  quick tap with no notes just clears). Turn it off for piano behaviour, where
  letting go fades the sound out.
- **Settle** parameter (0.1-1.0 s, default 0.2 s): how long after a note starts
  it waits before catching it, so the attack isn't looped.
- **Pedal from anywhere**: MIDI sustain pedal (CC64), or the host-automatable
  **Pedal** parameter (map any MIDI CC or key to it in Carla/your DAW), as well
  as the original keybind and on-screen pedal. Pedal handling now runs on the
  audio thread, so presses from any source are safe and need no window focus.
- **Sound fixes**: new onset detector (normalised spectral flux with vibrato
  suppression) that doesn't false-trigger on steady or low notes; loops are now
  phase-aligned to the note's pitch period, which fixes notes that sustained
  far quieter than played; the attack is trimmed from each loop; fixed two
  buffer overruns. Max Layers now defaults to 5.
- **Linux/CMake build** producing VST3, LV2 and Standalone.

### Building (Fedora / Nobara)

```bash
sudo dnf install cmake ninja-build gcc-c++ git alsa-lib-devel freetype-devel \
  fontconfig-devel libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel \
  libXext-devel mesa-libGL-devel libcurl-devel
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # downloads JUCE and pffft
cmake --build build
```

On Debian/Ubuntu the packages are `libasound2-dev libfreetype-dev libfontconfig1-dev
libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl-dev
libcurl4-openssl-dev`.

Install for your user:

```bash
mkdir -p ~/.lv2 ~/.vst3
cp -r build/Sostenahto_artefacts/Release/LV2/Sostenahto.lv2 ~/.lv2/
cp -r build/Sostenahto_artefacts/Release/VST3/Sostenahto.vst3 ~/.vst3/
```

Then load it in Carla (or any LV2/VST3 host), open its window once and press
Skip on the start screen to enable it.

### Tests

```bash
cmake -B build -DSOSTENAHTO_BUILD_TESTS=ON && cmake --build build --target AutoCatchTest
./build/AutoCatchTest_artefacts/Release/AutoCatchTest
```

Plays synthetic tongued, slurred, vibrato and low-register notes through the
plugin and checks that each one is caught exactly once, sustains at its played
level, and that latch release/clear and piano release behave correctly.

## Features:
- Infinite sustain - let your notes and chords ring out for as long as you like.
- Fade In/Out - adjust the speed at which the sustain fades in/out.
- Wet/Dry - adjust your mix.
- Layering - select however many layers you want to stack sustained notes and chords.
- Compatible with any Keyboard-emulating foot-switch/pedal - control the sustain with your feet, as you would an FX pedal.
- ***INCONSISTENT*** Works with trills and vibrato
- Force Period - Uses a phase vocoder to stretch recorded samples to the set period. Wait less time before pressing the pedal.
  - Not compatible with trills and vibrato

### Recommended Usage:
- Best used as a live tool, whether for a live performance or recording
- Having a foot-switch/pedal is not necessary, but highly recommended (as you use your hands to play the instrument)
- Not tested as a post-processing tool
- Easiest to set it up in a VST Host

### Usage Instructions:
- Place the VST plugin anywhere in your plugin chain.
- Open the plugin's editor window and ensure it is in focus whenever you want to use it
  - If the window is not in focus, it will not receive keyboard events, hence you won't be able to toggle the sustainer using a keyboard key/foot-switch/pedal
- Upon playing any note/chord, press your corresponding sustain keybind
  - If you press it too quickly after playing, the sustained sound may not be clear/smooth. This can be used intentionally to create different effects, or can be reset quickly by re-triggering the keybind, given no new notes are played.

Designed for use with Electric Guitars and Amp Sims.

## Compatibility:
Works with Windows. Not tested, but it should work on Linux/Mac as well.
### Working:
- Cantabile 4
### Not Working:
- Waveform DAW - plugin not verified
