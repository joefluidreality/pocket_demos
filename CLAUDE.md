# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for a **pneumatic haptic display** — not an LED panel. A Teensy 4.0 drives 32
air-cell "pixels" through a single 64-output HV507 high-voltage shift register. Each pixel
is controlled by **two** HV507 channels: a `finger` valve and a `reservoir` valve. The
device plays pre-recorded inflation animations and can be driven manually over serial.

## Build / flash / monitor

The project targets the **Teensy 4.0** and builds with PlatformIO, but the sketch is kept in
its Arduino-IDE folder (`Pocket_Demo_V3/`) so it also opens in the Arduino IDE. `platformio.ini`
points `src_dir` at that folder.

```
pio run                 # build
pio run -t upload       # build + flash (uses teensy-cli loader, no GUI app needed)
pio device monitor      # serial monitor
```

There are no tests, lint, or CI — this is single-sketch embedded firmware.

⚠️ **Baud mismatch:** the sketch calls `Serial.begin(250000)` but `platformio.ini` sets
`monitor_speed = 115200`. To read serial output correctly, monitor at **250000**
(`pio device monitor -b 250000`) or fix one of the two to match.

## Architecture

Everything lives in [Pocket_Demo_V3/Pocket_Demo_V3.ino](Pocket_Demo_V3/Pocket_Demo_V3.ino).
The file is ~2300 lines, but most of it is frame data; the logic is the last ~350 lines
(from the forward declarations onward).

### The animation engine runs inside the timer ISR
`updatePWM()` is an `IntervalTimer` callback firing at ~60 kHz (`PWM_BASE_FREQ` 15 kHz ×
`SUBPHASES` 4). It does *all* the real work in interrupt context: advancing the current
animation frame, running the deflate ramps, mapping pixel state to physical valve channels,
and shifting bits out over SPI. `loop()` only handles the button, serial commands, and the
standby timeout. When changing animation/timing behavior, you are almost always editing
`updatePWM()`, not `loop()`.

### Pixel → valve mapping (the `finger` / `reservoir` arrays)
`finger[32]` and `reservoir[32]` map a logical pixel index (0–31) to its two physical HV507
output channels (0–63). **Multiple alternate mappings are commented out** near the top of the
file ("TRACES UP", "TRACES IDK", "Traces OUT") — these correspond to different hardware trace
routings. Only one pair may be active at a time. If pixels light in the wrong physical
location, you are likely on the wrong mapping.

Valve semantics, applied per pixel each ISR tick:
- **Inflate:** `finger = 0`, `reservoir = 1`
- **Off:** `finger = 0`, `reservoir = 0`
- **Reverse / deflate:** `finger = 1`, `reservoir = 0` (vents air)

### PWM / pressure levels
`desired[pix]` holds a level 0–4. Within each ISR tick `pwm_phase` cycles 0–3, and a pixel
inflates only while `desired[pix] > pwm_phase` — so level 4 is always-on, level 0 is off, and
1–3 are partial duty (partial pressure). Frame data uses these same 0–4 values.

### Demos and frame data
Four animations are stored inline as `demoFramesN[NUM_FRAMES_N][32]` arrays (Up/Down Bar,
Ball Rotation, Bouncing Ball, Left/Right Bar). Each frame is a 32-element array of levels.
Frames advance every `DEMO_INTERVAL_SUBPHASES` (1000) ISR ticks ≈ 16.7 ms/frame.

The button on `BUTTON_PIN` (7) cycles `demoMode` 1→4→1. Each demo is bracketed by a
`REVERSE_DURATION_MS` (400 ms) deflate: a **pre-reverse** before the demo starts and a
**post-reverse** after it ends (see the `reverseActive` / `preReverseActive` state machine in
`updatePWM`). After `STANDBY_TIME` (10 s) with no button activity, HV is disabled.

### High-voltage sequencing
`enableHV()` raises `PIN_HV_EN`, waits 1 s for the supply to settle, then raises `PIN_HV_CTRL`.
`disableHV()` drops both. HV is enabled on the first button press, not at boot.

### Serial command interface
`SET 0 <pixel> <level> [<pixel> <level> ...]` sets manual pixel levels and **disables the
demo** (`demoMode = 0`). Address must be `0`, pixel 0–31, level 0–4. Parsed by
`parseSetCommand()`.

### SPI output path
`packBits()` packs the 64-element `vals[]` byte array into two `uint32_t`s; `shiftOut64()`
clocks them out LSB-first at 8 MHz (`SPI_MODE0`) between `PIN_SPI_LE` toggles.

## Supporting files (root directory)

- **[animation_visualizer.html](animation_visualizer.html)** — a standalone, self-contained
  web visualizer (open directly in a browser, no server). It embeds its **own JSON copy** of
  the frame data (`DEMOS`) plus a `LAYOUT` mapping pixel# → grid position. This copy is **not**
  generated from the firmware — if you edit `demoFramesN[][]` in the sketch, the visualizer
  will drift out of sync unless you update its `DEMOS` blob too.
- **`*_frame_data_*.h`** — standalone exported animation arrays. These are an animation
  *library*, **not** included by the sketch (it has no `#include` of them and defines its
  frames inline). Several reuse the same `NUM_FRAMES_2` / `demoFrames2` names, so they cannot
  all be compiled together — they're meant to be copied into the sketch one at a time.
- **`Homer_haptic_output_pixel_numbering.png`** — reference image for the physical pixel
  numbering / layout.
