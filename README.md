# AIONMECH Universal CNC Pendant

A professional USB pendant controller for **ALL CNC software**. Features ESP32-WROOM with GC9A01 round LCD display and instant 8ms jog response.

![Version](https://img.shields.io/badge/version-2.0.0-blue)
![Platform](https://img.shields.io/badge/platform-Windows-lightgrey)
![ESP32](https://img.shields.io/badge/hardware-ESP32--WROOM-green)

---

## 🌐 Supported CNC Software (11 Total)

| Software | Platform | Auto-Detect |
|----------|----------|-------------|
| **FireControl** | Windows | ✅ Yes |
| **CutControl** | Windows | ✅ Yes |
| **Mach3** | Windows | ✅ Yes |
| **Mach4** | Windows | ✅ Yes |
| **LinuxCNC** | Linux | ✅ Yes |
| **UCCNC** | Windows | ✅ Yes |
| **Carbide Motion** | Windows/Mac | ✅ Yes |
| **Universal G-Code Sender** | Cross-platform | ✅ Yes |
| **OpenBuilds CONTROL** | Windows | ✅ Yes |
| **CNCjs** | Web-based | ✅ Yes |
| **Manual Mode** | Any | Always |

---

## ✨ Features

- ⚡ **8ms Jog Latency** - Near-instant response to encoder rotation
- 🔄 **Rotary Encoder** - Precise axis jogging with hardware interrupts
- 🖥️ **240×240 Round LCD** - Beautiful machine-themed display
- 🔌 **USB-C Connection** - Simple plug-and-play operation
- 🛡️ **Safety Handshake** - Keys only sent when CNC software is active
- 🎨 **Theme Support** - Automatic colors per CNC software
- 💾 **4-Button Control** - 2 momentary + latching + E-stop

---

## 🎛️ Control Scheme

### Button 1 (GPIO25) - Momentary

| Action | Press |
|--------|-------|
| Change Axis (X → Y → Z) | Short press |
| Change Jog Distance | Long press (2 sec) |

### Button 2 (GPIO4) - Momentary

| Action | Press |
|--------|-------|
| Change Speed | Short press |
| Toggle WCS/MCS View | Long press (2 sec) |

### Power Switch (GPIO27) - Latching

| Position | State |
|----------|-------|
| ON | Pendant active |
| OFF | Sleep mode |

### E-Stop (GPIO14) - Emergency

| State | Action |
|-------|--------|
| Released | Normal operation |
| **Pressed** | **ALL MOTION BLOCKED** |

### Rotary Encoder

| Action | Input |
|--------|-------|
| Jog + Direction | Rotate CW |
| Jog − Direction | Rotate CCW |

---

## 📋 Hardware Requirements

| Component | Model | GPIO |
|-----------|-------|------|
| ESP32 Board | ESP32-WROOM 38-pin USB-C | - |
| LCD Display | GC9A01 240×240 Round | GPIO2, 15, 18, 21, 23 |
| Rotary Encoder | 6-pin Industrial (A, B, A̅, B̅, OV, VCC) | GPIO32, 33 |
| Button 1 | Momentary Push | GPIO25 |
| Button 2 | Momentary Push | GPIO4 |
| Power Switch | Latching Toggle | GPIO27 |
| E-Stop | 19mm NC Mushroom | GPIO14 |

### Quick Wiring Reference

```
LCD (use 3V3 power):
  VCC   → 3V3          GND   → GND
  SCLK  → GPIO18       MOSI  → GPIO23
  CS    → GPIO15       DC    → GPIO2
  RST   → GPIO21       BL    → 3V3

Encoder (use 5V power):
  A     → GPIO32       B     → GPIO33
  OV    → GND          VCC   → 5V
  A̅, B̅  → Not connected

Buttons & Switches (to GND):
  Button 1     → GPIO25    Button 2    → GPIO4
  Power Switch → GPIO27    E-Stop (NC) → GPIO14
```

> 📖 See [WIRING.md](WIRING.md) for complete guide
> 📄 See [docs/WIRING_GUIDE.html](docs/WIRING_GUIDE.html) for printable PDF version

---

## 🚀 Quick Start

### 1. Upload Firmware

```bash
# Using Arduino IDE
1. Open esp32_pendant_serial_universal/esp32_pendant_two_button.ino
2. Select Board: ESP32 Dev Module
3. Install Library: LovyanGFX
4. Upload via USB-C
```

Or use PlatformIO:
```bash
cd Mr1AionCnCJogger
pio run -t upload
```

### 2. Install Windows App

Download from `windows-app/dist/`:
- **Installer**: `AIONMECH Pendant Controller-v2.0.0-x64.exe`
- **Portable**: `AIONMECH Pendant Controller-Portable-v2.0.0-x64.exe`

### 3. Connect & Use

1. Plug ESP32 into PC via USB-C
2. Run Windows app (auto-detects pendant)
3. Open your CNC software
4. Start jogging with the encoder!

---

## 📁 Project Structure

```
Mr1AionCnCJogger/
├── README.md                   ← This file
├── WIRING.md                   ← Complete wiring guide
├── docs/
│   └── WIRING_GUIDE.html       ← Printable PDF version
├── esp32_pendant_serial_universal/
│   ├── esp32_pendant_two_button.ino  ← 4-button firmware (recommended)
│   └── esp32_pendant_single_button.ino
├── backup_pendant.ino          ← Single-button version
└── windows-app/
    ├── src/main-universal.js   ← Electron main process
    ├── dist/                   ← Built executables
    └── README.md               ← Windows app documentation
```

---

## 🔧 Alternative Firmware Versions

### Two-Button Version
Located in `esp32_pendant_serial_universal/esp32_pendant_two_button.ino`:

| Component | GPIO |
|-----------|------|
| Button 1 | GPIO25 |
| Button 2 | GPIO4 |
| Toggle Switch | GPIO27 |
| E-Stop (optional) | GPIO14 |

**Control scheme:**
- Button 1 short = Axis, Button 1 long = Distance
- Button 2 short = Speed, Button 2 long = WCS/MCS toggle

---

## ⚙️ Build Options

### Battery Mode
Uncomment in firmware for wireless operation:
```cpp
#define BATTERY_MODE
```
- 600mAh LiPo battery
- 48-72 hour runtime
- Auto-sleep power management

### Bluetooth Mode (Experimental)
```cpp
#define BLUETOOTH_MODE
```
- BLE HID keyboard emulation
- ~20ms wireless latency
- Works without Windows app

---

## 📡 Serial Protocol

Communication at **115200 baud**:

```
Movement Commands:
  KEY:LEFT,1 / KEY:LEFT,0     (X axis)
  KEY:UP,1 / KEY:DOWN,0       (Y axis)
  KEY:PGUP,1 / KEY:PGDN,0     (Z axis)

Status:
  PENDANT:READY               (boot complete)
  ARM:ENABLE / ARM:DISABLE    (safety state)

Display Control:
  LCD:SOLID,R,G,B             (background color)
  LCD:WHEEL,HUE               (RGB wheel mode)
```

---

## 🛡️ Safety Features

- **Arming Required**: Keys only sent when armed by Windows app
- **Software Detection**: Pendant inactive unless CNC software running
- **Heartbeat**: Auto-disarm on connection loss
- **Force Key Release**: All keys released on disconnect

---

## 📄 License

Private / Closed Source

---

## 🔗 Links

- [Wiring Guide](WIRING.md)
- [Windows App Documentation](windows-app/README.md)
- [Alternative Firmware](esp32_pendant_serial_universal/README.md)
