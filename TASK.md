# User outcome and work record

Build a native C++ desktop instrument for Linux and macOS: file/camera source, split stream with adjustable delay, difference blending, temporal motion blur, and experimental film controls understandable to a MAX/MSP performer.

## Delivered candidate

Repository: /home/chung/Projects/frame-differencer. Candidate is the complete local working tree, unpublished. C++17 / Qt6 / OpenCV engine and desktop application, CMake build, local Makefile fallback, engine tests, independent integration tests, README, macOS camera-usage plist. No Python application source.

## Coordination

OpenRig identity, owned queue, ps, discover and context discovery were attempted. This terminal has no seat binding and the daemon is down, so no durable OpenRig row or queue handoff was possible. No topology was started or invented. User explicitly requested collaboration; an in-session dev_check agent independently reviewed the exact candidate and wrote tests/integration_test.cpp. This repository-local note is the durable work record until a live seat can claim the work.

## Verification and findings

Linux compilation through make using g++, Qt6Widgets 6.11.2 and OpenCV5 5.0.0. Engine tests cover absolute difference without unsigned wrap, zero delay, static frames, temporal accumulation, anaglyph channels, and resolution-change reset. The initial API-only test failed at link because implementation did not exist; this is not a behavioral red test.

Independent checker exercised generated MJPEG input, complete 12-frame file processing and readable MJPEG output, 160×120 output size, paused-start recording and GUI anaglyph patch. GUI smoke verifies an actual processed demo frame arrives. Fixed checker findings: PIC flag required by this Qt build, lost recording request while paused, stale pause checkbox after source switch, recording error overwritten by normal status, recording state stale after failure. Host GTK platform theme required clearing QT_QPA_PLATFORMTHEME for offscreen tests. GCC16 emits a Qt header SFINAE warning; application compiles successfully.

## Limits and continuation

macOS build/bundle and physical camera path untested. CMake configure not run because CMake is absent on this host; Linux Makefile build is verified. Camera driver calls may stall shutdown. CPU processing, 720p working resolution, nominal-rate silent MJPEG export. No Syphon, OSC/MIDI, audio, native installers or saveable user presets. README gives controls and platform build instructions. Next useful validation: build on a Mac, authorize camera access, exercise real camera and external display. Native Syphon and OSC can then be added to the existing worker/output seam.

Final independent review: dev_check reported complete `make test` exit 0 after fixes, including intentional invalid recording-path handling; no further critical findings. OpenCV warnings during that negative test are expected backend probing. Final local verification also passed engine, integration and real-frame GUI smoke. Source was formatted with clang-format and reverified.

## macOS packaging and GitHub follow-up (2026-09-30)

User target: M3 Max MacBook Pro on latest macOS. User authorized upload to their GitHub account; live GitHub identity verified as andrewfrangella. Default repository visibility private. Added Apple Silicon macOS26 build script, native GitHub runner workflow, install-time Qt deployment plus full OpenCV dependency fixup, ad-hoc signing, arm64/dependency/plist verification, offscreen and Cocoa staged-app smoke, downloadable ZIP and Mac installation instructions. Independent dev_check reviewed exact packaging sources and recommended Cocoa smoke in addition to offscreen.

Local CMake configure/build and CTest passed 3/3 on Linux (engine, real-frame GUI smoke, file/recording integration). Shell syntax and packaging verifier parse checked. Actual Apple Silicon build must be verified via GitHub Actions; camera permission needs an end-user Mac test. GitHub authentication was initially missing, then supplied by user and verified. Work record will be updated with remote result.

First actual macOS26 arm64 CI run 36733223923 compiled all C++ targets and passed 3/3 tests, then failed bundle fixup. Root cause: CMake BundleUtilities reads CFBundleExecutable from the line following its key; compact plist had key/value together and parser took next key's value (bundle identifier) as executable. Added a platform-independent test against actual CMake parser, observed failure on original template, split key/value lines. The first regression-test rerun exposed a test fixture issue: @ONLY left ${...} plist placeholders unexpanded. Removed @ONLY to match CMake's bundle template substitution. Native packaging rerun pending.

Native run36733911304 passed all4 tests and reached dependency fixup, which could not resolve Qt's transitive @rpath/libbrotlicommon.1.dylib. macdeployqt had already copied that library into the app Frameworks directory. Added the staged Frameworks directory to BundleUtilities resolution paths; retains closure verification and actual launch checks.

Native run36734352187 completed dependency closure and ad-hoc code signing, verified all99 embedded Mach-O binaries arm64 and bundle-relative, then the redundant staged offscreen smoke failed because macdeployqt intentionally deploys only Cocoa. Removed that staged offscreen check; unbundled CTest already exercises offscreen and staged smoke now tests the shipped Cocoa plugin directly.

## GPU, Syphon, audio and ESP32 follow-up

Baseline Mac version c77d078 verified in native run36734822807; downloadable private release v0.1.0 retained. Only after that verification, built GPU processing and modulation with separate GPU/modulation builders and independent checker. GPU effects use OpenGL3.3 Linux/4.1Mac. Decode, bounded timestamp history and preview/record readback remain CPU; Syphon publishes GPU texture directly. Audio-input envelope/broad bands, OSC scalar/bundles+JSON UDP, newline JSON POSIX serial receiver, configurable bipolar maps and saved presets implemented. ESP-NOW stays on user's receiver; source templates document computer-facing bridge protocol.

User added Settings maximum delay requirement: maximum1..600000ms and history budget32..4096MiB persist; knobs, sliders, frame-unit conversion and delay modulation follow max. CPU test failed on old10s behavior then passed; modulation maxrange test likewise red/green; GPU parity confirms long-delay and immediate pruning. Settings widget test uses isolated QSettings path and verifies persisted max/budget, frame ranges and clamping. Inputs do not start or enumerate devices by default.

Final integrated local CMake build succeeded and all6 CTests passed on Linux with real GPU display, UDP socket and PTY access. GPU: all5modes across blur radii, floats/trails/strobe and strided/asymmetric inputs, maxdelay. Modulation: malformed/truncatedOSC, actualUDP and simulatedserial reads, audioDSP sensitivity/bands/attack/release. Physical microphone, ESP32 and camera remain untested. Native Syphon loopback test (checker-owned) verifies received pixels/orientation and discovery; native macOS CI must execute before publishing GPU release. Syphon source pinned official commitf4761677a45b8034a3c2069ec0f3d2553da81fba, built arm64 on Mac rather than obsolete2019SDK binary. Notices included in app Resources.

First native GPU/Syphon run36741673197 compiled the official pinned Syphon framework and all modules;6of7tests passed, including real Mac GPUparity, OSC/serial/audioDSP and Settings. Syphon frame received but RGB/upright assertion failed. Added diagnostic pixels and explicit makeCurrent before publication in checker-owned loopback test, preserving expectedorientation; next native run must determine root cause. Audio device choice now uses actual system default or stable deviceID, with explicit disconnected-device message rather than silently switching on hotplug.
