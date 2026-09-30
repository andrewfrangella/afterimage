# Afterimage

A C++17 GPU desktop temporal video instrument for Linux and macOS, using Qt 6 and OpenCV. Start with the built-in moving-shape demo, open a video, or connect a camera by device index.

## Build and run

For your M3 Max MacBook Pro, use the **Apple Silicon (arm64), macOS 26+** build.

### Download the Mac app

Once this repository is uploaded, open **Actions → macOS Apple Silicon → the latest successful run → Artifacts → Afterimage-macos-arm64**. Unzip the artifact download, then unzip `Afterimage-macos-arm64.zip` to get `Afterimage.app`. Drag the app into Applications and open it. Qt, OpenCV, Syphon and codec libraries are included; Homebrew is not required to run the packaged app.

The initial package is locally (ad-hoc) signed, not Apple-notarized. If macOS blocks it, use **System Settings → Privacy & Security → Open Anyway** after attempting to open it. Do not disable Gatekeeper globally. On the first camera connection, allow Afterimage's camera request. You can change this later under **Privacy & Security → Camera**. Close and reopen the app after changing camera access.

### Build locally on your Mac

Install Apple's command line tools and Homebrew first, then run from a native terminal without Rosetta:

```sh
xcode-select --install
brew install cmake qt opencv
./scripts/build-macos.sh
open dist/macos-arm64/Afterimage.app
```

The script builds arm64, runs the engine, recording and GUI tests, bundles Qt/OpenCV dependencies, signs and verifies the bundle, and produces `dist/Afterimage-macos-arm64.zip`. This build targets macOS 26 or newer. The GitHub workflow runs the same script on an Apple Silicon Mac runner. A Developer ID certificate and notarization are optional future steps for a normal verified-publisher installation.

Dependencies for source builds: a C++17 compiler, CMake 3.20+, Qt 6.5+ Widgets, OpenGL, Network and Multimedia, OpenCV (core, imgproc, videoio), and threads.

On Linux with dependencies installed:

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/afterimage
```

Linux also supports `make -j2 && make test` using pkg-config (opencv4 or opencv5). The local Linux executable is `build/afterimage`.

## The patch

Think of the input as a matrix stream split into two taps. One tap is immediate, the other reads history. Absolute difference computes `abs(current - delayed)` independently per RGB channel. Still regions disappear; motion leaves two contours. Zero delay produces black. Signed difference centers equal pixels at gray 128.

Delay has a slider and numeric control, in milliseconds or source-rate frames. The delayed tap selects the newest retained sample at or before the requested time; it does not interpolate frames. Files advance by nominal frame rate; cameras use elapsed capture time. Switching units preserves the delay time. Processing fits inside 1280×720 while preserving aspect ratio. Settings lets you change maximum delay (default 10000 ms, up to 600000 ms) and history memory budget (default 256 MiB). Both persist across launches. The delay knob, slider, frame-unit range and modulation range follow the configured maximum. History retains at most that duration and memory budget; warm-up or an unavailable delay uses the oldest frame and displays a warning.

- **Temporal blur / trails:** an exponential exposure accumulator: `(1 - trails) × new + trails × previous`. 0 is crisp; 0.9 is a long tail. It is distinct from spatial softness.
- **Spatial softness:** Gaussian blur radius 0–20.
- **Temporal anaglyph:** current red with delayed green and blue, creating red/cyan time separation.
- **Wet / dry:** crossfade between processed effect and source.
- **Strobe hold:** holds the output for N input frames, giving stepped motion without black flashes.
- **Difference gain and invert:** amplify contours and reverse polarity.

Signal order: blend mode → gain → spatial softness → invert → wet/dry → temporal blur → strobe hold. Starting patches provide motion contours, long exposure, anaglyph and stroboscopic studies. Pause freezes capture/processing; Space toggles it. Clear history resets both the delay line and exposure accumulator. Files loop by default, resetting history at the boundary.

Output window is independent of the controls. Double-click it to toggle fullscreen, and move it onto your projector/display. Record AVI writes the processed working-resolution output as silent MJPEG at nominal source FPS; stop recording or close the app to finalize the container. Slow hardware stretches wall-clock playback; recording has one output frame per processed input frame. Camera FPS metadata is nominal and falls back to 30.

## Scope and limitations

Effects run in OpenGL shaders on Linux (3.3 core) and macOS (4.1 core). If a GPU context is unavailable the status reports a CPU fallback; CPU reference is selectable. Pixel processing (blend, gain, blur, wet/dry, float temporal accumulator and strobe) runs on GPU. Video decoding, timestamped delay history, uploads, preview/recording readback and control logic remain CPU work; this is not an end-to-end zero-copy video pipeline and performance depends on driver and frame size.

On macOS, enable **Publish Syphon: Afterimage** while GPU processing is selected. Choose Afterimage in a Syphon client such as MAX/Jitter or your visual mixer. Frames publish directly from the GPU output texture. Linux shows Syphon unavailable. Native tests verify server discovery and received image dimensions, colors and orientation.

Open **Audio / sensors / mappings…** for microphone/line-input reactivity, USB serial receiver and OSC/UDP input. Input devices stay off until started. Audio envelopes and sensor values can target every processing parameter and audio gain/attack/release, with bipolar attenuverters, offsets, input ranges, smoothing, stale timeout and save/load presets. See [protocol and mapping guide](examples/MODULATION.md), [USB receiver example](examples/esp32-serial/esp32-serial.ino), and [wireless OSC example](examples/esp32-osc/esp32-osc.ino). Your existing ESP-NOW receiver remains responsible for the radio link and emits newline JSON to USB.

MIDI, audio synthesis/playback, file scrubbing and Developer ID signing/notarization remain outside this version. Linux and macOS use the same C++ source and CMake build. Native macOS builds and packaging are verified in the GitHub workflow. Physical camera, microphone and ESP32 firmware behavior require real-device tests. Camera capture/open uses the platform OpenCV backend: a stalled driver can delay source switching or shutdown, though it does not run on the GUI thread. Codecs depend on the installed OpenCV backend.

See TASK.md for review and verification evidence.
