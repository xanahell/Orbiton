# Motion (VST3)

Chorus / flanger / moving pan + phaser, tremolo, drive and bitcrusher. Built with JUCE.

## Get the Windows plugin without installing anything (GitHub Actions)
1. Create a free GitHub account and a new repository.
2. Upload this folder to it (everything, including the hidden `.github` folder).
3. Open the repo, tab **Actions**, wait for "Build Motion VST3" to finish (about 10 minutes).
4. Open the finished run, download **Motion-VST3-Windows** (a zip) from "Artifacts".
5. Unzip and copy the `Motion.vst3` folder to `C:\Program Files\Common Files\VST3\`.

## Add it to FL Studio
Options > Manage plugins > **Find installed plugins** (Start scan). Then find "Motion" in the
plugin database (Installed > Effects), or in a mixer insert slot: click a slot > Motion.
Works on a vocal track and on the Master insert (stereo only).

## Build locally (Windows)
Install Visual Studio 2022 (workload "Desktop development with C++") and CMake, then:

    cmake -B build
    cmake --build build --config Release --target Motion_VST3

Result: `build\Motion_artefacts\Release\VST3\Motion.vst3`

## Notes
- The macOS build is unsigned: macOS may block it until you allow it in System Settings > Privacy & Security.
- The plugin reports its latency (from 4x oversampling) to the host, so FL compensates automatically.
