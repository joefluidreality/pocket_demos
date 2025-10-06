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
- PWM control for precise pixel control
- Button interface for pattern selection

## Hardware Requirements

- Arduino-compatible microcontroller
- SPI-controlled display/pixel array
- Button for user interaction
- Various control pins for display management

## Files

- `Pocket_Demo_V3.ino` - Main Arduino sketch
- `*_frame_data_*.h` - Pre-recorded animation frame data files

## Usage

1. Upload the `Pocket_Demo_V3.ino` sketch to your Arduino
2. Connect the required hardware components
3. Use the button to cycle through different demo patterns
4. The device will automatically play recorded animations

## Pin Configuration

- SPI MOSI: Pin 11
- SPI Clock: Pin 13
- SPI Latch Enable: Pin 6
- Blank: Pin 9
- Shift Enable: Pin 4
- HV Enable: Pin 5
- HV Control: Pin 23
- Button: Pin 7
