# AIONMECH Pendant Firmware Versions

This directory contains two firmware versions for the AIONMECH CNC Pendant, allowing you to choose the control scheme that best fits your needs.

## Available Versions

### 1. Single Button Version (`esp32_pendant_single_button.ino`)

**Hardware Requirements:**
- 1 control button (GPIO25)
- Encoder + LCD + Toggle switch

> **Note:** The single-button variant does **not** include E-stop support. Use the two-button version if you need hardware E-stop.

**Control Scheme:**
- **1 click:** Change Axis (X → Y → Z)
- **2 clicks:** Change Distance (Fine → Small → Medium → Large)
- **3 clicks:** Change Speed/Feed Rate
- **4 clicks:** Change Theme Variant
- **5+ clicks:** Show Help Screen
- **Hold 2 seconds:** Change Machine Type (FireControl, CutControl, Mach3, etc.)
- **6 clicks:** Toggle WCS/MCS View

**Best For:**
- Minimal hardware complexity
- Clean, simple pendant design
- Users comfortable with multi-click patterns

### 2. Two Button Version (`esp32_pendant_two_button.ino`)

**Hardware Requirements:**
- 2 control buttons (GPIO25, GPIO4)
- Encoder + LCD + Toggle switch + E-stop

**Control Scheme:**
- **Button 1 (short press):** Change Axis
- **Button 1 (long press 2s):** Change Jog Distance
- **Button 2 (short press):** Change Speed/Feed Rate
- **Button 2 (long press 2s):** Toggle WCS/MCS View

**Best For:**
- More intuitive button mapping
- Faster access to common functions
- Users who prefer dedicated button functions

## Common Features (Both Versions)

- **Encoder:** Jog movement on selected axis
- **Toggle Switch:** Power/Sleep management
- **E-Stop:** Emergency stop safety system
- **LCD Display:** 240x240 GC9A1 round display
- **Universal CNC Support:** FireControl, CutControl, Mach3/4, LinuxCNC, UCCNC, Carbide Motion, UGS
- **Live Position Display:** Real-time WCS/MCS coordinates
- **Theme Support:** Multiple color themes
- **Serial Protocol:** 115200 baud USB communication

## Pin Mapping

| Component | GPIO Pin | Single Button | Two Button |
|-----------|----------|---------------|------------|
| Encoder A | 32 | ✓ | ✓ |
| Encoder B | 33 | ✓ | ✓ |
| Button 1/Main | 25 | ✓ | ✓ |
| Button 2 | 4 | - | ✓ |
| Toggle Switch | 27 | ✓ | ✓ |
| E-Stop | 14 | - | ✓ |
| LCD SCLK | 18 | ✓ | ✓ |
| LCD MOSI | 23 | ✓ | ✓ |
| LCD CS | 15 | ✓ | ✓ |
| LCD DC | 2 | ✓ | ✓ |
| LCD RST | 21 | ✓ | ✓ |

> **Note:** Button 2 uses GPIO4 (not GPIO26) to avoid conflict with LCD CS pin.
> Toggle Switch uses GPIO27 (not GPIO15) to avoid conflict with LCD CS.

## Installation

1. **Choose your version** based on your hardware and preference
2. **Open the .ino file** in Arduino IDE
3. **Select ESP32 board** (ESP32 Dev Module)
4. **Install required libraries:**
   - LovyanGFX (for LCD support)
5. **Upload firmware** to your ESP32
6. **Connect to host software** via USB serial (115200 baud)

## Build Options

Both versions support optional build switches:

```cpp
// #define BATTERY_MODE     // Enable battery power management
// #define BLUETOOTH_MODE   // Enable BLE HID functionality (experimental)
```

## Host Software Compatibility

Both firmware versions work with:
- **FireControl** (Langmuir Systems plasma tables)
- **CutControl** (Langmuir Systems mills/routers)
- **Mach3/Mach4** (Artsoft CNC software)
- **LinuxCNC** (Open-source CNC control)
- **UCCNC** (CNCdrive software)
- **Carbide Motion** (Carbide 3D)
- **Universal G-Code Sender** (UGS)
- **Manual mode** (standalone operation)

## Safety Features

- **E-Stop Integration:** Hardware emergency stop with proper debouncing
- **Motion Safety Gates:** All movement commands blocked when E-stop active
- **Host Communication:** Heartbeat monitoring and timeout protection
- **System Lock:** Complete motion blocking during emergency conditions

## Version Selection Guide

**Choose Single Button if:**
- You want the simplest hardware design
- You're comfortable with multi-click patterns
- You prefer a clean, minimal pendant
- You want to reduce component count

**Choose Two Button if:**
- You want faster access to functions
- You prefer dedicated button functions
- You're building multiple pendants for different users
- You want the most intuitive operation

## Technical Support

For technical support, wiring questions, or firmware issues:
- Check the `WIRING.md` file for hardware connections
- Review the main `README.md` for quick start instructions

## Version History

- **v2.0.0-SINGLE-BUTTON:** Single button control implementation
- **v2.0.0-TWO-BUTTON:** Two button control implementation
- **v2.0.0-UNIVERSAL:** Full universal multi-button version