# WaterFlowAlarm

A water flow monitoring alarm built for the **M5Atom Echo** (ESP32-based).

The device detects water flow via a switch sensor and provides audio and visual feedback:

- **Start sound** — A short melody plays when water flow is detected
- **Background beep** — Periodic beeps while water is flowing
- **Alarm** — An alternating two-tone alarm triggers if water flows continuously for more than 60 seconds
- **LED indicators** — Green (idle), blue (flowing), blinking red (alarm)

Audio is output via I2S to the built-in speaker/amplifier of the Atom Echo.

## Hardware

- M5Atom Echo
- Flow/switch sensor on GPIO 32
