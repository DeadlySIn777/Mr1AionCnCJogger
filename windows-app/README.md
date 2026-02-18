# AIONMECH Universal CNC Pendant - Windows Application

A Windows desktop app that bridges your ESP32 pendant to **any CNC software** via keyboard simulation.

## ✅ Supported CNC Software

| Software | Auto-Detect | Status |
|----------|-------------|--------|
| **FireControl** | ✅ Yes | Langmuir Plasma Tables |
| **CutControl** | ✅ Yes | Langmuir MR-1 Mill |
| **Mach3** | ✅ Yes | Artsoft CNC |
| **Mach4** | ✅ Yes | Artsoft CNC |
| **LinuxCNC** | ✅ Yes | Open-source CNC |
| **UCCNC** | ✅ Yes | CNCdrive |
| **Carbide Motion** | ✅ Yes | Shapeoko/Nomad |
| **Universal G-Code Sender** | ✅ Yes | GRBL-based |
| **OpenBuilds CONTROL** | ✅ Yes | OpenBuilds machines |
| **CNCjs** | ✅ Yes | Web-based CNC |
| **Manual Mode** | Always | Any software |

## 🚀 Quick Start

### Option 1: Download Pre-Built EXE (Recommended)
1. Download `AIONMECH Pendant Controller-v2.0.0-x64.exe` from `dist/`
2. Run the installer
3. Connect your ESP32 pendant via USB-C
4. Launch the app - it auto-detects your pendant!

### Option 2: Run from Source
```powershell
cd windows-app
npm install
npm start
```

### Option 3: Build Your Own EXE
```powershell
cd windows-app
npm install
npm run build-universal
```
Output: `dist/AIONMECH Pendant Controller-v2.0.0-x64.exe`

## 🔧 How It Works

```
┌─────────────┐      USB-C      ┌─────────────┐      Keyboard      ┌─────────────┐
│   ESP32     │ ◄─────────────► │  Windows    │ ◄────────────────► │ CNC Software│
│   Pendant   │   Serial 115200 │    App      │   Key Simulation   │  (Any!)     │
└─────────────┘                 └─────────────┘                    └─────────────┘
```

1. **Pendant sends commands** via serial (e.g., `KEY:LEFT,1`)
2. **App detects CNC software** running on your PC
3. **App simulates keypress** (Left Arrow) to jog the machine
4. **Safety**: Keys only work when CNC software is detected and focused

## 🎮 Features

- **🔌 Auto-Connect**: Finds ESP32 automatically
- **🔍 Auto-Detect**: Identifies which CNC software is running
- **🛡️ Safety Handshake**: Keys only simulated when CNC software active
- **💓 Heartbeat**: Connection monitoring with auto-reconnect
- **🎨 Theme Control**: Change pendant LCD colors from the app
- **📍 System Tray**: Runs quietly in background
- **🚀 Auto-Start**: Optional Windows startup

## ⌨️ Keyboard Shortcuts (Universal)

| Action | Key | Works With |
|--------|-----|------------|
| Jog X+ | Right Arrow | All |
| Jog X− | Left Arrow | All |
| Jog Y+ | Up Arrow | All |
| Jog Y− | Down Arrow | All |
| Jog Z+ | Page Up | All |
| Jog Z− | Page Down | All |
| Pause | Space | All |
| Start/Resume | Alt+R | Langmuir |
| Stop | Alt+S | Langmuir |
| Distance 1-4 | 1, 2, 3, 4 | CutControl |
| Feed Rate | F1-F4 | CutControl |

## 🔒 Safety Features

1. **Armed/Disarmed State**: Pendant must be armed before keys work
2. **Software Detection**: Keys only sent when CNC software is running
3. **Heartbeat Timeout**: Pendant disarms if app disconnects
4. **Force Key Release**: All keys released on disconnect/error
5. **No Accidental Jogs**: Keys blocked when window not focused

## 🐛 Troubleshooting

### Pendant not detected?
- Check USB cable (must be data cable, not charge-only)
- Install CP210x or CH340 drivers
- Try different USB port

### Keys not working?
- Ensure CNC software is running AND focused
- Check app logs: View → Developer Tools → Console
- RobotJS may need rebuild: `npm rebuild robotjs`

### Blank window / GPU issues?
```powershell
npm run dev -- --disable-gpu
```

### Want verbose logging?
```powershell
$env:DEBUG="*"; npm start
```

## 📁 File Structure

```
windows-app/
├── src/
│   ├── main-universal.js    # Electron main process
│   ├── renderer-universal.js # UI logic
│   └── renderer-universal.html # UI layout
├── assets/                   # Icons and images
├── dist/                     # Built executables
├── package.json              # Dependencies
└── README.md                 # This file
```

## 🔨 Development

```powershell
# Install dependencies
npm install

# Run in development mode
npm start

# Run with DevTools open
npm run dev

# Build for distribution
npm run build-universal

# Build portable EXE (no installer)
npm run build-portable
```

## 📋 System Requirements

- **OS**: Windows 10/11 (x64)
- **RAM**: 2GB minimum
- **USB**: Available USB port for ESP32
- **CNC Software**: Any supported software (see list above)

## 📄 License

MIT License - AIONMECH © 2025-2026
