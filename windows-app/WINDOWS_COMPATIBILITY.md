# AIONMECH Pendant Controller - Windows Compatibility Guide

## Supported Systems

- **Windows 10** (1903 or later) - All editions
- **Windows 11** - All editions
- **Architecture:** x64 (64-bit), x86 (32-bit), ARM64
- **Memory:** 2GB RAM minimum, 4GB recommended
- **Display:** Any resolution from 800x600 to 4K+

## Hardware Compatibility

- **Graphics:** Intel HD, AMD, NVIDIA (any DirectX 11+ GPU); automatic fallback if drivers are limited
- **CPU:** Intel, AMD, and Windows on ARM (ARM64) devices
- **USB:** Any system with USB 2.0+ ports for ESP32 connection
- **DPI Scaling:** Automatic support for 100%, 125%, 150%, 200%+ scaling

## Installation Options

### Option 1: Portable (Recommended)

Download and run — no installation required:

```
AIONMECH Pendant Controller-Portable-v2.0.0-x64.exe     # 64-bit systems
AIONMECH Pendant Controller-Portable-v2.0.0-ia32.exe    # 32-bit systems
```

### Option 2: Full Installer

Installs to Program Files with desktop/start menu shortcuts:

```
AIONMECH Pendant Controller-v2.0.0-x64.exe
AIONMECH Pendant Controller-v2.0.0-ia32.exe
```

## Compatibility Features

### Automatic Hardware Detection
- GPU acceleration auto-disabled on low-end hardware
- Memory usage optimized for systems with <4GB RAM
- DPI scaling automatically adjusted for high-DPI displays
- Responsive UI adapts to any screen resolution

### Fallback Modes
- Software rendering if GPU acceleration fails
- USB-only keyboard simulation if robotjs fails to load
- Safe mode startup for problematic hardware configurations

### Performance by System Tier

| Tier | Specs | Experience |
|------|-------|------------|
| **High-End** | 8GB+ RAM, dedicated GPU | Full hardware acceleration, smooth animations, 4K support |
| **Mid-Range** | 4-8GB RAM, integrated GPU | Automatic GPU acceleration with fallbacks, full functionality |
| **Low-End** | 2-4GB RAM, older hardware | Software rendering, reduced visual effects, core features work |

## Troubleshooting

### App won't start
```bash
# Try safe mode (disable GPU acceleration)
"AIONMECH Pendant Controller.exe" --disable-gpu
```

### Graphics look corrupted
```bash
# Force software rendering
"AIONMECH Pendant Controller.exe" --disable-gpu --disable-software-rasterizer
```

## System Requirements

### Minimum
- Windows 10 version 1903+
- 2GB RAM
- 100MB disk space
- USB 2.0 port
- 800x600 display

### Recommended
- Windows 10/11 latest version
- 4GB+ RAM
- DirectX 11+ graphics
- USB 3.0 port
- 1080p+ display

## Universal Compatibility

This app is designed to work on ANY Windows 10/11 system, from budget laptops to high-end workstations, with automatic hardware detection and graceful fallbacks for maximum compatibility.
