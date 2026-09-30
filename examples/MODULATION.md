# Audio and ESP32 modulation

Open Audio / ESP32 modulation. Inputs remain stopped until you start them. Refresh audio devices to select your interface, then Start audio. macOS asks for microphone access; if denied, enable Afterimage in System Settings → Privacy & Security → Microphone. Low/mid/high are broad one-pole bands around 250 Hz and 2.5 kHz, not a calibrated spectrum analyzer. Sensitivity scales RMS before the attack/release envelope.

Add a mapping and type any incoming source name. Built-in audio names are `audio.rms`, `audio.low`, `audio.mid`, `audio.high`. Map sensors to any processing target, including discrete `mode` (0 Difference, 1 Signed, 2 Anaglyph, 3 Source, 4 Delayed) and `invert` (threshold 0.5). Audio controls are targets `audio.gain` (0–20), `audio.attack` (1–2000 ms), and `audio.release` (1–5000 ms). Multiple mappings sum.

Mapping formula: `base + (intensity × clamp((input−min)/(max−min),0,1) + offset) × target_range`. Intensity is a bipolar attenuverter from −1 to +1; offset is also a fraction of the target range. Choose input min/max to match ADC, acceleration, distance, or normalized values. Smooth ms applies an exponential filter. Stale inputs stop affecting the controls after the timeout, restoring base values; live source readings indicate stale inputs. Set intensity 0.05 for a subtle 500 ms delay sweep (with the default 10000 ms delay maximum; the mapping range follows the maximum configured in Settings). To map a bipolar sensor, use appropriate input bounds and a negative offset to center the modulation. Save/load presets stores rows and audio envelope controls; it does not start hardware or networking.

## Wireless OSC / JSON

Listen OSC / JSON on UDP 9000. Send one scalar OSC float or int to `/sensor/knob` (source becomes `knob`). OSC bundles are accepted; future timetags are applied on receipt. Strings and multiargument OSC messages are not supported. Alternative: send a UTF-8 JSON datagram such as `{"knob":0.7,"tilt":0.3}`. Each numeric field becomes a source. Use a trusted local network: input has no authentication. Allow UDP through the computer's firewall. Set the computer's LAN address in the example ESP32 sketch. Port is adjustable. MAX can send the same OSC scalar messages using its OSC tools.

## USB serial / ESP-NOW receiver

Refresh ports, choose `/dev/cu.usbmodem…` or `/dev/cu.usbserial…` on macOS, `/dev/ttyACM…` or `/dev/ttyUSB…` on Linux, and connect at the receiver's baud (115200 default). The path is editable for another serial device. Close other serial monitors first. On Linux your account needs permission for that device, commonly membership in `dialout` or `uucp`.

Protocol: one JSON object per line, e.g. `{"knob":0.7,"tilt":-0.4}\n`. Partial reads are reassembled, malformed lines skipped, oversize lines discarded until the next newline. Debug text lines are ignored. An ESP-NOW receiver can keep its existing radio bridge and emit this format to USB. Afterimage reads the receiver, so no ESP-NOW radio is required on the computer. Match the payload layout and peer configuration to your existing ESP firmware; the serial demo illustrates the computer-facing protocol without replacing your receiver firmware.

Arduino examples are source templates; they have not been flashed to a physical ESP32. Select a suitable ADC pin for your specific ESP32 variant.
