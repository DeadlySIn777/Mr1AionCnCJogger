# Tiny Home Personal Assistant - Universal ESP32 LoRa Firmware Plan

## Universal Device Firmware Concept

### Device Types Supported
- **LIGHT_SWITCH** - Simple on/off relay control
- **DIMMER_LIGHT** - PWM dimming capability + relay
- **RGB_STRIP** - WS2812B/Neopixel control + effects
- **FAN_CONTROLLER** - Variable speed + direction control
- **OUTLET_SWITCH** - Smart outlet/appliance control
- **SENSOR_NODE** - Temperature/humidity/motion sensing
- **COMBO_DEVICE** - Multiple functions on one board

### Configuration Methods
1. **Web Interface** - Connect to ESP32 AP for browser-based setup
2. **Voice Configuration** - "Set kitchen device to RGB light"
3. **Mobile App** - Simple configuration app
4. **Auto-Discovery** - Device announces capabilities, user assigns role

### Key Features
- **Modular GPIO assignment** based on device type
- **Hardware auto-detection** for connected components
- **Live reconfiguration** without firmware reflashing
- **LoRa mesh networking** with auto-pairing
- **OTA updates** over LoRa network
- **Battery monitoring** and low-power modes
- **Alexa integration** via Home Assistant bridge

### Hardware Requirements
- ESP32 (C3/S3 preferred for cost)
- LoRa module (SX1276/SX1278)
- Relay modules as needed
- Optional: LED strips, sensors, PWM controllers

---

## Current Focus: Ubuntu + Roku TV Integration

### Hardware Setup
- Libre Computer S905X CC V1
- 55" TCL Roku TV (4K display)
- Ubuntu 22.04.3 Desktop ARM64

### Integration Goals
- Voice assistant with 4K UI
- TCL Roku TV control (CEC + Roku API)
- Clean, ADHD-friendly interface
- Alexa compatibility bridge
- Smart home device management