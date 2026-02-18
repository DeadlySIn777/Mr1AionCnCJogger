const { ipcRenderer } = require('electron');

// Global state
let currentMode = 'solid';
let isConnected = false;
let currentHue = 0;
let isDragging = false;

// Color wheel variables
let wheelCanvas, wheelCtx, wheelRadius, wheelCenter;

// Initialize the application
document.addEventListener('DOMContentLoaded', () => {
    initializeColorWheel();
    initializeHueSlider();
    setupEventListeners();
    checkSerialStatus();
    updateSolidColor();
});

// Initialize the color wheel canvas
function initializeColorWheel() {
    wheelCanvas = document.getElementById('colorWheel');
    wheelCtx = wheelCanvas.getContext('2d');
    wheelRadius = 250; // Updated for 500px wheel
    wheelCenter = { x: 250, y: 250 }; // Updated center point
    
    drawColorWheel();
    updateColorPicker();
}

// Draw the RGB color wheel with smooth gradients
function drawColorWheel() {
    // Clear the canvas
    wheelCtx.clearRect(0, 0, 500, 500);
    
    // Create a proper HSV color wheel using canvas gradients
    const centerX = wheelCenter.x;
    const centerY = wheelCenter.y;
    
    // Create hue wheel using conic gradient
    const hueGradient = wheelCtx.createConicGradient(0, centerX, centerY);
    
    // Add color stops for full hue spectrum
    for (let i = 0; i <= 360; i += 10) {
        const hue = (i + currentHue) % 360;
        hueGradient.addColorStop(i / 360, `hsl(${hue}, 100%, 50%)`);
    }
    
    // Fill the wheel with hue gradient
    wheelCtx.fillStyle = hueGradient;
    wheelCtx.beginPath();
    wheelCtx.arc(centerX, centerY, wheelRadius, 0, 2 * Math.PI);
    wheelCtx.fill();
    
    // Add saturation gradient (white to transparent from center)
    const satGradient = wheelCtx.createRadialGradient(centerX, centerY, 0, centerX, centerY, wheelRadius);
    satGradient.addColorStop(0, 'rgba(255, 255, 255, 1)');
    satGradient.addColorStop(1, 'rgba(255, 255, 255, 0)');
    
    wheelCtx.fillStyle = satGradient;
    wheelCtx.beginPath();
    wheelCtx.arc(centerX, centerY, wheelRadius, 0, 2 * Math.PI);
    wheelCtx.fill();
    
    // Add brightness gradient (transparent to black from center)  
    const brightnessGradient = wheelCtx.createRadialGradient(centerX, centerY, 0, centerX, centerY, wheelRadius);
    brightnessGradient.addColorStop(0, 'rgba(0, 0, 0, 0.7)');
    brightnessGradient.addColorStop(0.5, 'rgba(0, 0, 0, 0.3)');
    brightnessGradient.addColorStop(1, 'rgba(0, 0, 0, 0)');
    
    wheelCtx.fillStyle = brightnessGradient;
    wheelCtx.beginPath();
    wheelCtx.arc(centerX, centerY, wheelRadius, 0, 2 * Math.PI);
    wheelCtx.fill();
}

// Convert HSV to RGB
function hsvToRgb(h, s, v) {
    const c = v * s;
    const x = c * (1 - Math.abs((h / 60) % 2 - 1));
    const m = v - c;
    
    let r1, g1, b1;
    
    if (h >= 0 && h < 60) {
        r1 = c; g1 = x; b1 = 0;
    } else if (h >= 60 && h < 120) {
        r1 = x; g1 = c; b1 = 0;
    } else if (h >= 120 && h < 180) {
        r1 = 0; g1 = c; b1 = x;
    } else if (h >= 180 && h < 240) {
        r1 = 0; g1 = x; b1 = c;
    } else if (h >= 240 && h < 300) {
        r1 = x; g1 = 0; b1 = c;
    } else {
        r1 = c; g1 = 0; b1 = x;
    }
    
    return {
        r: Math.round((r1 + m) * 255),
        g: Math.round((g1 + m) * 255),
        b: Math.round((b1 + m) * 255)
    };
}

// Initialize hue slider
function initializeHueSlider() {
    const hueSlider = document.getElementById('hueSlider');
    const hueHandle = document.getElementById('hueHandle');
    
    updateHueHandle();
    
    hueSlider.addEventListener('mousedown', (e) => {
        isDragging = true;
        updateHueFromMouse(e);
    });
    
    document.addEventListener('mousemove', (e) => {
        if (isDragging) {
            updateHueFromMouse(e);
        }
    });
    
    document.addEventListener('mouseup', () => {
        isDragging = false;
    });
}

// Update hue from mouse position
function updateHueFromMouse(e) {
    const hueSlider = document.getElementById('hueSlider');
    const rect = hueSlider.getBoundingClientRect();
    const x = Math.max(0, Math.min(rect.width, e.clientX - rect.left));
    const percentage = x / rect.width;
    
    currentHue = percentage * 360;
    updateHueHandle();
    updateHueValue();
    
    if (currentMode === 'wheel') {
        drawColorWheel();
    }
}

// Update hue handle position
function updateHueHandle() {
    const hueHandle = document.getElementById('hueHandle');
    const percentage = currentHue / 360;
    hueHandle.style.left = `${percentage * 100}%`;
}

// Update hue value display
function updateHueValue() {
    const hueValue = document.getElementById('hueValue');
    hueValue.textContent = `${Math.round(currentHue)}°`;
}

// Update color picker position
function updateColorPicker() {
    const colorPicker = document.getElementById('colorPicker');
    const wheelContainer = document.querySelector('.color-wheel-container');
    
    // Position at the edge of the wheel for demonstration
    const angle = (currentHue * Math.PI) / 180;
    const x = wheelCenter.x + Math.cos(angle) * (wheelRadius * 0.8);
    const y = wheelCenter.y + Math.sin(angle) * (wheelRadius * 0.8);
    
    // Convert canvas coordinates to container coordinates
    const containerRect = wheelContainer.getBoundingClientRect();
    const canvasRect = wheelCanvas.getBoundingClientRect();
    
    // Calculate position relative to container
    const canvasRelativeX = (x / 500) * canvasRect.width;
    const canvasRelativeY = (y / 500) * canvasRect.height;
    
    // Position relative to container (canvas position within container + canvas-relative position)
    const containerX = (canvasRect.left - containerRect.left) + canvasRelativeX;
    const containerY = (canvasRect.top - containerRect.top) + canvasRelativeY;
    
    colorPicker.style.left = `${containerX}px`;
    colorPicker.style.top = `${containerY}px`;
    
    // Update picker color based on current position
    const distance = wheelRadius * 0.8;
    const saturation = Math.min(distance / wheelRadius, 1);
    const value = 0.3 + 0.7 * (distance / wheelRadius);
    const rgb = hsvToRgb(currentHue, saturation, value);
    colorPicker.style.backgroundColor = `rgb(${rgb.r}, ${rgb.g}, ${rgb.b})`;
}

// Setup event listeners
function setupEventListeners() {
    // Listen for serial status updates
    ipcRenderer.on('serial-status', (event, status) => {
        updateConnectionStatus(status);
    });
    
    // Enhanced color wheel event handlers
    wheelCanvas.addEventListener('mousedown', handleWheelMouseDown);
    wheelCanvas.addEventListener('mousemove', handleWheelMouseMove);
    wheelCanvas.addEventListener('mouseup', handleWheelMouseUp);
    wheelCanvas.addEventListener('mouseleave', handleWheelMouseUp);
    wheelCanvas.addEventListener('click', handleWheelClick);
}

// Enhanced wheel interaction handlers
function handleWheelMouseDown(e) {
    if (currentMode === 'wheel') {
        isDragging = true;
        handleWheelInteraction(e);
    }
}

function handleWheelMouseMove(e) {
    if (currentMode === 'wheel' && isDragging) {
        handleWheelInteraction(e);
    }
}

function handleWheelMouseUp(e) {
    isDragging = false;
}

function handleWheelClick(e) {
    if (currentMode === 'wheel') {
        handleWheelInteraction(e);
    }
}

function handleWheelInteraction(e) {
    const rect = wheelCanvas.getBoundingClientRect();
    const containerRect = document.querySelector('.color-wheel-container').getBoundingClientRect();
    
    // Mouse position relative to canvas
    const canvasX = e.clientX - rect.left;
    const canvasY = e.clientY - rect.top;
    
    // Mouse position relative to container
    const containerX = e.clientX - containerRect.left;
    const containerY = e.clientY - containerRect.top;
    
    // Convert to canvas coordinates (account for canvas scaling)
    const scaledX = (canvasX / rect.width) * 500;
    const scaledY = (canvasY / rect.height) * 500;
    
    const dx = scaledX - wheelCenter.x;
    const dy = scaledY - wheelCenter.y;
    const distance = Math.sqrt(dx * dx + dy * dy);
    
    console.log(`Click at: canvas(${scaledX.toFixed(1)}, ${scaledY.toFixed(1)}), distance: ${distance.toFixed(1)}, radius: ${wheelRadius}`);
    
    // Check if click is within the wheel
    if (distance <= wheelRadius) {
        // Calculate color at this position
        const angle = Math.atan2(dy, dx);
        const hue = ((angle * 180 / Math.PI) + 360 + currentHue) % 360;
        const saturation = Math.min(distance / wheelRadius, 1);
        const value = 0.3 + 0.7 * (distance / wheelRadius);
        
        // Convert to RGB
        const rgb = hsvToRgb(hue, saturation, value);
        
        // Update color picker position - use container-relative coordinates
        const colorPicker = document.getElementById('colorPicker');
        
        // Position the picker exactly where the user clicked (relative to container)
        colorPicker.style.left = `${containerX}px`;
        colorPicker.style.top = `${containerY}px`;
        colorPicker.style.position = 'absolute';
        
        console.log(`Picker positioned at container coords: (${containerX}, ${containerY})`);
        
        // Update the color picker appearance
        colorPicker.style.backgroundColor = `rgb(${rgb.r}, ${rgb.g}, ${rgb.b})`;
        
        // Update center preview
        const centerPreview = document.getElementById('wheelCenterPreview');
        if (centerPreview) {
            centerPreview.style.backgroundColor = `rgb(${rgb.r}, ${rgb.g}, ${rgb.b})`;
            centerPreview.style.boxShadow = `0 0 20px rgba(${rgb.r}, ${rgb.g}, ${rgb.b}, 0.6)`;
        }
        
        // Update solid color inputs to match selected color
        document.getElementById('redInput').value = rgb.r;
        document.getElementById('greenInput').value = rgb.g;
        document.getElementById('blueInput').value = rgb.b;
        updateSolidColor();
        
        // Visual feedback
        colorPicker.style.transform = 'translate(-50%, -50%) scale(1.2)';
        setTimeout(() => {
            colorPicker.style.transform = 'translate(-50%, -50%) scale(1)';
        }, 150);
    }
}

// Reset wheel to default position
function resetWheelToDefault() {
    currentHue = 0;
    document.getElementById('hueHandle').style.left = '0%';
    document.getElementById('hueValue').textContent = '0°';
    drawColorWheel();
    updateColorPicker();
    
    // Reset center preview
    const centerPreview = document.getElementById('wheelCenterPreview');
    if (centerPreview) {
        centerPreview.style.backgroundColor = '#000';
        centerPreview.style.boxShadow = '0 0 10px rgba(0, 0, 0, 0.5)';
    }
}

// Set current mode (solid, wheel, firmware, or settings)
function setMode(mode) {
    currentMode = mode;
    
    // Hide all sections
    document.getElementById('solidSection').style.display = 'none';
    document.getElementById('wheelSection').style.display = 'none';
    document.getElementById('firmwareSection').style.display = 'none';
    document.getElementById('settingsSection').style.display = 'none';
    
    // Remove active class from all tabs
    document.querySelectorAll('.mode-tab').forEach(tab => tab.classList.remove('active'));
    
    // Show selected section and activate tab
    switch(mode) {
        case 'solid':
            document.getElementById('solidSection').style.display = 'block';
            document.getElementById('solidTab').classList.add('active');
            break;
        case 'wheel':
            document.getElementById('wheelSection').style.display = 'block';
            document.getElementById('wheelTab').classList.add('active');
            drawColorWheel();
            updateColorPicker();
            break;
        case 'firmware':
            document.getElementById('firmwareSection').style.display = 'block';
            document.getElementById('firmwareTab').classList.add('active');
            break;
        case 'settings':
            document.getElementById('settingsSection').style.display = 'block';
            document.getElementById('settingsTab').classList.add('active');
            break;
    }
}

// Update solid color preview and values
function updateSolidColor() {
    const r = parseInt(document.getElementById('redInput').value) || 0;
    const g = parseInt(document.getElementById('greenInput').value) || 0;
    const b = parseInt(document.getElementById('blueInput').value) || 0;
    
    const hex = `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`;
    
    document.getElementById('solidPreview').style.backgroundColor = `rgb(${r}, ${g}, ${b})`;
    document.getElementById('solidValue').textContent = hex.toUpperCase();
}

// Check current serial connection status
async function checkSerialStatus() {
    try {
        const status = await ipcRenderer.invoke('get-serial-status');
        updateConnectionStatus(status);
    } catch (error) {
        console.error('Error checking serial status:', error);
    }
}

// Update connection status display
function updateConnectionStatus(status) {
    isConnected = status.connected;
    
    const statusDot = document.getElementById('statusDot');
    const statusText = document.getElementById('statusText');
    const connectionPanel = document.getElementById('connectionPanel');
    const connectionStatus = document.getElementById('connectionStatus');
    const portInfo = document.getElementById('portInfo');
    const applyBtn = document.getElementById('applyBtn');
    
    if (isConnected) {
        statusDot.classList.add('connected');
        statusText.textContent = 'Connected';
        connectionPanel.classList.add('connected');
        connectionStatus.textContent = '✅ Connected';
        portInfo.textContent = `Port: ${status.port}`;
        applyBtn.disabled = false;
    } else {
        statusDot.classList.remove('connected');
        statusText.textContent = 'Disconnected';
        connectionPanel.classList.remove('connected');
        connectionStatus.textContent = '🔌 Not Connected';
        portInfo.textContent = status.error ? `Error: ${status.error}` : 'Select a serial port to connect';
        applyBtn.disabled = true;
    }
}

// Select serial port
async function selectPort() {
    try {
        await ipcRenderer.invoke('select-port');
    } catch (error) {
        console.error('Error selecting port:', error);
    }
}

// Apply current settings to the pendant
async function applyChanges() {
    if (!isConnected) {
        alert('Please connect to the pendant first');
        return;
    }
    
    const applyBtn = document.getElementById('applyBtn');
    applyBtn.classList.add('sending');
    
    try {
        let command;
        
        if (currentMode === 'solid') {
            const r = parseInt(document.getElementById('redInput').value) || 0;
            const g = parseInt(document.getElementById('greenInput').value) || 0;
            const b = parseInt(document.getElementById('blueInput').value) || 0;
            
            command = `LCD:SOLID,${r},${g},${b}`;
        } else {
            command = `LCD:WHEEL,${currentHue.toFixed(1)}`;
        }
        
        const success = await ipcRenderer.invoke('send-lcd-command', command);
        
        if (success) {
            // Visual feedback
            setTimeout(() => {
                applyBtn.classList.remove('sending');
            }, 500);
        } else {
            throw new Error('Failed to send command');
        }
        
    } catch (error) {
        console.error('Error applying changes:', error);
        alert('Failed to send command to pendant');
        applyBtn.classList.remove('sending');
    }
}

// Keyboard shortcuts
document.addEventListener('keydown', (e) => {
    if (e.ctrlKey && e.key === 'Enter') {
        applyChanges();
    }
    
    if (e.key === '1') {
        setMode('solid');
    }
    
    if (e.key === '2') {
        setMode('wheel');
    }
    
    if (e.key === '3') {
        setMode('firmware');
    }
});

// Auto-apply for solid colors (optional - can be enabled)
function enableAutoApply() {
    document.getElementById('redInput').addEventListener('input', debounce(applyChanges, 500));
    document.getElementById('greenInput').addEventListener('input', debounce(applyChanges, 500));
    document.getElementById('blueInput').addEventListener('input', debounce(applyChanges, 500));
}

// Debounce utility function
function debounce(func, wait) {
    let timeout;
    return function executedFunction(...args) {
        const later = () => {
            clearTimeout(timeout);
            func(...args);
        };
        clearTimeout(timeout);
        timeout = setTimeout(later, wait);
    };
}

// Firmware Update Functions
let firmwareSource = 'github';
let latestFirmwareUrl = null;
let selectedFile = null;

function setFirmwareSource(source) {
    firmwareSource = source;
    
    // Update button styles
    document.querySelectorAll('.firmware-source-btn').forEach(btn => {
        btn.classList.remove('active');
        btn.style.background = 'rgba(255,255,255,0.1)';
        btn.style.color = '#fff';
        btn.style.border = '1px solid rgba(255,255,255,0.2)';
    });
    
    const activeBtn = source === 'github' ? document.getElementById('githubBtn') : document.getElementById('localBtn');
    activeBtn.classList.add('active');
    activeBtn.style.background = '#00ff88';
    activeBtn.style.color = '#000';
    activeBtn.style.border = 'none';
    
    // Show/hide sections
    document.getElementById('githubSection').style.display = source === 'github' ? 'block' : 'none';
    document.getElementById('localSection').style.display = source === 'local' ? 'block' : 'none';
    
    // Reset flash button
    updateFlashButton();
}

function checkForUpdates() {
    const statusEl = document.getElementById('firmwareStatus');
    const checkBtn = document.getElementById('checkUpdateBtn');
    
    statusEl.textContent = '🔍 Checking for updates...';
    statusEl.style.color = '#ffaa00';
    checkBtn.disabled = true;
    checkBtn.textContent = '🔍 Checking...';
    
    // Request firmware update check from main process
    ipcRenderer.send('check-firmware-updates');
}

function flashFirmware() {
    if (!isConnected) {
        alert('Please connect to ESP32 first');
        return;
    }
    
    const flashBtn = document.getElementById('flashBtn');
    const progressDiv = document.getElementById('flashProgress');
    const progressBar = document.getElementById('progressBar');
    const progressText = document.getElementById('progressText');
    
    flashBtn.disabled = true;
    flashBtn.textContent = '⚡ Preparing...';
    progressDiv.style.display = 'block';
    progressBar.style.width = '0%';
    progressText.textContent = 'Preparing to flash...';
    
    if (firmwareSource === 'github') {
        if (!latestFirmwareUrl) {
            alert('No firmware available. Please check for updates first.');
            resetFlashButton();
            return;
        }
        ipcRenderer.send('flash-firmware-url', latestFirmwareUrl);
    } else {
        if (!selectedFile) {
            alert('Please select a firmware file first.');
            resetFlashButton();
            return;
        }
        ipcRenderer.send('flash-firmware-file', selectedFile);
    }
}

function resetFlashButton() {
    const flashBtn = document.getElementById('flashBtn');
    const progressDiv = document.getElementById('flashProgress');
    
    flashBtn.disabled = !canFlash();
    flashBtn.textContent = '⚡ Flash Firmware';
    progressDiv.style.display = 'none';
}

function updateFlashButton() {
    const flashBtn = document.getElementById('flashBtn');
    const canFlashNow = canFlash();
    
    flashBtn.disabled = !canFlashNow;
    flashBtn.style.background = canFlashNow ? 'linear-gradient(45deg, #ff6b00, #ff8800)' : '#666';
    flashBtn.style.cursor = canFlashNow ? 'pointer' : 'not-allowed';
}

function canFlash() {
    if (firmwareSource === 'github') {
        return latestFirmwareUrl !== null;
    } else {
        return selectedFile !== null;
    }
}

// File input handler
document.addEventListener('DOMContentLoaded', () => {
    const fileInput = document.getElementById('firmwareFile');
    if (fileInput) {
        fileInput.addEventListener('change', (e) => {
            selectedFile = e.target.files[0]?.path || null;
            updateFlashButton();
            
            const statusEl = document.getElementById('firmwareStatus');
            if (selectedFile) {
                statusEl.textContent = `📁 File selected: ${e.target.files[0].name}`;
                statusEl.style.color = '#00ff88';
            } else {
                statusEl.textContent = '❌ No file selected';
                statusEl.style.color = '#ff4444';
            }
        });
    }
});

// IPC Event Listeners for firmware updates
ipcRenderer.on('firmware-check-result', (event, result) => {
    const statusEl = document.getElementById('firmwareStatus');
    const checkBtn = document.getElementById('checkUpdateBtn');
    
    checkBtn.disabled = false;
    checkBtn.textContent = '🔍 Check for Updates';
    
    if (result.success) {
        latestFirmwareUrl = result.downloadUrl;
        statusEl.textContent = `✅ Latest firmware available: ${result.version}`;
        statusEl.style.color = '#00ff88';
    } else {
        latestFirmwareUrl = null;
        statusEl.textContent = `❌ ${result.error}`;
        statusEl.style.color = '#ff4444';
    }
    
    updateFlashButton();
});

ipcRenderer.on('flash-progress', (event, progress) => {
    const progressBar = document.getElementById('progressBar');
    const progressText = document.getElementById('progressText');
    const flashBtn = document.getElementById('flashBtn');
    
    progressBar.style.width = `${progress.percent}%`;
    progressText.textContent = progress.message;
    flashBtn.textContent = `⚡ ${Math.round(progress.percent)}%`;
});

ipcRenderer.on('flash-complete', (event, result) => {
    const statusEl = document.getElementById('firmwareStatus');
    const progressText = document.getElementById('progressText');
    
    if (result.success) {
        statusEl.textContent = '✅ Firmware updated successfully!';
        statusEl.style.color = '#00ff88';
        progressText.textContent = 'Flash complete! ESP32 restarting...';
        
        // Auto-reconnect after a delay
        setTimeout(() => {
            checkSerialStatus();
        }, 3000);
    } else {
        statusEl.textContent = `❌ Flash failed: ${result.error}`;
        statusEl.style.color = '#ff4444';
        progressText.textContent = 'Flash failed. Check connection and try again.';
    }
    
    resetFlashButton();
});

// Initialize firmware tab
document.addEventListener('DOMContentLoaded', () => {
    setFirmwareSource('github');
    loadSettings();
    setupSettingsListeners();
});

// Settings management
const defaultSettings = {
    autoConnect: false,
    baudRate: 115200,
    wheelSpeed: 50,
    uiTheme: 'dark',
    lcdEnabled: true,
    lcdDimming: false,
    defaultAxis: 'Z'
};

function loadSettings() {
    const settings = JSON.parse(localStorage.getItem('mr1Settings') || JSON.stringify(defaultSettings));
    
    // Apply settings to UI
    document.getElementById('autoConnect').checked = settings.autoConnect;
    document.getElementById('baudRate').value = settings.baudRate;
    document.getElementById('wheelSpeed').value = settings.wheelSpeed;
    document.getElementById('speedValue').textContent = settings.wheelSpeed + '%';
    document.getElementById('uiTheme').value = settings.uiTheme;
    document.getElementById('lcdEnabled').checked = settings.lcdEnabled;
    document.getElementById('lcdDimming').checked = settings.lcdDimming;
    document.getElementById('defaultAxis').value = settings.defaultAxis;
    
    // Apply theme
    applyTheme(settings.uiTheme);
}

function saveSettings() {
    const settings = {
        autoConnect: document.getElementById('autoConnect').checked,
        baudRate: parseInt(document.getElementById('baudRate').value),
        wheelSpeed: parseInt(document.getElementById('wheelSpeed').value),
        uiTheme: document.getElementById('uiTheme').value,
        lcdEnabled: document.getElementById('lcdEnabled').checked,
        lcdDimming: document.getElementById('lcdDimming').checked,
        defaultAxis: document.getElementById('defaultAxis').value
    };
    
    localStorage.setItem('mr1Settings', JSON.stringify(settings));
    applyTheme(settings.uiTheme);
    
    // Show save confirmation
    const saveBtn = event.target;
    const originalText = saveBtn.textContent;
    saveBtn.textContent = '✅ Saved!';
    saveBtn.style.background = 'linear-gradient(45deg, #00ff88, #00cc6a)';
    
    setTimeout(() => {
        saveBtn.textContent = originalText;
        saveBtn.style.background = 'linear-gradient(45deg, #00ff88, #00cc6a)';
    }, 2000);
}

function resetSettings() {
    if (confirm('Reset all settings to defaults? This cannot be undone.')) {
        localStorage.removeItem('mr1Settings');
        loadSettings();
        
        // Show reset confirmation
        const resetBtn = event.target;
        const originalText = resetBtn.textContent;
        resetBtn.textContent = '🔄 Reset!';
        
        setTimeout(() => {
            resetBtn.textContent = originalText;
        }, 2000);
    }
}

function setupSettingsListeners() {
    // Speed slider live update
    const speedSlider = document.getElementById('wheelSpeed');
    const speedValue = document.getElementById('speedValue');
    
    speedSlider.addEventListener('input', (e) => {
        speedValue.textContent = e.target.value + '%';
    });
    
    // Theme change live preview
    const themeSelect = document.getElementById('uiTheme');
    themeSelect.addEventListener('change', (e) => {
        applyTheme(e.target.value);
    });
}

function applyTheme(theme) {
    const root = document.documentElement;
    
    switch(theme) {
        case 'blue':
            root.style.setProperty('--accent-color', '#007acc');
            root.style.setProperty('--accent-light', '#0099ff');
            root.style.setProperty('--accent-dark', '#005a99');
            break;
        case 'green':
            root.style.setProperty('--accent-color', '#00ff88');
            root.style.setProperty('--accent-light', '#00ffaa');
            root.style.setProperty('--accent-dark', '#00cc6a');
            break;
        case 'purple':
            root.style.setProperty('--accent-color', '#764ba2');
            root.style.setProperty('--accent-light', '#9966cc');
            root.style.setProperty('--accent-dark', '#5a3d7a');
            break;
        default: // dark
            root.style.setProperty('--accent-color', '#00ff88');
            root.style.setProperty('--accent-light', '#00ffaa');
            root.style.setProperty('--accent-dark', '#00cc6a');
    }
    
    console.log(`Theme applied: ${theme}`);
}
