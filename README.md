# Pocket Demos

Arduino firmware for Pocket Demo V3 with record and autoplay functionality.

## Overview

This project contains Arduino code for controlling a pocket-sized demonstration device with various animation patterns and the ability to record and replay sequences.

## Features

- Multiple demo patterns including:
  - Ball loop animation
  - Expanding animation
  - Left-right bar animation
  - Raindrops animation
  - Up-down bar animation
- Record and autoplay functionality
- PWM control for precise pixel control (5 intensity levels, ~60kHz effective rate)
- Button interface for pattern selection
- Modular actuator configuration system supporting multiple PCB types:
  - LEMONT PCB
  - HOMER PCB with traces up (HOMER_UP)
  - HOMER PCB with traces down (HOMER_DOWN)

## Hardware Requirements

- Arduino-compatible microcontroller
- SPI-controlled display/pixel array
- Button for user interaction
- Various control pins for display management

## Files

- `Pocket_Demo_V3.ino` - Main Arduino sketch
- `*_frame_data_*.h` - Pre-recorded animation frame data files

## Configuration

### Actuator Type Selection

The firmware supports multiple actuator PCB configurations. To select your actuator type, edit line 23 in `Pocket_Demo_V3.ino`:

```cpp
#define ACTUATOR_TYPE HOMER_UP    // Choose: LEMONT, HOMER_UP, or HOMER_DOWN
```

Available configurations:
- `LEMONT` - For Lemont PCB with traces up
- `HOMER_UP` - For Homer PCB with traces up (top PCB)
- `HOMER_DOWN` - For Homer PCB with traces down

Each configuration uses optimized pin mappings for the specific PCB layout.

## Usage

1. Configure the actuator type in `Pocket_Demo_V3.ino` (see Configuration section above)
2. Upload the sketch to your Arduino
3. Connect the required hardware components
4. Use the button to cycle through different demo patterns (1-4)
5. Device enters standby mode after 10 seconds of inactivity
6. Press button to wake and cycle to next demo mode

## Pin Configuration

- SPI MOSI: Pin 11
- SPI Clock: Pin 13
- SPI Latch Enable: Pin 6
- Blank: Pin 9
- Shift Enable: Pin 4
- HV Enable: Pin 5
- HV Control: Pin 23
- Button: Pin 7 - connect to ground with momentary switch to activate animation
