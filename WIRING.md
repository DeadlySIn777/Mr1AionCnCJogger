# 🔌 AIONMECH CNC Pendant - Complete Wiring Guide

## 📦 What You're Building

A professional USB pendant for ANY CNC software with:
- **Round LCD display** (240×240 pixels)
- **Rotary encoder** (precision jogging)
- **2 momentary buttons** (axis, distance, speed controls)
- **1 latching switch** (power/sleep toggle)
- **1 emergency stop** (safety shutdown)

---

# 🎯 PARTS LIST

| Component | Type | Quantity |
|-----------|------|----------|
| ESP32-WROOM 38-pin | USB-C Dev Board | 1 |
| GC9A01 LCD | 240×240 Round SPI Display | 1 |
| Rotary Encoder | 6-pin (A, B, A̅, B̅, OV, VCC) | 1 |
| Momentary Button | Push button (normally open) | 2 |
| Latching Switch | Toggle or rocker switch | 1 |
| Emergency Stop | 19mm mushroom button (NC) | 1 |
| USB-C Cable | Data cable (not charge-only) | 1 |

---

# 📋 COMPLETE WIRING TABLE

## 🖥️ LCD Display (GC9A01)

| LCD Pin | → | ESP32 | Color |
|---------|---|-------|-------|
| VCC | → | **3V3** | 🔴 Red |
| GND | → | **GND** | ⚫ Black |
| SCLK | → | **GPIO18** | 🟡 Yellow |
| MOSI/SDA | → | **GPIO23** | 🔵 Blue |
| CS | → | **GPIO15** | 🟢 Green |
| DC | → | **GPIO2** | 🟠 Orange |
| RST | → | **GPIO21** | 🟣 Purple |
| BL | → | **3V3** | 🔴 Red |

> ⚠️ **CRITICAL**: LCD uses **3V3 ONLY** - 5V will destroy it!

---

## 🔄 Rotary Encoder (6-Pin Industrial)

| Encoder Pin | → | ESP32 | Notes |
|-------------|---|-------|-------|
| **A** | → | **GPIO32** | Phase A signal |
| **B** | → | **GPIO33** | Phase B signal |
| **OV** | → | **GND** | Ground (0V) |
| **VCC** | → | **5V** | Power supply |
| A̅ | → | ❌ | Leave disconnected |
| B̅ | → | ❌ | Leave disconnected |

> ℹ️ Encoder needs **5V power** but ESP32 inputs are 5V tolerant

---

## 🔘 Buttons & Switches

| Component | Type | → | ESP32 | Other Terminal |
|-----------|------|---|-------|----------------|
| **Button 1** | Momentary | → | **GPIO25** | → GND |
| **Button 2** | Momentary | → | **GPIO4** | → GND |
| **Power Switch** | Latching | → | **GPIO27** | → GND |
| **E-Stop** | NC Contact | → | **GPIO14** | → GND |

> All buttons use internal pull-ups. Press = GPIO goes LOW.

---

# 🔌 VISUAL WIRING DIAGRAM

```
                        ESP32-WROOM 38-PIN
                    ┌─────────────────────────┐
                    │       USB-C PORT        │
                    │       ════════════      │
                    ├─────────────────────────┤
         ⚠️ NO! CLK │●                      ●│ 5V ────► ENCODER VCC
                    │●                      ●│
                    │●                      ●│
                    │●                      ●│
        LCD DC  ◄───│● GPIO2           GPIO14│───► E-STOP (NC)
                    │●                 GPIO27│───► POWER SWITCH
                    │●                 GPIO25│───► BUTTON 1
                    │●                  GPIO4│───► BUTTON 2
                    │●                      ●│
        LCD CS  ◄───│● GPIO15          GPIO33│───► ENCODER B
                    │●                 GPIO32│───► ENCODER A
       LCD SCLK ◄───│● GPIO18               ●│
       LCD MOSI ◄───│● GPIO23               ●│
                    │●                      ●│
        LCD RST ◄───│● GPIO21               ●│
                    │●                      ●│
                    │●                      ●│
    LCD GND ◄───────│● GND              3V3 │───► LCD VCC
    ENCODER OV ◄────│● GND              3V3 │───► LCD BL
    ALL BUTTONS ◄───│● GND                  ●│
                    └─────────────────────────┘


    ┌──────────────────────────────────────────────────────────┐
    │                    COMPONENT WIRING                       │
    └──────────────────────────────────────────────────────────┘

    6-PIN ENCODER:              BUTTONS (All same wiring):
    ┌───────────────────┐       ┌─────────────┐
    │ A  B  A̅  B̅  OV VCC│       │    ┌───┐    │
    │ │  │  ×  ×  │   │ │       │    │   │    │
    └─┼──┼────────┼───┼─┘       └────┼───┼────┘
      │  │        │   │              │   │
      │  │        │   └► 5V          │   └──► GND
      │  │        └────► GND         └──────► GPIO
      │  └─────────────► GPIO33
      └────────────────► GPIO32

      × = Not connected


    E-STOP (Normally Closed):
    ┌─────────────────┐
    │   ╔═══════╗     │
    │   ║ STOP  ║     │    NC = Normally Closed
    │   ╚═══════╝     │    Circuit CLOSED when not pressed
    │    NC    COM    │    Circuit OPENS when pressed
    │    │      │     │
    └────┼──────┼─────┘
         │      │
         │      └──────► GND
         └─────────────► GPIO14
```

---

# 🎛️ CONTROL FUNCTIONS

## Button 1 (GPIO25) - Momentary
| Action | Press |
|--------|-------|
| Change Axis (X→Y→Z) | Short press |
| Change Jog Distance | Long press (2 sec) |

## Button 2 (GPIO4) - Momentary
| Action | Press |
|--------|-------|
| Change Speed | Short press |
| Toggle WCS/MCS | Long press (2 sec) |

## Power Switch (GPIO27) - Latching
| Position | State |
|----------|-------|
| ON | Pendant active |
| OFF | Sleep mode (5-sec countdown) |

## E-Stop (GPIO14) - Emergency
| State | Action |
|-------|--------|
| Released | Normal operation |
| Pressed | **ALL MOTION BLOCKED** |

## Rotary Encoder
| Action | Result |
|--------|--------|
| Rotate CW | Jog + direction |
| Rotate CCW | Jog − direction |

---

# ⚠️ SAFETY WARNINGS

## 🚫 NEVER Connect These:

| Pin | Danger |
|-----|--------|
| **CLK** (top-left) | Flash pin - will brick ESP32 |
| **5V to LCD** | Will destroy the display! |
| **GPIO0** | Boot mode - causes boot failure |
| **GPIO6-11** | Flash memory pins |
| **GPIO12** | Boot strap - causes boot failure |

## ✅ Power Rules:

| Component | Voltage |
|-----------|---------|
| LCD (VCC, BL) | **3V3 ONLY** |
| Encoder (VCC) | **5V** |
| Buttons | No power needed (internal pullup) |

---

# ✅ PRE-POWER CHECKLIST

Before connecting USB:

- [ ] LCD VCC → 3V3 (**NOT 5V!**)
- [ ] LCD GND → GND
- [ ] Encoder VCC → 5V
- [ ] Encoder OV → GND  
- [ ] Encoder A → GPIO32
- [ ] Encoder B → GPIO33
- [ ] Encoder A̅ and B̅ → **NOT CONNECTED**
- [ ] Button 1 → GPIO25 to GND
- [ ] Button 2 → GPIO4 to GND
- [ ] Power Switch → GPIO27 to GND
- [ ] E-Stop NC → GPIO14 to GND
- [ ] No wires on CLK, GPIO0, GPIO6-11

---

# 🚀 FIRST POWER ON

1. **Connect USB-C** to your PC
2. **Open Serial Monitor** at 115200 baud
3. **Look for**: `PENDANT:READY`
4. **Test encoder** - rotate and watch for jog commands
5. **Test buttons** - press and watch LCD change
6. **Test E-Stop** - should show red screen when pressed

---

# 🔧 TROUBLESHOOTING

| Problem | Solution |
|---------|----------|
| Black LCD screen | Check 3V3 power and SPI wiring |
| No serial output | Verify baud rate (115200) |
| Encoder no response | Check A→GPIO32, B→GPIO33, 5V power |
| Buttons no response | Check GPIO→GND wiring |
| E-Stop always active | Verify NC contact (should be closed normally) |
| ESP32 won't boot | Remove wires from GPIO0, GPIO12 |

---

# 📁 FIRMWARE FILES

| File | Use For |
|------|---------|
| `esp32_pendant_serial_universal/esp32_pendant_two_button.ino` | **This 4-button setup** |
| `backup_pendant.ino` | Single-button version |

---

**🛠️ Questions? Check serial output at 115200 baud for debug info!**
