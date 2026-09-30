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
