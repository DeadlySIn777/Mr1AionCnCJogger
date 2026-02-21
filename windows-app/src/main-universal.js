/*
  UNIVERSAL AIONMECH CNC PENDANT CONTROLLER - Windows App
  Supports ALL CNC machines with auto-detection:
  
  🔥 PLASMA CUTTING MACHINES:
  - CrossFire CNC Plasma (Gen2 with FireControl)
  - CrossFire PRO CNC Plasma (FireControl)
  - CrossFire XR CNC Plasma (FireControl)
  
  🔧 MILLING MACHINES:
  - MR-1 Gantry Mill (CutControl)

  Universal Windows 10/11 support with automatic hardware detection
  Responsive UI for any resolution and DPI scaling
*/

const { app, BrowserWindow, Tray, Menu, ipcMain, dialog, nativeImage, shell } = require('electron');
// const { autoUpdater } = require('electron-updater'); // DISABLED - Can cause crashes
const path = require('path');
const os = require('os');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const { exec } = require('child_process');

// Optional robotjs import for testing - handle gracefully on all hardware
let robot = null;
try {
  // Only load robotjs if explicitly needed and compatible
  if (process.platform === 'win32' && (process.arch === 'x64' || process.arch === 'ia32')) {
    try {
      robot = require('robotjs');
      console.log('RobotJS loaded successfully - Full keyboard simulation enabled');
    } catch (robotError) {
      console.log('RobotJS not available on this system - using fallback keyboard simulation');
      robot = null;
    }
  } else {
    console.log('RobotJS not loaded - platform/arch compatibility mode');
  }
} catch (error) {
  console.log('RobotJS not available; keyboard simulation disabled:', error.message);
}

const AutoLaunch = require('auto-launch');

// Hardware compatibility and GPU fallback settings
const isLowEndHardware = os.totalmem() < 4 * 1024 * 1024 * 1024; // Less than 4GB RAM
const isOldCPU = process.arch === 'ia32'; // 32-bit architecture indicates older system

// Apply command line switches for maximum compatibility
if (isLowEndHardware || isOldCPU) {
  app.commandLine.appendSwitch('disable-gpu-sandbox');
  app.commandLine.appendSwitch('disable-software-rasterizer');
  app.commandLine.appendSwitch('disable-features', 'VizDisplayCompositor');
  console.log('Low-end hardware detected: Applied compatibility switches');
}

// Enhanced GPU crash prevention
app.commandLine.appendSwitch('disable-gpu-driver-bug-workarounds'); 
app.commandLine.appendSwitch('disable-background-timer-throttling');
app.commandLine.appendSwitch('disable-renderer-backgrounding');
app.commandLine.appendSwitch('disable-backgrounding-occluded-windows');

// Universal compatibility switches for all hardware
app.commandLine.appendSwitch('ignore-gpu-blacklist');
app.commandLine.appendSwitch('enable-webgl-draft-extensions');
app.commandLine.appendSwitch('enable-webgl2-compute-context');

// Fallback mode for problematic graphics drivers
if (process.argv.includes('--safe-mode') || process.argv.includes('--disable-gpu')) {
  app.commandLine.appendSwitch('disable-gpu');
  app.commandLine.appendSwitch('disable-gpu-compositing');
  console.log('Safe mode: GPU acceleration disabled');
}

// Global variables
let mainWindow = null;
let tray = null;
let serialPort = null;
let currentPort = null;
let isConnected = false;
let currentMachine = 'MANUAL';
let currentSoftware = 'NONE';
let currentUnits = 'MM'; // Default to metric
let unitCheckInterval = null;
let machineDetectionInterval = null;
// Timer used by scheduleReconnect(); ensure it's defined
let reconnectTimer = null;
// Heartbeat timer to keep pendant armed
let heartbeatInterval = null;
// Tray menu update interval
let trayUpdateInterval = null;
// Track currently pressed keys to be able to force-release on errors/disconnect
const pressedKeys = new Set();
let driverPromptShown = false;
let lastDriverPromptAt = 0;

function execAsync(command) {
  return new Promise((resolve) => {
    exec(command, { windowsHide: true }, (error, stdout, stderr) => {
      resolve({ error, stdout: stdout || '', stderr: stderr || '' });
    });
  });
}

async function maybePromptCp210xDriverInstall(ports) {
  if (process.platform !== 'win32') return;

  const now = Date.now();
  if (driverPromptShown && (now - lastDriverPromptAt) < 60000) return;

  const hasKnownSerialPort = ports.some((port) =>
    port.manufacturer && (
      port.manufacturer.includes('Silicon Labs') ||
      port.manufacturer.includes('FTDI') ||
      port.manufacturer.includes('Prolific') ||
      port.manufacturer.includes('CH340') ||
      port.manufacturer.includes('ESP32')
    )
  );

  if (hasKnownSerialPort) return;

  const pnp = await execAsync('powershell -NoProfile -Command "Get-PnpDevice | Where-Object { $_.InstanceId -like \"USB\\\\VID_10C4*\" } | Select-Object -First 1 -ExpandProperty Status"');
  const hasCp210xHardware = (pnp.stdout || '').trim().length > 0;

  const drivers = await execAsync('pnputil /enum-drivers | findstr /I "silicon labs cp210 cp210x"');
  const hasCp210xDriver = (drivers.stdout || '').trim().length > 0;

  if (!hasCp210xHardware && hasCp210xDriver) return;
  if (!hasCp210xHardware && !hasCp210xDriver) return;

  driverPromptShown = true;
  lastDriverPromptAt = now;

  const result = await dialog.showMessageBox(mainWindow || null, {
    type: 'warning',
    title: 'ESP32 Driver Required',
    message: 'CP210x USB driver appears missing or not fully installed.',
    detail: 'The pendant uses a Silicon Labs CP210x USB-UART bridge.\n\nClick Install Driver to open the official driver download page, then unplug/replug the pendant after installation.',
    buttons: ['Install Driver', 'Open Device Manager', 'Ignore'],
    defaultId: 0,
    cancelId: 2,
    noLink: true
  });

  if (result.response === 0) {
    await execAsync('pnputil /scan-devices');
    await shell.openExternal('https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers');
    await shell.openExternal('ms-settings:windowsupdate');
  } else if (result.response === 1) {
    await execAsync('start devmgmt.msc');
  }
}

// Auto-launch setup with AIONMECH branding
const autoLauncher = new AutoLaunch({
  name: 'AIONMECH CNC Pendant',
  path: app.getPath('exe'),
});

// Windows shell integration: AppUserModelID for notifications/jumplists
try {
  app.setAppUserModelId('com.aionmech.pendant-controller');
} catch (_) {}

// Enforce single instance
const gotLock = app.requestSingleInstanceLock();
if (!gotLock) {
  app.quit();
} else {
  app.on('second-instance', () => {
    if (mainWindow) {
      if (!mainWindow.isVisible()) mainWindow.show();
      if (mainWindow.isMinimized()) mainWindow.restore();
      mainWindow.focus();
    }
  });
}

// Supported CNC software with process names and detection
const cncSoftware = {
  // Langmuir Systems
  'FireControl': {
    processName: 'FireControl.exe',
    machineType: 'FIRECONTROL',
    displayName: 'FireControl (Plasma)',
    machines: ['CrossFire', 'CrossFire PRO', 'CrossFire XR']
  },
  'CutControl': {
    processName: 'CutControl.exe', 
    machineType: 'CUTCONTROL',
    displayName: 'CutControl (Mill)',
    machines: ['MR-1']
  },
  // Industrial CNC
  'Mach3': {
    processName: 'Mach3.exe',
    machineType: 'MACH3',
    displayName: 'Mach3',
    machines: ['Generic CNC']
  },
  'Mach4': {
    processName: 'Mach4GUI.exe',
    machineType: 'MACH4',
    displayName: 'Mach4',
    machines: ['Generic CNC']
  },
  'LinuxCNC': {
    processName: 'linuxcnc.exe',  // Wine/WSL
    machineType: 'LINUXCNC',
    displayName: 'LinuxCNC',
    machines: ['Generic CNC']
  },
  'UCCNC': {
    processName: 'UCCNC.exe',
    machineType: 'UCCNC',
    displayName: 'UCCNC',
    machines: ['Generic CNC']
  },
  // Hobby/Prosumer
  'CarbideMotion': {
    processName: 'Carbide Motion.exe',
    machineType: 'CARBIDE',
    displayName: 'Carbide Motion',
    machines: ['Shapeoko', 'Nomad']
  },
  'UGS': {
    processName: 'ugsplatform.exe',
    machineType: 'UGS',
    displayName: 'Universal G-Code Sender',
    machines: ['GRBL']
  },
  'OpenBuilds': {
    processName: 'OpenBuilds-CONTROL.exe',
    machineType: 'OPENBUILDS',
    displayName: 'OpenBuilds CONTROL',
    machines: ['OpenBuilds']
  },
  'CNCjs': {
    processName: 'cncjs.exe',
    machineType: 'CNCJS',
    displayName: 'CNCjs',
    machines: ['GRBL', 'Marlin']
  }
};

// Universal keyboard mappings - compatible with ALL CNC software
const universalKeyMappings = {
  // Common movement controls (all machines)
  'LEFT': 'left',        // Jog X−: Left Arrow
  'RIGHT': 'right',      // Jog X+: Right Arrow  
  'UP': 'up',            // Jog Y+: Up Arrow
  'DOWN': 'down',        // Jog Y−: Down Arrow
  'PGUP': 'pageup',      // Jog Z+: Page Up
  'PGDN': 'pagedown',    // Jog Z−: Page Down
  
  // Program controls (FireControl & CutControl)
  'SPACE': 'space',      // Pause Program: Space
  'TAB': 'tab',          // Toggle Nudge Jog: Tab (FireControl & CutControl)
  'ALT_R': ['alt', 'r'], // Start/Resume Program: Alt+R
  'ALT_S': ['alt', 's'], // Stop Program: Alt+S
  
  // CutControl-specific controls
  '1': '1',              // Jog Distance 0.1mm: 1 key
  '2': '2',              // Jog Distance 1mm: 2 key
  '3': '3',              // Jog Distance 10mm: 3 key
  '4': '4',              // Jog Distance 100mm: 4 key
  'F1': 'f1',            // Feed Rate 25%: F1 key
  'F2': 'f2',            // Feed Rate 50%: F2 key
  'F3': 'f3',            // Feed Rate 75%: F3 key
  'F4': 'f4',            // Feed Rate 100%: F4 key
};

// Machine detection and process checking
function detectCncSoftware() {
  return new Promise((resolve) => {
    // Build findstr pattern from ALL known CNC process names
    const processNames = Object.values(cncSoftware).map(info => {
      // Extract base name without .exe for broader matching
      return info.processName.replace('.exe', '');
    });
    const findstrPattern = processNames.join(' ');
    
    exec(`tasklist /fo csv | findstr /i "${findstrPattern}"`, (error, stdout) => {
      if (error) {
        resolve({ detected: false, software: 'NONE', machine: 'MANUAL' });
        return;
      }
      
      const processes = stdout.toLowerCase();
      
      for (const [name, info] of Object.entries(cncSoftware)) {
        if (processes.includes(info.processName.toLowerCase())) {
          console.log(`Detected ${info.displayName} running`);
          resolve({ 
            detected: true, 
            software: name, 
            machine: info.machineType,
            displayName: info.displayName 
          });
          return;
        }
      }
      
      resolve({ detected: false, software: 'NONE', machine: 'MANUAL' });
    });
  });
}

// Send machine detection to pendant
function updatePendantMachine(machineType) {
  if (serialPort && serialPort.isOpen) {
    serialPort.write(`MACHINE:${machineType}\n`);
    serialPort.write(`SOFTWARE:${currentSoftware !== 'NONE' ? 'CONNECTED' : 'DISCONNECTED'}\n`);
    console.log(`Updated pendant: ${machineType} (${currentSoftware})`);
  }
}

// Enhanced key simulation with machine awareness
function simulateKey(key, isPressed) {
  if (!robot) {
    console.log(`Key simulation: ${key} ${isPressed ? 'pressed' : 'released'} (robot disabled)`);
    return;
  }

  try {
    const mapping = universalKeyMappings[key];
    if (!mapping) {
      console.warn(`Unknown key mapping: ${key}`);
      return;
    }

    if (Array.isArray(mapping)) {
      // Multi-key combination (e.g., Alt+R)
      if (isPressed) {
        robot.keyToggle(mapping[0], 'down');
        robot.keyToggle(mapping[1], 'down');
        pressedKeys.add(key);
      } else {
        robot.keyToggle(mapping[1], 'up');
        robot.keyToggle(mapping[0], 'up');
        pressedKeys.delete(key);
      }
    } else {
      // Single key
      robot.keyToggle(mapping, isPressed ? 'down' : 'up');
      if (isPressed) {
        pressedKeys.add(key);
      } else {
        pressedKeys.delete(key);
      }
    }
    
    console.log(`Key simulated: ${key} ${isPressed ? 'pressed' : 'released'} (${currentMachine})`);
  } catch (error) {
    console.error(`Key simulation error: ${error.message}`);
  }
}

// Force-release any keys still marked as pressed (safety on disconnect/errors)
function releaseAllPressedKeys() {
  if (!robot || pressedKeys.size === 0) return;
  try {
    for (const key of Array.from(pressedKeys)) {
      const mapping = universalKeyMappings[key];
      if (!mapping) continue;
      if (Array.isArray(mapping)) {
        // Release combo in reverse order
        robot.keyToggle(mapping[1], 'up');
        robot.keyToggle(mapping[0], 'up');
      } else {
        robot.keyToggle(mapping, 'up');
      }
      console.log(`Safety: forced release for ${key}`);
      pressedKeys.delete(key);
    }
  } catch (e) {
    console.warn('Safety: error while releasing pressed keys:', e.message);
  }
}

// Enhanced serial communication handler
function handleSerialCommand(command) {
  command = command.trim();
  console.log(`Pendant command: ${command}`);

  if (command.startsWith('KEY:')) {
    // KEY:LEFT,1 or KEY:ALT_R,0 format
    const parts = command.substring(4).split(',');
    if (parts.length === 2) {
      // Safety: only allow keys when a supported CNC software is active
      if (currentSoftware === 'NONE') {
        console.warn('Safety: Ignoring KEY command (no CNC software detected)');
        return;
      }
      const key = parts[0];
      const pressed = parts[1] === '1';
      simulateKey(key, pressed);
    }
  }
  else if (command.startsWith('MACHINE:')) {
    // Machine change notification from pendant
    const machine = command.substring(8);
    currentMachine = machine;
    console.log(`Pendant set machine to: ${machine}`);
    
    // Update UI
    if (mainWindow) {
      mainWindow.webContents.send('machine-changed', machine);
    }
  }
  else if (command.startsWith('AXIS:')) {
    // Axis change notification
    const axis = command.substring(5);
    console.log(`Pendant axis changed to: ${axis}`);
    
    if (mainWindow) {
      mainWindow.webContents.send('axis-changed', axis);
    }
  }
  else if (command.startsWith('DISTANCE:')) {
    // Jog distance change notification
    const distance = command.substring(9);
    console.log(`Pendant jog distance changed to: ${distance}`);
    
    if (mainWindow) {
      mainWindow.webContents.send('distance-changed', distance);
    }
  }
  else if (command.startsWith('FEEDRATE:')) {
    // Feed rate change notification
    const feedrate = command.substring(9);
    console.log(`Pendant feedrate changed to: ${feedrate}%`);
    
    if (mainWindow) {
      mainWindow.webContents.send('feedrate-changed', feedrate);
    }
  }
  else if (command.startsWith('COORD:VIEW,') || command.startsWith('COORD:VIEW:')) {
    // Coordinate view change notification (accept both comma and colon delimiters)
    const delimPos = command.indexOf('VIEW') + 4;
    const view = command.substring(delimPos + 1);
    console.log(`Pendant coordinate view changed to: ${view}`);
    
    if (mainWindow) {
      mainWindow.webContents.send('coord-view-changed', view);
    }
  }
  else if (command.startsWith('POWER:')) {
    // Power state change notification
    const state = command.substring(6);
    console.log(`Pendant power state: ${state}`);
    
    if (mainWindow) {
      mainWindow.webContents.send('power-state-changed', state);
    }
  }
  else if (command.startsWith('ESTOP:')) {
    // Emergency stop notifications
    const estopEvent = command.substring(6);
    console.log(`EMERGENCY STOP: ${estopEvent}`);
    
    if (mainWindow) {
      mainWindow.webContents.send('estop-event', estopEvent);
    }
    
    // Handle specific E-stop events
    if (estopEvent === 'PRESSED' || estopEvent === 'EMERGENCY_STOP_ACTIVATED') {
      console.log('⚠️  EMERGENCY STOP ACTIVATED - All motion blocked!');
      
      // Send emergency stop to CNC software based on current detection
      if (currentSoftware === 'FireControl') {
        // FireControl emergency stop
        if (serialPort && serialPort.isOpen) {
          try {
            serialPort.write('!!\n');  // FireControl emergency stop
          } catch (e) {
            console.error('Failed to send FireControl emergency stop:', e);
          }
        }
      } else if (currentSoftware === 'CutControl') {
        // CutControl emergency stop  
        if (serialPort && serialPort.isOpen) {
          try {
            serialPort.write('ESTOP\n');  // CutControl emergency stop
          } catch (e) {
            console.error('Failed to send CutControl emergency stop:', e);
          }
        }
      }
      // Note: Other CNC software E-stop handled by pendant firmware
      
    } else if (estopEvent === 'RELEASED' || estopEvent === 'EMERGENCY_STOP_RELEASED') {
      console.log('✅ Emergency stop released - System ready for manual reset');
    }
  }
  else if (command === 'PENDANT:READY') {
    console.log('Pendant ready - sending current machine type');
    updatePendantMachine(currentMachine);
    // Re-establish safety handshake on pendant restart
    if (serialPort && serialPort.isOpen) {
          try {
            serialPort.write('HOST:HELLO\n');
              if (currentSoftware === 'CutControl' || currentSoftware === 'FireControl') serialPort.write('ARM:ENABLE\n');
              else serialPort.write('ARM:DISABLE\n');
          } catch (_) {}
    }
  }
  else if (command.startsWith('FIRMWARE:')) {
    // Firmware version/type identification: FIRMWARE:SINGLE_BUTTON or FIRMWARE:TWO_BUTTON
    const firmwareType = command.substring(9);
    console.log(`Pendant firmware detected: ${firmwareType}`);
    
    if (mainWindow) {
      mainWindow.webContents.send('firmware-detected', firmwareType);
    }
    
    // Auto-configure interface based on firmware capabilities
    if (firmwareType.includes('SINGLE')) {
      console.log('Single-button firmware: Simplified controls active');
    } else if (firmwareType.includes('TWO')) {
      console.log('Two-button firmware: Full controls active');
    }
  }
  else if (command.startsWith('POS:')) {
    // POS:X,12.3456MM or POS:Y,1.2345IN
    // Extract axis, value, units
    try {
      const payload = command.substring(4);
      const [axis, valueWithUnits] = payload.split(',');
      if (axis && valueWithUnits) {
        // Strip units suffix (IN/MM) if present
        const val = parseFloat(valueWithUnits);
        if (!isNaN(val) && mainWindow) {
          mainWindow.webContents.send('position-update', { axis, value: val });
        }
      }
    } catch (_) {}
  }
  else if (command.startsWith('ENCODER_SCALE_SET:')) {
    const scaleStr = command.substring('ENCODER_SCALE_SET:'.length);
    const scale = parseInt(scaleStr, 10);
    if (!isNaN(scale) && mainWindow) {
      mainWindow.webContents.send('encoder-scale', scale);
    }
  }
  else if (command.startsWith('POSITION:KNOWN,')) {
    const known = command.endsWith('TRUE');
    if (mainWindow) mainWindow.webContents.send('position-known', known);
  }
}

// Serial port management with auto-reconnection
function connectSerial() {
  SerialPort.list().then(ports => {
    const espPort = ports.find(port => 
      port.manufacturer && (
        port.manufacturer.includes('Silicon Labs') ||
        port.manufacturer.includes('FTDI') ||
        port.manufacturer.includes('Prolific') ||
        port.manufacturer.includes('CH340') ||
        port.manufacturer.includes('ESP32')
      ) && port.path && (
        // Avoid common mouse/keyboard COM ports by checking patterns
        !port.path.includes('COM1') && // Often legacy mouse/keyboard
        !port.path.includes('COM2') && // Often legacy mouse/keyboard
        !(port.manufacturer.includes('Microsoft') || 
          port.manufacturer.includes('Logitech') ||
          port.manufacturer.includes('Razer') ||
          port.manufacturer.includes('Corsair'))
      )
    );

    if (espPort) {
      currentPort = espPort.path;
      serialPort = new SerialPort({
        path: currentPort,
        baudRate: 115200,
        autoOpen: false
      });

      const parser = new ReadlineParser();
      serialPort.pipe(parser);

      serialPort.open((error) => {
        if (error) {
          console.error('Serial connection failed:', error.message);
          scheduleReconnect();
        } else {
          console.log(`Connected to pendant on ${currentPort}`);
          isConnected = true;
          
          // Clear reconnect timer on successful connection
          if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
          
          // Send current configuration to pendant
          updatePendantMachine(currentMachine);

          // Arm pendant and start heartbeat for safety
          try {
            serialPort.write('HOST:HELLO\n');
            if (currentSoftware !== 'NONE') {
              serialPort.write('ARM:ENABLE\n');
            } else {
              serialPort.write('ARM:DISABLE\n');
            }
          } catch (_) {}
          if (heartbeatInterval) clearInterval(heartbeatInterval);
          heartbeatInterval = setInterval(() => {
            if (serialPort && serialPort.isOpen) {
              serialPort.write('HOST:PING\n');
            }
          }, 750); // keepalive ~0.75s
          
          if (mainWindow) {
            mainWindow.webContents.send('connection-status', { connected: true, port: currentPort });
          }
        }
      });

      parser.on('data', handleSerialCommand);

      serialPort.on('close', () => {
        console.log('Serial connection closed');
        isConnected = false;
        // Safety: stop heartbeat and force-release any keys
        if (heartbeatInterval) { clearInterval(heartbeatInterval); heartbeatInterval = null; }
        releaseAllPressedKeys();
        if (mainWindow) {
          mainWindow.webContents.send('connection-status', { connected: false });
        }
        scheduleReconnect();
      });

      serialPort.on('error', (error) => {
        console.error('Serial error:', error.message);
        isConnected = false;
        // Safety: stop heartbeat and force-release any keys
        if (heartbeatInterval) { clearInterval(heartbeatInterval); heartbeatInterval = null; }
        releaseAllPressedKeys();
        scheduleReconnect();
      });
    } else {
      console.log('No ESP32 pendant found, retrying...');
      maybePromptCp210xDriverInstall(ports).catch((err) => {
        console.warn('CP210x driver check failed:', err.message || err);
      });
      scheduleReconnect();
    }
  }).catch(error => {
    console.error('Port listing error:', error.message);
    scheduleReconnect();
  });
}

function scheduleReconnect() {
  if (reconnectTimer) clearTimeout(reconnectTimer);
  // Increase reconnect delay to reduce COM port scanning interference
  reconnectTimer = setTimeout(connectSerial, 8000);
}

// Windows compatibility and hardware detection
function checkWindowsCompatibility() {
  const isWindows10Plus = process.platform === 'win32' && 
    parseFloat(process.getSystemVersion()) >= 10.0;
  
  if (!isWindows10Plus) {
    console.warn('Windows 10/11 recommended for optimal compatibility');
  }
  
  // Detect hardware capabilities
  let hardwareLevel = 'basic';
  if (process.arch === 'x64') hardwareLevel = 'standard';
  if (process.arch === 'x64' && os.totalmem() > 8 * 1024 * 1024 * 1024) hardwareLevel = 'high';
  
  console.log(`Hardware compatibility: ${hardwareLevel} (${process.arch})`);
  return { compatible: true, level: hardwareLevel };
}

// Enhanced window creation with responsive design
function createWindow() {
  const { screen } = require('electron');
  const primaryDisplay = screen.getPrimaryDisplay();
  const { width: screenWidth, height: screenHeight } = primaryDisplay.workAreaSize;
  const scaleFactor = primaryDisplay.scaleFactor;
  
  // Fixed window sizing - optimized for 1080p/1440p work screens, not 4K
  let windowWidth = 800;
  let windowHeight = 600;
  
  // Simple sizing - don't auto-detect screens to avoid 4K issues
  if (screenWidth < 1280) {
    windowWidth = 700;
    windowHeight = 500;
  }
  
  console.log(`Creating window: ${windowWidth}x${windowHeight} (DPI: ${scaleFactor})`);

  mainWindow = new BrowserWindow({
    width: windowWidth,
    height: windowHeight,
    minWidth: 700,
    minHeight: 450,
    show: false,
    center: true,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false,
      enableRemoteModule: true,
      webSecurity: false,
      allowRunningInsecureContent: true,
      // Hardware acceleration fallbacks
      hardwareAcceleration: !isLowEndHardware,
      backgroundThrottling: false,
      // Disable features that can cause crashes on older hardware
      experimentalFeatures: false,
      webgl: !isOldCPU
    },
    icon: path.join(__dirname, '../assets/tray-icon.png'),
    title: 'AIONMECH Universal CNC Pendant Controller',
    titleBarStyle: 'default',
    resizable: true,
    maximizable: true,
    fullscreenable: true
  });

  // Load the universal renderer that matches this main process
  mainWindow.loadFile(path.join(__dirname, 'renderer-universal.html'));

  // Enhanced ready event with machine detection
  mainWindow.once('ready-to-show', () => {
    mainWindow.show();
    
    // Start machine detection
    startMachineDetection();
    
    // Start serial connection
    connectSerial();
    
    console.log('Universal Pendant Controller ready - All CNC machines supported');

    // Check for updates shortly after startup (non-blocking) - DISABLED
    setTimeout(() => {
      try {
        // autoUpdater.autoDownload = true; // DISABLED - Can cause crashes
        // autoUpdater.checkForUpdatesAndNotify(); // DISABLED - Can cause crashes
        console.log('Auto-updater disabled for stability');
      } catch (e) {
        console.warn('Auto-update check skipped:', e.message);
      }
    }, 3000);
  });

  mainWindow.on('closed', () => {
    mainWindow = null;
    if (machineDetectionInterval) clearInterval(machineDetectionInterval);
    if (unitCheckInterval) clearInterval(unitCheckInterval);
    if (heartbeatInterval) clearInterval(heartbeatInterval);
  });

  // System tray with machine status
  createTray();
}

// Open DevTools from renderer on demand
ipcMain.on('open-devtools', () => {
  if (mainWindow && !mainWindow.isDestroyed()) {
    if (!mainWindow.webContents.isDevToolsOpened()) {
      mainWindow.webContents.openDevTools({ mode: 'detach' });
    } else {
      mainWindow.webContents.closeDevTools();
    }
  }
});

// Machine detection loop
function startMachineDetection() {
  // Initial detection
  detectCncSoftware().then(result => {
    if (result.detected) {
      currentSoftware = result.software;
      currentMachine = result.machine;
      console.log(`Initial detection: ${result.displayName}`);
      updatePendantMachine(currentMachine);
      
      if (mainWindow) {
        mainWindow.webContents.send('software-detected', result);
      }
    } else {
      currentSoftware = 'NONE';
      currentMachine = 'MANUAL';
    }
  });

  // Periodic detection every 5 seconds
  machineDetectionInterval = setInterval(async () => {
    const result = await detectCncSoftware();
    
    if (result.detected && result.software !== currentSoftware) {
      currentSoftware = result.software;
      currentMachine = result.machine;
      console.log(`Detected software change: ${result.displayName}`);
      updatePendantMachine(currentMachine);
      if (serialPort && serialPort.isOpen) {
        try {
          if (currentSoftware === 'CutControl' || currentSoftware === 'FireControl') serialPort.write('ARM:ENABLE\n');
          else serialPort.write('ARM:DISABLE\n');
        } catch (_) {}
      }
      
      if (mainWindow) {
        mainWindow.webContents.send('software-detected', result);
      }
    } else if (!result.detected && currentSoftware !== 'NONE') {
      currentSoftware = 'NONE'; 
      currentMachine = 'MANUAL';
      console.log('No CNC software detected');
      updatePendantMachine(currentMachine);
      if (serialPort && serialPort.isOpen) {
        try { serialPort.write('ARM:DISABLE\n'); } catch (_) {}
      }
      // Safety: ensure no stuck keys when software exits
      releaseAllPressedKeys();
      
      if (mainWindow) {
        mainWindow.webContents.send('software-disconnected');
      }
    }
  }, 5000);
}

// Enhanced tray with machine status
function createTray() {
  // Try to create tray with PNG; if it fails, continue without tray
  try {
    const trayPath = path.join(__dirname, '../assets/tray-icon.png');
    let image = null;
    try {
      image = nativeImage.createFromPath(trayPath);
    } catch (_) {
      image = null;
    }
    tray = image && !image.isEmpty() ? new Tray(image) : new Tray(trayPath);
  } catch (e) {
    console.warn('Tray icon failed to load; continuing without tray:', e.message);
    tray = null;
  }
  
  const updateTrayMenu = () => {
    if (!tray) return;
    const contextMenu = Menu.buildFromTemplate([
      {
        label: `AIONMECH CNC Pendant`,
        enabled: false
      },
      { type: 'separator' },
      {
        label: `Machine: ${currentMachine}`,
        enabled: false
      },
      {
        label: `Software: ${currentSoftware}`,
        enabled: false
      },
      {
        label: `Connection: ${isConnected ? currentPort : 'Disconnected'}`,
        enabled: false
      },
      { type: 'separator' },
      {
        label: 'Show Window',
        click: () => {
          if (mainWindow) {
            mainWindow.show();
            mainWindow.focus();
          }
        }
      },
      {
        label: 'Restart Connection',
        click: () => {
          if (serialPort && serialPort.isOpen) {
            serialPort.close();
          }
          setTimeout(connectSerial, 1000);
        }
      },
      { type: 'separator' },
      {
        label: 'Quit',
        click: () => {
          app.quit();
        }
      }
    ]);
    
    tray.setContextMenu(contextMenu);
  };

  if (tray) updateTrayMenu();
  
  // Update tray menu when connection status changes
  if (tray) trayUpdateInterval = setInterval(updateTrayMenu, 2000);

  if (tray) tray.setToolTip('AIONMECH Universal CNC Pendant Controller');
  
  if (tray) {
    tray.on('double-click', () => {
      if (mainWindow) {
        mainWindow.show();
        mainWindow.focus();
      }
    });
  }
}

// IPC handlers for UI communication
ipcMain.handle('get-system-info', () => {
  return {
    platform: process.platform,
    arch: process.arch,
    version: process.getSystemVersion(),
    node: process.version,
    electron: process.versions.electron,
    chrome: process.versions.chrome,
    robotjs: robot ? 'available' : 'unavailable',
    currentMachine,
    currentSoftware,
    isConnected,
    supportedMachines: Object.values(cncSoftware).map(s => s.displayName)
  };
});

// --- Auto-updater wiring --- DISABLED FOR STABILITY
function wireAutoUpdater() {
  console.log('Auto-updater disabled - wireAutoUpdater() called but not active');
  /*
  autoUpdater.on('checking-for-update', () => {
    if (mainWindow) mainWindow.webContents.send('update-status', { status: 'checking' });
  });
  autoUpdater.on('update-available', (info) => {
    if (mainWindow) mainWindow.webContents.send('update-available', info);
  });
  autoUpdater.on('update-not-available', (info) => {
    if (mainWindow) mainWindow.webContents.send('update-not-available', info);
  });
  autoUpdater.on('error', (err) => {
    if (mainWindow) mainWindow.webContents.send('update-error', { message: err.message });
  });
  autoUpdater.on('download-progress', (progress) => {
    if (mainWindow) mainWindow.webContents.send('update-download-progress', progress);
  });
  autoUpdater.on('update-downloaded', (info) => {
    if (mainWindow) mainWindow.webContents.send('update-downloaded', info);
  });
  */
}

wireAutoUpdater();

ipcMain.handle('check-updates', async () => {
  try {
    // const result = await autoUpdater.checkForUpdatesAndNotify(); // DISABLED
    console.log('Update check disabled for stability');
    return { success: true, result: 'Updates disabled' };
  } catch (e) {
    return { success: false, message: e.message };
  }
});

ipcMain.handle('install-update', async () => {
  try {
    // autoUpdater.quitAndInstall(); // DISABLED
    console.log('Update install disabled for stability');
    return { success: true };
  } catch (e) {
    return { success: false, message: e.message };
  }
});

ipcMain.handle('test-connection', () => {
  if (serialPort && serialPort.isOpen) {
    serialPort.write('LCD:SOLID,50,100,150\n');
    setTimeout(() => {
      serialPort.write('LCD:SOLID,0,0,0\n');
    }, 2000);
    return { success: true, message: 'Test signal sent to pendant' };
  }
  return { success: false, message: 'No pendant connected' };
});

ipcMain.handle('reconnect-serial', () => {
  if (serialPort && serialPort.isOpen) {
    serialPort.close();
  }
  setTimeout(connectSerial, 1000);
  return { success: true, message: 'Reconnection initiated' };
});

ipcMain.handle('set-theme', (event, theme) => {
  if (serialPort && serialPort.isOpen) {
    if (theme.useWheel) {
      serialPort.write(`LCD:WHEEL,${theme.hue}\n`);
    } else {
      serialPort.write(`LCD:SOLID,${theme.bgR},${theme.bgG},${theme.bgB}\n`);
    }
    return { success: true };
  }
  return { success: false, message: 'No pendant connected' };
});

ipcMain.handle('force-machine-type', (event, machineType) => {
  currentMachine = machineType;
  updatePendantMachine(machineType);
  console.log(`Manually set machine type to: ${machineType}`);
  return { success: true };
});

ipcMain.handle('get-pendant-status', () => {
  return {
    connected: isConnected,
    port: currentPort,
    machine: currentMachine,
    software: currentSoftware,
    units: currentUnits
  };
});

// Send command to pendant
ipcMain.handle('send-pendant-command', (event, command) => {
  return new Promise((resolve, reject) => {
    if (!isConnected || !serialPort || !serialPort.isOpen) {
      reject(new Error('Pendant not connected'));
      return;
    }

    try {
      serialPort.write(command + '\n', (error) => {
        if (error) {
          console.log('Command send error:', error);
          reject(error);
        } else {
          console.log('Command sent:', command);
          resolve(true);
        }
      });
    } catch (error) {
      console.log('Command write error:', error);
      reject(error);
    }
  });
});

// App lifecycle management
app.whenReady().then(() => {
  checkWindowsCompatibility();
  createWindow();
  
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  // If tray is available, keep running in background; otherwise quit
  if (process.platform !== 'darwin') {
    if (!tray) {
      app.quit();
    }
    // else keep running in system tray
  }
});

app.on('before-quit', () => {
  if (serialPort && serialPort.isOpen) {
    try { serialPort.write('ARM:DISABLE\n'); } catch (_) {}
    serialPort.close();
  }
  if (machineDetectionInterval) clearInterval(machineDetectionInterval);
  if (unitCheckInterval) clearInterval(unitCheckInterval);
  if (heartbeatInterval) clearInterval(heartbeatInterval);
  if (trayUpdateInterval) clearInterval(trayUpdateInterval);
  if (reconnectTimer) clearTimeout(reconnectTimer);
  releaseAllPressedKeys();
});

console.log('Universal AIONMECH CNC Pendant Controller starting...');
console.log(`Platform: ${process.platform} ${process.arch}`);
console.log(`Node: ${process.version}, Electron: ${process.versions.electron}`);
console.log('Supported machines: FireControl, CutControl, Mach3, Mach4, LinuxCNC, UCCNC, Carbide Motion, UGS, OpenBuilds, CNCjs');