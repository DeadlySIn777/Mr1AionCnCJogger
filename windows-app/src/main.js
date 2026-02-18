const { app, BrowserWindow, Tray, Menu, ipcMain, dialog } = require('electron');
const path = require('path');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');

// Optional robotjs import for testing - handle gracefully on all hardware
let robot = null;
try {
  // Only load robotjs if explicitly needed and compatible
  if (process.platform === 'win32' && process.arch === 'x64') {
    robot = require('robotjs');
    console.log('RobotJS loaded successfully');
  } else {
    console.log('RobotJS not loaded - platform/arch compatibility');
  }
} catch (error) {
  console.log('RobotJS not available - UI testing mode enabled:', error.message);
}

const AutoLaunch = require('auto-launch');

// Global variables
let mainWindow = null;
let tray = null;
let serialPort = null;
let currentPort = null;
let isConnected = false;
let cutControlConnected = false;
let cutControlUnits = 'MM'; // Default to metric
let unitCheckInterval = null;

// Auto-launch setup
const autoLauncher = new AutoLaunch({
  name: 'MR1 Pendant Controller',
  path: app.getPath('exe'),
});

// Official CutControl keyboard mappings (verified from documentation)
const keyMappings = {
  'LEFT': 'left',        // Jog X−: Left Arrow
  'RIGHT': 'right',      // Jog X+: Right Arrow  
  'UP': 'up',            // Jog Y+: Up Arrow
  'DOWN': 'down',        // Jog Y−: Down Arrow
  'PGUP': 'pageup',      // Jog Z+: Page Up
  'PGDN': 'pagedown',    // Jog Z−: Page Down
  'SPACE': 'space',      // Pause Program: Space
  'TAB': 'tab',          // Toggle Nudge Jog: Tab
  'ALT_R': ['alt', 'r'], // Start/Resume Program: Alt+R
  'ALT_S': ['alt', 's'], // Stop Program: Alt+S
  // CutControl Jog Distance Controls
  '1': '1',              // Jog Distance 0.1mm: 1 key
  '2': '2',              // Jog Distance 1mm: 2 key
  '3': '3',              // Jog Distance 10mm: 3 key
  '4': '4',              // Jog Distance 100mm: 4 key
  // CutControl Feed Rate Controls  
  'F1': 'f1',            // Feed Rate 25%: F1 key
  'F2': 'f2',            // Feed Rate 50%: F2 key
  'F3': 'f3',            // Feed Rate 75%: F3 key
  'F4': 'f4'             // Feed Rate 100%: F4 key
};

// Windows compatibility and hardware detection
function checkWindowsCompatibility() {
  const os = require('os');
  const osInfo = {
    platform: os.platform(),
    release: os.release(),
    arch: os.arch(),
    totalmem: Math.round(os.totalmem() / 1024 / 1024 / 1024), // GB
    cpus: os.cpus().length
  };
  
  console.log('System Info:', osInfo);
  
  // Windows 10/11 version check
  if (osInfo.platform === 'win32') {
    const version = osInfo.release;
    if (parseFloat(version) < 10.0) {
      console.warn('Warning: Windows 10 or later recommended for best compatibility');
    }
  }
  
  // Memory check
  if (osInfo.totalmem < 2) {
    console.warn('Warning: Low system memory detected. Consider disabling hardware acceleration if experiencing issues.');
    app.commandLine.appendSwitch('--disable-gpu');
    app.commandLine.appendSwitch('--disable-software-rasterizer');
  }
  
  // Graphics compatibility
  app.commandLine.appendSwitch('--ignore-gpu-blacklist');
  app.commandLine.appendSwitch('--disable-gpu-sandbox');
  app.commandLine.appendSwitch('--enable-unsafe-webgpu');
}
  
// Track currently pressed keys to avoid spam
const pressedKeys = new Set();

// Check compatibility before app starts
checkWindowsCompatibility();

function createWindow() {
  // Get screen dimensions for responsive sizing
  const { screen } = require('electron');
  const primaryDisplay = screen.getPrimaryDisplay();
  const { width: screenWidth, height: screenHeight } = primaryDisplay.workAreaSize;
  
  // Responsive window sizing (70% of screen, min 800x600, max 1600x1200)
  const windowWidth = Math.min(Math.max(Math.floor(screenWidth * 0.7), 800), 1600);
  const windowHeight = Math.min(Math.max(Math.floor(screenHeight * 0.7), 600), 1200);
  
  mainWindow = new BrowserWindow({
    width: windowWidth,
    height: windowHeight,
    minWidth: 800,
    minHeight: 600,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false,
      webSecurity: false, // For local file access
      enableRemoteModule: false,
      spellcheck: false,
      // Hardware acceleration fixes for compatibility
      hardwareAcceleration: true,
      offscreen: false
    },
    icon: path.join(__dirname, '../assets/icon.png'),
    show: false, // Don't show until ready
    skipTaskbar: false,
    alwaysOnTop: false,
    resizable: true,
    center: true, // Center on screen
    titleBarStyle: 'default',
    frame: true,
    transparent: false,
    backgroundColor: '#0a0a0a' // Dark background for loading
  });

  mainWindow.loadFile(path.join(__dirname, 'renderer.html'));
  
  // Show window when ready to prevent visual glitches
  mainWindow.once('ready-to-show', () => {
    mainWindow.show();
    
    // Windows 10/11 DPI scaling compatibility
    if (process.platform === 'win32') {
      const scaleFactor = screen.getPrimaryDisplay().scaleFactor;
      if (scaleFactor > 1) {
        mainWindow.webContents.setZoomFactor(1 / scaleFactor);
      }
    }
  });
  
  // Prevent blank screen on Windows
  mainWindow.webContents.on('did-fail-load', () => {
    console.log('Page failed to load, reloading...');
    mainWindow.webContents.reload();
  });
  
  // Hide window when closed instead of quitting
  mainWindow.on('close', (event) => {
    if (!app.isQuitting) {
      event.preventDefault();
      mainWindow.hide();
    }
  });

  // Development tools
  if (process.argv.includes('--dev')) {
    mainWindow.webContents.openDevTools();
    mainWindow.show();
  }
}

function createTray() {
  const iconPath = path.join(__dirname, '../assets/tray-icon.png');
  
  // Try to create tray with icon, fallback to native icon if not found
  try {
    tray = new Tray(iconPath);
  } catch (error) {
    console.log('Using default icon for tray');
    // Create a minimal icon from NativeImage
    const { nativeImage } = require('electron');
    const icon = nativeImage.createEmpty();
    tray = new Tray(icon);
  }
  
  const contextMenu = Menu.buildFromTemplate([
    {
      label: 'MR1 Pendant Controller',
      type: 'normal',
      enabled: false
    },
    { type: 'separator' },
    {
      label: `Status: ${isConnected ? 'Connected' : 'Disconnected'}`,
      type: 'normal',
      enabled: false
    },
    {
      label: `Port: ${currentPort || 'None'}`,
      type: 'normal',
      enabled: false
    },
    { type: 'separator' },
    {
      label: 'Show RGB Controller',
      type: 'normal',
      click: () => {
        mainWindow.show();
        mainWindow.focus();
      }
    },
    {
      label: 'Select Serial Port',
      type: 'normal',
      click: selectSerialPort
    },
    {
      label: 'Auto-start with Windows',
      type: 'checkbox',
      checked: false,
      click: toggleAutoStart
    },
    { type: 'separator' },
    {
      label: 'Reconnect',
      type: 'normal',
      click: reconnectSerial
    },
    {
      label: 'Disconnect',
      type: 'normal',
      click: disconnectSerial
    },
    { type: 'separator' },
    {
      label: 'Quit',
      type: 'normal',
      click: () => {
        app.isQuitting = true;
        app.quit();
      }
    }
  ]);

  tray.setContextMenu(contextMenu);
  tray.setToolTip('MR1 Pendant Controller');
  
  // Check current auto-launch status
  autoLauncher.isEnabled().then((isEnabled) => {
    contextMenu.items[6].checked = isEnabled;
    tray.setContextMenu(contextMenu);
  });
}

async function selectSerialPort() {
  try {
    const ports = await SerialPort.list();
    const portChoices = ports.map(port => ({
      type: 'radio',
      label: `${port.path} - ${port.manufacturer || 'Unknown'}`,
      value: port.path
    }));

    if (portChoices.length === 0) {
      dialog.showMessageBox(mainWindow, {
        type: 'warning',
        title: 'No Serial Ports',
        message: 'No serial ports found. Make sure your ESP32 is connected.'
      });
      return;
    }

    const result = await dialog.showMessageBox(mainWindow, {
      type: 'question',
      title: 'Select Serial Port',
      message: 'Choose the serial port for your ESP32 pendant:',
      detail: portChoices.map(choice => choice.label).join('\n'),
      buttons: portChoices.map(choice => choice.label),
      defaultId: 0
    });

    if (result.response >= 0) {
      const selectedPort = portChoices[result.response].value;
      await connectToSerial(selectedPort);
    }
  } catch (error) {
    console.error('Error selecting serial port:', error);
  }
}

async function connectToSerial(portPath) {
  if (serialPort && serialPort.isOpen) {
    serialPort.close();
  }

  try {
    serialPort = new SerialPort({
      path: portPath,
      baudRate: 115200,
      autoOpen: false
    });

    const parser = serialPort.pipe(new ReadlineParser({ delimiter: '\n' }));
    
    serialPort.open((err) => {
      if (err) {
        console.error('Error opening port:', err);
        isConnected = false;
        updateTrayStatus();
        return;
      }
      
      console.log(`Connected to ${portPath}`);
      currentPort = portPath;
      isConnected = true;
      updateTrayStatus();
      
      // Start monitoring CutControl and units
      startCutControlMonitoring();
      
      // Send initial connection acknowledgment
      if (mainWindow) {
        mainWindow.webContents.send('serial-status', { connected: true, port: portPath });
      }
    });

    serialPort.on('error', (err) => {
      console.error('Serial port error:', err);
      isConnected = false;
      currentPort = null;
      cutControlConnected = false;
      stopCutControlMonitoring();
      updateTrayStatus();
      
      if (mainWindow) {
        mainWindow.webContents.send('serial-status', { connected: false, error: err.message });
      }
    });

    serialPort.on('close', () => {
      console.log('Serial port closed');
      isConnected = false;
      currentPort = null;
      cutControlConnected = false;
      stopCutControlMonitoring();
      updateTrayStatus();
      
      if (mainWindow) {
        mainWindow.webContents.send('serial-status', { connected: false });
      }
    });

    parser.on('data', (line) => {
      handleSerialData(line.trim());
    });

  } catch (error) {
    console.error('Failed to connect to serial port:', error);
    dialog.showErrorBox('Connection Failed', `Could not connect to ${portPath}: ${error.message}`);
  }
}

function handleSerialData(data) {
  console.log('Received:', data);
  
  if (data === 'PENDANT:READY') {
    console.log('Pendant is ready');
    return;
  }

  if (data.startsWith('KEY:')) {
    // Parse key command: KEY:LEFT,1 or KEY:ALT_R,0
    const parts = data.substring(4).split(',');
    if (parts.length === 2) {
      const keyName = parts[0];
      const pressed = parts[1] === '1';
      
      handleKeyCommand(keyName, pressed);
    }
  }
}

function handleKeyCommand(keyName, pressed) {
  const mapping = keyMappings[keyName];
  if (!mapping) {
    console.warn(`Unknown key: ${keyName} (not in official CutControl shortcuts)`);
    return;
  }

  try {
    if (!robot) {
      console.log(`RobotJS not available - would send: ${keyName} (${pressed ? 'PRESS' : 'RELEASE'})`);
      return;
    }

    // Handle distance and feed rate keys as single-shot presses
    if (['1', '2', '3', '4', 'F1', 'F2', 'F3', 'F4'].includes(keyName)) {
      if (pressed) {
        robot.keyTap(mapping);
        console.log(`Sent: ${mapping} (${keyName}) TAP`);
      }
      return;
    }

    if (Array.isArray(mapping)) {
      // Combination key like Alt+R or Alt+S (official CutControl shortcuts)
      const keyId = `${keyName}_${pressed}`;
      
      if (pressed && !pressedKeys.has(keyId)) {
        robot.keyToggle(mapping[0], 'down');  // Press modifier (Alt)
        robot.keyToggle(mapping[1], 'down');  // Press key (R or S)
        pressedKeys.add(keyId);
        console.log(`Sent: ${mapping[0]}+${mapping[1]} (${keyName}) PRESS`);
      } else if (!pressed && pressedKeys.has(keyId)) {
        robot.keyToggle(mapping[1], 'up');    // Release key first
        robot.keyToggle(mapping[0], 'up');    // Release modifier
        pressedKeys.delete(keyId);
        console.log(`Sent: ${mapping[0]}+${mapping[1]} (${keyName}) RELEASE`);
      }
    } else {
      // Single key (arrows, pageup/down, space, tab)
      const keyId = `${keyName}_${pressed}`;
      
      if (pressed && !pressedKeys.has(keyId)) {
        robot.keyToggle(mapping, 'down');
        pressedKeys.add(keyId);
        console.log(`Sent: ${mapping} (${keyName}) PRESS`);
      } else if (!pressed && pressedKeys.has(keyId)) {
        robot.keyToggle(mapping, 'up');
        pressedKeys.delete(keyId);
        console.log(`Sent: ${mapping} (${keyName}) RELEASE`);
      }
    }
  } catch (error) {
    console.error(`Error handling official CutControl key ${keyName}:`, error);
  }
}

function updateTrayStatus() {
  if (!tray) return;
  
  const contextMenu = Menu.buildFromTemplate([
    {
      label: 'MR1 Pendant Controller',
      type: 'normal',
      enabled: false
    },
    { type: 'separator' },
    {
      label: `Status: ${isConnected ? 'Connected' : 'Disconnected'}`,
      type: 'normal',
      enabled: false
    },
    {
      label: `Port: ${currentPort || 'None'}`,
      type: 'normal',
      enabled: false
    },
    {
      label: `CutControl: ${cutControlConnected ? 'Running' : 'Not Running'}`,
      type: 'normal',
      enabled: false
    },
    {
      label: `Units: ${cutControlUnits}`,
      type: 'normal',
      enabled: false
    },
    { type: 'separator' },
    {
      label: 'Show RGB Controller',
      type: 'normal',
      click: () => {
        mainWindow.show();
        mainWindow.focus();
      }
    },
    {
      label: 'Select Serial Port',
      type: 'normal',
      click: selectSerialPort
    },
    {
      label: 'Auto-start with Windows',
      type: 'checkbox',
      checked: false,
      click: toggleAutoStart
    },
    { type: 'separator' },
    {
      label: 'Force Units to Inches',
      type: 'normal',
      click: () => forceUnits('INCHES')
    },
    {
      label: 'Force Units to MM',
      type: 'normal',
      click: () => forceUnits('MM')
    },
    { type: 'separator' },
    {
      label: 'Reconnect',
      type: 'normal',
      click: reconnectSerial,
      enabled: !isConnected && currentPort
    },
    {
      label: 'Disconnect',
      type: 'normal',
      click: disconnectSerial,
      enabled: isConnected
    },
    { type: 'separator' },
    {
      label: 'Quit',
      type: 'normal',
      click: () => {
        app.isQuitting = true;
        app.quit();
      }
    }
  ]);

  // Update auto-launch checkbox
  autoLauncher.isEnabled().then((isEnabled) => {
    contextMenu.items[8].checked = isEnabled;
    tray.setContextMenu(contextMenu);
  });
  
  tray.setContextMenu(contextMenu);
}

// Force units override (for testing or manual control)
function forceUnits(units) {
  updateUnitsIfChanged(units);
}

async function toggleAutoStart() {
  try {
    const isEnabled = await autoLauncher.isEnabled();
    if (isEnabled) {
      await autoLauncher.disable();
    } else {
      await autoLauncher.enable();
    }
    updateTrayStatus();
  } catch (error) {
    console.error('Error toggling auto-start:', error);
  }
}

function reconnectSerial() {
  if (currentPort) {
    connectToSerial(currentPort);
  } else {
    selectSerialPort();
  }
}

function disconnectSerial() {
  if (serialPort && serialPort.isOpen) {
    serialPort.close();
  }
  stopCutControlMonitoring();
}

// CutControl monitoring functions
function startCutControlMonitoring() {
  // Check CutControl status and units every 2 seconds
  unitCheckInterval = setInterval(checkCutControlStatus, 2000);
  
  // Initial check
  checkCutControlStatus();
}

function stopCutControlMonitoring() {
  if (unitCheckInterval) {
    clearInterval(unitCheckInterval);
    unitCheckInterval = null;
  }
}

async function checkCutControlStatus() {
  try {
    // Check if CutControl process is running
    const { spawn } = require('child_process');
    const checkProcess = spawn('tasklist', ['/FI', 'IMAGENAME eq CutControl.exe', '/FO', 'CSV']);
    
    let output = '';
    checkProcess.stdout.on('data', (data) => {
      output += data.toString();
    });
    
    checkProcess.on('close', (code) => {
      const isRunning = output.includes('CutControl.exe');
      
      if (isRunning !== cutControlConnected) {
        cutControlConnected = isRunning;
        console.log(`CutControl ${isRunning ? 'connected' : 'disconnected'}`);
        
        // Send status to ESP32
        if (serialPort && serialPort.isOpen) {
          serialPort.write(`CUTCONTROL:${isRunning ? 'CONNECTED' : 'DISCONNECTED'}\n`);
        }
        
        // Update tray menu
        updateTrayStatus();
        
        // Send to renderer if open
        if (mainWindow && !mainWindow.isDestroyed()) {
          mainWindow.webContents.send('cutcontrol-status', { connected: isRunning });
        }
      }
      
      // If CutControl is running, check units
      if (isRunning) {
        checkCutControlUnits();
      }
    });
    
  } catch (error) {
    console.error('Error checking CutControl status:', error);
  }
}

async function checkCutControlUnits() {
  try {
    const fs = require('fs');
    const path = require('path');
    const os = require('os');
    
    // Common CutControl configuration locations
    const possiblePaths = [
      path.join(os.homedir(), 'AppData', 'Local', 'CutControl', 'config.ini'),
      path.join(os.homedir(), 'AppData', 'Local', 'CutControl', 'settings.ini'),
      path.join(os.homedir(), 'AppData', 'Roaming', 'CutControl', 'config.ini'),
      path.join(os.homedir(), 'AppData', 'Roaming', 'CutControl', 'settings.ini'),
      path.join(os.homedir(), 'Documents', 'CutControl', 'config.ini'),
      'C:\\ProgramData\\CutControl\\config.ini'
    ];
    
    let detectedUnits = 'MM'; // Default to metric
    
    for (const configPath of possiblePaths) {
      try {
        if (fs.existsSync(configPath)) {
          const configContent = fs.readFileSync(configPath, 'utf8');
          
          // Look for unit settings in config file
          if (configContent.includes('units=inches') || 
              configContent.includes('Units=Inches') ||
              configContent.includes('UNITS=INCHES') ||
              configContent.includes('imperial=true') ||
              configContent.includes('Imperial=True')) {
            detectedUnits = 'INCHES';
            break;
          } else if (configContent.includes('units=mm') || 
                     configContent.includes('Units=MM') ||
                     configContent.includes('UNITS=MM') ||
                     configContent.includes('metric=true') ||
                     configContent.includes('Metric=True')) {
            detectedUnits = 'MM';
            break;
          }
        }
      } catch (error) {
        // Ignore individual file read errors
        continue;
      }
    }
    
    // Also check Windows registry for CutControl settings
    try {
      const { spawn } = require('child_process');
      const regQuery = spawn('reg', ['query', 'HKCU\\Software\\CutControl', '/v', 'Units']);
      
      let regOutput = '';
      regQuery.stdout.on('data', (data) => {
        regOutput += data.toString();
      });
      
      regQuery.on('close', (code) => {
        if (code === 0 && regOutput.includes('Inches')) {
          detectedUnits = 'INCHES';
        } else if (code === 0 && regOutput.includes('MM')) {
          detectedUnits = 'MM';
        }
        
        updateUnitsIfChanged(detectedUnits);
      });
      
    } catch (error) {
      // Registry check failed, use file-based detection
      updateUnitsIfChanged(detectedUnits);
    }
    
  } catch (error) {
    console.error('Error checking CutControl units:', error);
  }
}

function updateUnitsIfChanged(newUnits) {
  if (newUnits !== cutControlUnits) {
    cutControlUnits = newUnits;
    console.log(`CutControl units changed to: ${newUnits}`);
    
    // Send units to ESP32
    if (serialPort && serialPort.isOpen) {
      serialPort.write(`UNITS:${newUnits}\n`);
    }
    
    // Update tray menu
    updateTrayStatus();
    
    // Send to renderer if open
    if (mainWindow && !mainWindow.isDestroyed()) {
      mainWindow.webContents.send('units-changed', { units: newUnits });
    }
  }
}

// IPC handlers for renderer process
ipcMain.handle('get-serial-status', () => {
  return {
    connected: isConnected,
    port: currentPort,
    cutControlConnected: cutControlConnected,
    units: cutControlUnits
  };
});

ipcMain.handle('send-lcd-command', (event, command) => {
  if (serialPort && serialPort.isOpen) {
    serialPort.write(command + '\n');
    console.log('Sent LCD command:', command);
    return true;
  }
  return false;
});

ipcMain.handle('force-units', (event, units) => {
  forceUnits(units);
  return true;
});

ipcMain.handle('get-cutcontrol-status', () => {
  return {
    connected: cutControlConnected,
    units: cutControlUnits
  };
});

ipcMain.handle('select-port', selectSerialPort);

// Firmware Update Handlers
ipcMain.on('check-firmware-updates', async (event) => {
  try {
    console.log('Checking for firmware updates...');
    
    // Check GitHub releases for latest firmware
    const fetch = require('node-fetch');
    const response = await fetch('https://api.github.com/repos/YOUR_USERNAME/Mr1AionCnCJogger/releases/latest');
    
    if (!response.ok) {
      throw new Error('Failed to fetch release info');
    }
    
    const release = await response.json();
    
    // Look for .bin file in assets
    const firmwareAsset = release.assets.find(asset => 
      asset.name.endsWith('.bin') && asset.name.includes('esp32')
    );
    
    if (firmwareAsset) {
      event.reply('firmware-check-result', {
        success: true,
        version: release.tag_name,
        downloadUrl: firmwareAsset.browser_download_url,
        size: firmwareAsset.size
      });
    } else {
      throw new Error('No ESP32 firmware found in latest release');
    }
    
  } catch (error) {
    console.error('Firmware check failed:', error);
    event.reply('firmware-check-result', {
      success: false,
      error: error.message
    });
  }
});

ipcMain.on('flash-firmware-url', async (event, downloadUrl) => {
  try {
    await flashFirmwareFromUrl(event, downloadUrl);
  } catch (error) {
    console.error('Firmware flash failed:', error);
    event.reply('flash-complete', {
      success: false,
      error: error.message
    });
  }
});

ipcMain.on('flash-firmware-file', async (event, filePath) => {
  try {
    await flashFirmwareFromFile(event, filePath);
  } catch (error) {
    console.error('Firmware flash failed:', error);
    event.reply('flash-complete', {
      success: false,
      error: error.message
    });
  }
});

async function flashFirmwareFromUrl(event, downloadUrl) {
  const fs = require('fs');
  const path = require('path');
  const fetch = require('node-fetch');
  const os = require('os');
  
  // Download firmware to temp directory
  const tempDir = os.tmpdir();
  const firmwarePath = path.join(tempDir, 'mr1_firmware.bin');
  
  event.reply('flash-progress', { percent: 5, message: 'Downloading firmware...' });
  
  const response = await fetch(downloadUrl);
  if (!response.ok) {
    throw new Error('Failed to download firmware');
  }
  
  const buffer = await response.buffer();
  fs.writeFileSync(firmwarePath, buffer);
  
  event.reply('flash-progress', { percent: 20, message: 'Download complete. Flashing...' });
  
  await flashFirmwareFromFile(event, firmwarePath);
  
  // Clean up temp file
  try {
    fs.unlinkSync(firmwarePath);
  } catch (e) {
    console.warn('Could not delete temp firmware file:', e);
  }
}

async function flashFirmwareFromFile(event, firmwarePath) {
  const { spawn } = require('child_process');
  const fs = require('fs');
  const path = require('path');
  
  // Verify file exists
  if (!fs.existsSync(firmwarePath)) {
    throw new Error('Firmware file not found');
  }
  
  // Close serial connection before flashing
  if (serialPort && serialPort.isOpen) {
    serialPort.close();
    // Wait for port to close
    await new Promise(resolve => setTimeout(resolve, 1000));
  }
  
  event.reply('flash-progress', { percent: 30, message: 'Preparing ESP32 for flash...' });
  
  // Use esptool.py for flashing (requires Python and esptool)
  // Alternative: bundle esptool executable or use esptool-js
  
  try {
    // Try to use bundled esptool or system esptool
    const esptoolCmd = await findEsptool();
    
    const flashArgs = [
      '--chip', 'esp32',
      '--port', currentPort || 'AUTO',
      '--baud', '921600',
      '--before', 'default_reset',
      '--after', 'hard_reset',
      'write_flash',
      '-z',
      '--flash_mode', 'dio',
      '--flash_freq', '40m',
      '--flash_size', '4MB',
      '0x1000', firmwarePath
    ];
    
    const espProcess = spawn(esptoolCmd, flashArgs);
    
    let output = '';
    
    espProcess.stdout.on('data', (data) => {
      output += data.toString();
      console.log('esptool output:', data.toString());
      
      // Parse progress from esptool output
      const progressMatch = data.toString().match(/(\d+)%/);
      if (progressMatch) {
        const percent = Math.min(30 + parseInt(progressMatch[1]) * 0.6, 90);
        event.reply('flash-progress', { 
          percent: percent, 
          message: `Flashing firmware... ${progressMatch[1]}%` 
        });
      }
    });
    
    espProcess.stderr.on('data', (data) => {
      console.error('esptool error:', data.toString());
      output += data.toString();
    });
    
    await new Promise((resolve, reject) => {
      espProcess.on('close', (code) => {
        if (code === 0) {
          event.reply('flash-progress', { percent: 95, message: 'Flash complete. Restarting ESP32...' });
          resolve();
        } else {
          reject(new Error(`esptool failed with code ${code}. Output: ${output}`));
        }
      });
      
      espProcess.on('error', (error) => {
        reject(new Error(`Failed to start esptool: ${error.message}`));
      });
    });
    
    event.reply('flash-progress', { percent: 100, message: 'Firmware update complete!' });
    
    // Wait for ESP32 to restart, then try to reconnect
    setTimeout(() => {
      event.reply('flash-complete', { success: true });
      
      // Attempt to reconnect after 3 seconds
      setTimeout(() => {
        if (currentPort) {
          connectToSerial(currentPort);
        }
      }, 3000);
    }, 2000);
    
  } catch (error) {
    throw new Error(`Flash failed: ${error.message}`);
  }
}

async function findEsptool() {
  const { spawn } = require('child_process');
  
  // Try different possible esptool commands
  const commands = ['esptool.py', 'esptool', 'python -m esptool'];
  
  for (const cmd of commands) {
    try {
      const testProcess = spawn(cmd.split(' ')[0], ['--help'], { stdio: 'pipe' });
      await new Promise((resolve, reject) => {
        testProcess.on('close', (code) => {
          if (code === 0) resolve();
          else reject();
        });
        testProcess.on('error', reject);
      });
      return cmd.split(' ')[0]; // Return just the command name
    } catch (e) {
      continue;
    }
  }
  
  throw new Error('esptool not found. Please install Python and esptool: pip install esptool');
}

// App event handlers
app.whenReady().then(() => {
  // Windows 10/11 compatibility settings
  if (process.platform === 'win32') {
    // Disable hardware acceleration if causing issues
    if (process.argv.includes('--disable-gpu')) {
      app.disableHardwareAcceleration();
    }
    
    // Set app user model ID for Windows notifications
    app.setAppUserModelId('com.mr1pendant.controller');
  }
  
  createWindow();
  createTray();
  
  // Start monitoring for CutControl and units
  startCutControlMonitoring();
});

app.on('window-all-closed', () => {
  // Don't quit on window close, keep running in tray
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) {
    createWindow();
  }
});

// Cleanup on quit
app.on('before-quit', () => {
  stopCutControlMonitoring();
  if (serialPort && serialPort.isOpen) {
    serialPort.close();
  }
});
