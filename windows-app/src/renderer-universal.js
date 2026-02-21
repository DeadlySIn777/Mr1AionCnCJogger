/*
  Universal AIONMECH CNC Pendant Controller - Renderer Process
  Supports all CNC machines with auto-detection and enhanced UI
*/

const { ipcRenderer } = require('electron');

// UI state management
let currentMachine = 'MANUAL';
let currentSoftware = 'NONE';
let isConnected = false;
let currentPort = null;
let systemInfo = null;
let posX = 0, posY = 0, posZ = 0;

// Enhanced pendant state tracking
let pendantState = {
    axis: 'Z',           // Current active axis (X, Y, Z)
    distance: '1.0mm',   // Current jog distance
    speed: '50%',        // Current speed/feed rate
    powerState: 'ACTIVE', // ACTIVE, COUNTDOWN, SLEEP
    estopState: 'RELEASED', // RELEASED, PRESSED, FAULT
    coordView: 'WCS',    // WCS or MCS
    armed: false,        // Host armed status
    motionState: 'IDLE'  // IDLE, RUN, HOLD
};

// Machine configurations with official AIONMECH colors
const machineConfigs = {
    'FIRECONTROL': {
        name: 'FireControl',
        type: 'Plasma Cutting',
        color: '#ff6b35',
        icon: '🔥',
        machines: ['CrossFire', 'CrossFire PRO', 'CrossFire XR']
    },
    'CUTCONTROL': {
        name: 'CutControl',
        type: 'Milling Machine', 
        color: '#388e3c',
        icon: '⚙️',
        machines: ['MR-1']
    },
    'MACH3': {
        name: 'Mach3',
        type: 'Industrial CNC',
        color: '#1565c0',
        icon: '🔧',
        machines: ['Generic CNC']
    },
    'MACH4': {
        name: 'Mach4',
        type: 'Industrial CNC',
        color: '#1565c0',
        icon: '🔧',
        machines: ['Generic CNC']
    },
    'LINUXCNC': {
        name: 'LinuxCNC',
        type: 'Open Source CNC',
        color: '#c62828',
        icon: '🐧',
        machines: ['Generic CNC']
    },
    'UCCNC': {
        name: 'UCCNC',
        type: 'Motion Control',
        color: '#00838f',
        icon: '🎯',
        machines: ['Generic CNC']
    },
    'CARBIDE': {
        name: 'Carbide Motion',
        type: 'Desktop CNC',
        color: '#e65100',
        icon: '💎',
        machines: ['Shapeoko', 'Nomad']
    },
    'UGS': {
        name: 'UGS',
        type: 'GRBL Sender',
        color: '#00897b',
        icon: '📡',
        machines: ['GRBL']
    },
    'OPENBUILDS': {
        name: 'OpenBuilds',
        type: 'Open Source CNC',
        color: '#558b2f',
        icon: '🛠️',
        machines: ['OpenBuilds']
    },
    'CNCJS': {
        name: 'CNCjs',
        type: 'Web CNC Controller',
        color: '#6a1b9a',
        icon: '🌐',
        machines: ['GRBL', 'Marlin']
    },
    'MANUAL': {
        name: 'Manual',
        type: 'Override Mode',
        color: '#9c27b0',
        icon: '🎛️',
        machines: ['All Machines']
    }
};

// Theme presets with AIONMECH colors
const themePresets = {
    plasma: { bgR: 255, bgG: 107, bgB: 53, useWheel: false },   // AIONMECH orange
    mill: { bgR: 56, bgG: 142, bgB: 60, useWheel: false },      // AIONMECH green
    rainbow: { bgR: 0, bgG: 0, bgB: 0, useWheel: true, hue: 0 }
};

function addLog(message, type = 'info') {
    const logContainer = document.getElementById('log-container');
    const logEntry = document.createElement('div');
    logEntry.className = `log-entry ${type}`;
    logEntry.textContent = `${new Date().toLocaleTimeString()}: ${message}`;
    
    logContainer.appendChild(logEntry);
    logContainer.scrollTop = logContainer.scrollHeight;
    
    // Keep only last 50 entries
    while (logContainer.children.length > 50) {
        logContainer.removeChild(logContainer.firstChild);
    }
    
    // Highlight important machine-detection messages
    if (message.includes('AIONMECH') || message.includes('FireControl') || 
        message.includes('CutControl')) {
        logEntry.style.color = '#ff6b35';  // AIONMECH orange
        logEntry.style.fontWeight = '600';
    }
}

// Update status displays
function updateConnectionStatus(connected, port = null) {
    isConnected = connected;
    currentPort = port;
    
    const statusEl = document.getElementById('connection-status');
    const iconEl = document.getElementById('connection-icon');
    
    if (connected) {
        statusEl.textContent = port ? `Connected (${port})` : 'Connected';
        statusEl.className = 'status-value status-connected';
        iconEl.textContent = '✅';
        addLog(`Connected to pendant on ${port}`, 'info');
    } else {
        statusEl.textContent = 'Disconnected';
        statusEl.className = 'status-value status-disconnected';
        iconEl.textContent = '❌';
        addLog('Pendant disconnected', 'warning');
    }
}

function updateSoftwareStatus(software = 'NONE', displayName = null) {
    currentSoftware = software;
    
    const statusEl = document.getElementById('software-status');
    const iconEl = document.getElementById('software-icon');
    
    if (software !== 'NONE') {
        statusEl.textContent = displayName || software;
        statusEl.className = `status-value status-${software.toLowerCase()}`;
        iconEl.textContent = machineConfigs[software]?.icon || '💻';
        addLog(`Detected ${displayName || software}`, 'info');
    } else {
        statusEl.textContent = 'None Detected';
        statusEl.className = 'status-value status-disconnected';
        iconEl.textContent = '❌';
    }
    
    updateMachineCards();
}

function updateMachineStatus(machine = 'MANUAL') {
    currentMachine = machine;
    
    const statusEl = document.getElementById('machine-status');
    const iconEl = document.getElementById('machine-icon');
    const config = machineConfigs[machine];
    
    if (config) {
        statusEl.textContent = config.name;
        statusEl.className = `status-value status-${machine.toLowerCase()}`;
        iconEl.textContent = config.icon;
    }
    
    updateMachineCards();
}

function updateMachineCards() {
    const cards = document.querySelectorAll('.machine-card');
    
    cards.forEach(card => {
        const machineType = card.dataset.machine;
        card.classList.remove('active', 'detected');
        
        // Mark active machine
        if (machineType === currentMachine) {
            card.classList.add('active');
        }
        
        // Mark detected software
        if (machineType === currentSoftware) {
            card.classList.add('detected');
        }
    });
}

// Machine card click handlers
function setupMachineCards() {
    const cards = document.querySelectorAll('.machine-card');
    
    cards.forEach(card => {
        card.addEventListener('click', async () => {
            const machineType = card.dataset.machine;
            
            try {
                const result = await ipcRenderer.invoke('force-machine-type', machineType);
                if (result.success) {
                    updateMachineStatus(machineType);
                    addLog(`Manually set machine to ${machineType}`, 'info');
                }
            } catch (error) {
                addLog(`Failed to set machine type: ${error.message}`, 'error');
            }
        });
    });
}

// Color picker setup
function setupColorPicker() {
    const colorOptions = document.querySelectorAll('.color-option');
    
    colorOptions.forEach(option => {
        option.addEventListener('click', async () => {
            const [r, g, b] = option.dataset.color.split(',').map(Number);
            
            try {
                const theme = { bgR: r, bgG: g, bgB: b, useWheel: false };
                const result = await ipcRenderer.invoke('set-theme', theme);
                
                if (result.success) {
                    // Update active state
                    colorOptions.forEach(opt => opt.classList.remove('active'));
                    option.classList.add('active');
                    addLog(`Theme color set to RGB(${r}, ${g}, ${b})`, 'info');
                }
            } catch (error) {
                addLog(`Failed to set theme: ${error.message}`, 'error');
            }
        });
    });
}

// RGB wheel setup
function setupRGBWheel() {
    const hueSlider = document.getElementById('hue-slider');
    const enableBtn = document.getElementById('enable-wheel');
    const disableBtn = document.getElementById('disable-wheel');
    
    hueSlider.addEventListener('input', async (e) => {
        const hue = parseInt(e.target.value);
        
        try {
            const theme = { bgR: 0, bgG: 0, bgB: 0, useWheel: true, hue };
            await ipcRenderer.invoke('set-theme', theme);
        } catch (error) {
            console.error('Failed to update wheel:', error);
        }
    });
    
    enableBtn.addEventListener('click', async () => {
        const hue = parseInt(hueSlider.value);
        
        try {
            const theme = { bgR: 0, bgG: 0, bgB: 0, useWheel: true, hue };
            const result = await ipcRenderer.invoke('set-theme', theme);
            
            if (result.success) {
                addLog(`RGB wheel enabled (hue: ${hue}°)`, 'info');
            }
        } catch (error) {
            addLog(`Failed to enable RGB wheel: ${error.message}`, 'error');
        }
    });
    
    disableBtn.addEventListener('click', async () => {
        try {
            const theme = { bgR: 0, bgG: 0, bgB: 0, useWheel: false };
            const result = await ipcRenderer.invoke('set-theme', theme);
            
            if (result.success) {
                addLog('RGB wheel disabled', 'info');
            }
        } catch (error) {
            addLog(`Failed to disable RGB wheel: ${error.message}`, 'error');
        }
    });
}

// Theme preset buttons
function setupThemePresets() {
    const presetButtons = {
        'theme-plasma': themePresets.plasma,
        'theme-mill': themePresets.mill
    };
    
    Object.entries(presetButtons).forEach(([buttonId, theme]) => {
        const button = document.getElementById(buttonId);
        button.addEventListener('click', async () => {
            try {
                const result = await ipcRenderer.invoke('set-theme', theme);
                if (result.success) {
                    addLog(`Applied ${buttonId.replace('theme-', '')} theme`, 'info');
                }
            } catch (error) {
                addLog(`Failed to apply theme: ${error.message}`, 'error');
            }
        });
    });
}

// Control button setup
function setupControlButtons() {
    // Testing toggle and test action (hidden by default)
    const toggleTesting = document.getElementById('toggle-testing');
    const testBtn = document.getElementById('test-connection');
    if (toggleTesting && testBtn) {
        toggleTesting.addEventListener('change', () => {
            testBtn.style.display = toggleTesting.checked ? 'inline-block' : 'none';
        });
        testBtn.addEventListener('click', async () => {
            if (!toggleTesting.checked) return;
            try {
                const result = await ipcRenderer.invoke('test-connection');
                if (result.success) {
                    addLog('Test signal sent to pendant', 'info');
                } else {
                    addLog(result.message, 'warning');
                }
            } catch (error) {
                addLog(`Test failed: ${error.message}`, 'error');
            }
        });
    }
    
    document.getElementById('reconnect').addEventListener('click', async () => {
        addLog('Reconnecting to pendant...', 'info');
        try {
            await ipcRenderer.invoke('reconnect-serial');
        } catch (e) {
            addLog('Reconnect request sent', 'info');
        }
    });
    
    // System info
    document.getElementById('show-info').addEventListener('click', async () => {
        try {
            systemInfo = await ipcRenderer.invoke('get-system-info');
            showSystemInfoModal();
        } catch (error) {
            addLog(`Failed to get system info: ${error.message}`, 'error');
        }
    });
    
    document.getElementById('open-devtools').addEventListener('click', () => {
        ipcRenderer.send('open-devtools');
    });

    // Updates
    const updateStatus = document.getElementById('update-status');
    document.getElementById('check-updates').addEventListener('click', async () => {
        updateStatus.textContent = 'Checking for updates...';
        try {
            const result = await ipcRenderer.invoke('check-updates');
            if (result.success) {
                addLog('Update check complete', 'info');
            } else {
                addLog(`Update check failed: ${result.message}`, 'error');
            }
        } catch (e) {
            addLog(`Update check error: ${e.message}`, 'error');
        }
    });
    document.getElementById('install-update').addEventListener('click', async () => {
        updateStatus.textContent = 'Installing update...';
        try {
            const result = await ipcRenderer.invoke('install-update');
            if (result.success) {
                addLog('Installing update and restarting...', 'info');
            } else {
                addLog(`Install failed: ${result.message}`, 'error');
            }
        } catch (e) {
            addLog(`Install error: ${e.message}`, 'error');
        }
    });
    
    // Units controls
    document.getElementById('units-mm').addEventListener('click', async () => {
        document.getElementById('units-status').textContent = 'MM';
        try {
            await ipcRenderer.invoke('send-pendant-command', 'UNITS:MM');
        } catch (e) {}
        addLog('Units set to metric (mm)', 'info');
    });
    
    document.getElementById('units-inches').addEventListener('click', async () => {
        document.getElementById('units-status').textContent = 'IN';
        try {
            await ipcRenderer.invoke('send-pendant-command', 'UNITS:IN');
        } catch (e) {}
        addLog('Units set to imperial (inches)', 'info');
    });
    
    // Encoder scaling controls
    const encoderScaleSlider = document.getElementById('encoder-scale-slider');
    const encoderScaleValue = document.getElementById('encoder-scale-value');
    
    encoderScaleSlider.addEventListener('input', (e) => {
        encoderScaleValue.textContent = e.target.value;
    });
    
    document.getElementById('encoder-apply').addEventListener('click', async () => {
        const scale = parseInt(encoderScaleSlider.value);
        try {
            await ipcRenderer.invoke('send-pendant-command', `ENCODER:SCALE,${scale}`);
            addLog(`Encoder scaling set to ${scale} clicks per unit`, 'info');
        } catch (error) {
            addLog(`Failed to set encoder scaling: ${error.message}`, 'error');
        }
    });
    
    document.getElementById('encoder-status').addEventListener('click', async () => {
        try {
            await ipcRenderer.invoke('send-pendant-command', 'ENCODER:GET');
            addLog('Requested encoder status', 'info');
        } catch (error) {
            addLog(`Failed to get encoder status: ${error.message}`, 'error');
        }
    });
    
    // Position control
    document.getElementById('position-zero').addEventListener('click', async () => {
        try {
            await ipcRenderer.invoke('send-pendant-command', 'MACHINE:ZERO');
            addLog('All positions zeroed', 'info');
        } catch (error) {
            addLog(`Failed to zero positions: ${error.message}`, 'error');
        }
    });
    
    document.getElementById('position-sync').addEventListener('click', async () => {
        try {
            await ipcRenderer.invoke('send-pendant-command', 'POS:GET');
            addLog('Requested pendant positions', 'info');
        } catch (error) {
            addLog(`Failed to sync positions: ${error.message}`, 'error');
        }
    });
    // Real positioning inputs
    const posXInput = document.getElementById('pos-input-x');
    const posYInput = document.getElementById('pos-input-y');
    const posZInput = document.getElementById('pos-input-z');

    document.getElementById('apply-positions').addEventListener('click', async () => {
        try {
            const toSend = [];
            if (posXInput.value !== '') toSend.push(`POS:SET,,X,${parseFloat(posXInput.value)}`);
            if (posYInput.value !== '') toSend.push(`POS:SET,,Y,${parseFloat(posYInput.value)}`);
            if (posZInput.value !== '') toSend.push(`POS:SET,,Z,${parseFloat(posZInput.value)}`);
            for (const cmd of toSend) {
                await ipcRenderer.invoke('send-pendant-command', cmd);
            }
            if (toSend.length) addLog('Synced real machine positions to pendant', 'info');
            await ipcRenderer.invoke('send-pendant-command', 'POS:GET');
        } catch (error) {
            addLog(`Failed to apply positions: ${error.message}`, 'error');
        }
    });

    document.getElementById('clear-positions').addEventListener('click', () => {
        posXInput.value = '';
        posYInput.value = '';
        posZInput.value = '';
    });

    
    // Machine detection
    document.getElementById('auto-detect').addEventListener('click', async () => {
        addLog('Scanning for CNC software...', 'info');
        try {
            const status = await ipcRenderer.invoke('get-pendant-status');
            updateConnectionStatus(status.connected, status.port);
            updateMachineStatus(status.machine);
            updateSoftwareStatus(status.software);
            addLog(`Detection complete: ${status.machine} (${status.software})`, 'info');
        } catch (error) {
            addLog(`Detection failed: ${error.message}`, 'error');
        }
    });
    
    document.getElementById('refresh-machines').addEventListener('click', async () => {
        try {
            const status = await ipcRenderer.invoke('get-pendant-status');
            updateConnectionStatus(status.connected, status.port);
            updateMachineStatus(status.machine);
            updateSoftwareStatus(status.software);
            addLog('Status refreshed', 'info');
        } catch (error) {
            addLog(`Failed to refresh: ${error.message}`, 'error');
        }
    });
}
    // Live position updates from pendant
    ipcRenderer.on('position-update', (_evt, { axis, value }) => {
        if (axis === 'X') { posX = value; document.getElementById('pos-x').textContent = value.toFixed(4); }
        else if (axis === 'Y') { posY = value; document.getElementById('pos-y').textContent = value.toFixed(4); }
        else if (axis === 'Z') { posZ = value; document.getElementById('pos-z').textContent = value.toFixed(4); }
    });

    ipcRenderer.on('encoder-scale', (_evt, scale) => {
        const slider = document.getElementById('encoder-scale-slider');
        const val = document.getElementById('encoder-scale-value');
        if (slider) slider.value = String(scale);
        if (val) val.textContent = String(scale);
        addLog(`Encoder scale confirmed: ${scale}`, 'info');
    });

    ipcRenderer.on('position-known', (_evt, known) => {
        addLog(`Machine position known: ${known ? 'Yes' : 'No'}`, 'info');
    });


// System info modal
function showSystemInfoModal() {
    if (!systemInfo) return;
    
    const modal = document.createElement('div');
    modal.style.cssText = `
        position: fixed;
        top: 0;
        left: 0;
        width: 100%;
        height: 100%;
        background: rgba(0, 0, 0, 0.8);
        display: flex;
        justify-content: center;
        align-items: center;
        z-index: 1000;
    `;
    
    const content = document.createElement('div');
    content.style.cssText = `
        background: var(--secondary-bg);
        border-radius: 16px;
        padding: 2rem;
        max-width: 500px;
        width: 90%;
        border: 1px solid rgba(255, 255, 255, 0.08);
    `;
    
    content.innerHTML = `
        <h3 style="margin-bottom: 1rem; color: var(--text-primary);">System Information</h3>
        <div style="font-family: monospace; font-size: 0.9rem; line-height: 1.6; color: var(--text-secondary);">
            <div><strong>Platform:</strong> ${systemInfo.platform} ${systemInfo.arch}</div>
            <div><strong>OS Version:</strong> ${systemInfo.version}</div>
            <div><strong>Node.js:</strong> ${systemInfo.node}</div>
            <div><strong>Electron:</strong> ${systemInfo.electron}</div>
            <div><strong>Chrome:</strong> ${systemInfo.chrome}</div>
            <div><strong>RobotJS:</strong> ${systemInfo.robotjs}</div>
            <div><strong>Current Machine:</strong> ${systemInfo.currentMachine}</div>
            <div><strong>Current Software:</strong> ${systemInfo.currentSoftware}</div>
            <div><strong>Connection:</strong> ${systemInfo.isConnected ? 'Connected' : 'Disconnected'}</div>
            <div><strong>Supported Machines:</strong></div>
            <ul style="margin: 0.5rem 0; padding-left: 1.5rem;">
                ${systemInfo.supportedMachines.map(machine => `<li>${machine}</li>`).join('')}
            </ul>
        </div>
        <button id="close-modal" style="
            margin-top: 1rem;
            padding: 0.5rem 1rem;
            background: var(--aionmech-green);
            border: none;
            border-radius: 8px;
            color: white;
            cursor: pointer;
        ">Close</button>
    `;
    
    modal.appendChild(content);
    document.body.appendChild(modal);
    
    document.getElementById('close-modal').addEventListener('click', () => {
        document.body.removeChild(modal);
    });
    
    modal.addEventListener('click', (e) => {
        if (e.target === modal) {
            document.body.removeChild(modal);
        }
    });
}

// IPC event handlers
ipcRenderer.on('connection-status', (event, data) => {
    updateConnectionStatus(data.connected, data.port);
});

ipcRenderer.on('software-detected', (event, data) => {
    updateSoftwareStatus(data.machine, data.displayName);
});

ipcRenderer.on('software-disconnected', () => {
    updateSoftwareStatus('NONE');
});

ipcRenderer.on('machine-changed', (event, machine) => {
    updateMachineStatus(machine);
});

ipcRenderer.on('axis-changed', (event, axis) => {
    addLog(`Pendant axis changed to: ${axis}`, 'info');
});

ipcRenderer.on('distance-changed', (event, distance) => {
    addLog(`Pendant jog distance changed to: ${distance}`, 'info');
});

ipcRenderer.on('feedrate-changed', (event, feedrate) => {
    addLog(`Pendant feedrate changed to: ${feedrate}%`, 'info');
});

ipcRenderer.on('coord-view-changed', (event, view) => {
    addLog(`Pendant coordinate view changed to: ${view}`, 'info');
});

ipcRenderer.on('power-state-changed', (event, state) => {
    addLog(`Pendant power state: ${state}`, state === 'SLEEP' ? 'warning' : 'info');
    if (state === 'SLEEP') {
        document.body.classList.add('pendant-sleep');
    } else {
        document.body.classList.remove('pendant-sleep');
    }
});

ipcRenderer.on('estop-event', (event, estopEvent) => {
    if (estopEvent === 'PRESSED' || estopEvent === 'EMERGENCY_STOP_ACTIVATED') {
        addLog(`🚨 EMERGENCY STOP ACTIVATED - All motion blocked!`, 'error');
        document.body.classList.add('estop-active');
        
        // Show emergency notification
        showEmergencyNotification('EMERGENCY STOP ACTIVATED', 'All machine motion has been stopped. Release E-stop and manually reset CNC software.');
        
    } else if (estopEvent === 'RELEASED' || estopEvent === 'EMERGENCY_STOP_RELEASED') {
        addLog(`✅ Emergency stop released - Ready for manual reset`, 'warning');
        document.body.classList.remove('estop-active');
        
        showEmergencyNotification('Emergency Stop Released', 'E-stop has been released. Manually reset your CNC software before resuming operations.');
    } else if (estopEvent === 'MOTION_BLOCKED') {
        addLog(`⚠️ Motion blocked - E-stop active`, 'warning');
    }
});

function showEmergencyNotification(title, message) {
    // Create emergency modal if it doesn't exist
    let modal = document.getElementById('emergency-modal');
    if (!modal) {
        modal = document.createElement('div');
        modal.id = 'emergency-modal';
        modal.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0, 0, 0, 0.9);
            z-index: 10000;
            display: flex;
            align-items: center;
            justify-content: center;
        `;
        document.body.appendChild(modal);
    }
    
    modal.innerHTML = `
        <div style="
            background: var(--error);
            color: white;
            padding: 30px;
            border-radius: 10px;
            text-align: center;
            max-width: 500px;
            margin: 20px;
            box-shadow: 0 0 30px rgba(244, 67, 54, 0.5);
        ">
            <h2 style="margin: 0 0 20px 0; font-size: 24px;">${title}</h2>
            <p style="margin: 0 0 20px 0; font-size: 16px; line-height: 1.4;">${message}</p>
            <button onclick="document.getElementById('emergency-modal').style.display='none'" style="
                background: white;
                color: var(--error);
                border: none;
                padding: 10px 20px;
                border-radius: 5px;
                font-weight: bold;
                cursor: pointer;
            ">Acknowledge</button>
        </div>
    `;
    modal.style.display = 'flex';
}

// Startup animations
function startupAnimations() {
    // Animate machine cards
    const cards = document.querySelectorAll('.machine-card');
    cards.forEach((card, index) => {
        setTimeout(() => {
            card.style.transform = 'scale(1.1)';
            setTimeout(() => {
                card.style.transform = 'scale(1)';
            }, 200);
        }, index * 100);
    });
    
    // Pulse connection status
    setTimeout(() => {
        const connectionIcon = document.getElementById('connection-icon');
        connectionIcon.classList.add('pulse');
    }, 1000);
}

// Periodic status updates
function startStatusUpdates() {
    setInterval(async () => {
        try {
            const status = await ipcRenderer.invoke('get-pendant-status');
            
            // Only update if status changed
            if (status.connected !== isConnected) {
                updateConnectionStatus(status.connected, status.port);
            }
            
            if (status.machine !== currentMachine) {
                updateMachineStatus(status.machine);
            }
            
            if (status.software !== currentSoftware) {
                updateSoftwareStatus(status.software);
            }
        } catch (error) {
            // Silently handle errors during periodic updates
        }
    }, 2000);
}

// Initialize application
document.addEventListener('DOMContentLoaded', () => {
    addLog('Universal Pendant Controller UI loaded', 'info');
    
    setupMachineCards();
    setupColorPicker();
    setupRGBWheel();
    setupThemePresets();
    setupControlButtons();
    
    startupAnimations();
    startStatusUpdates();
    
    // Initial status request
    setTimeout(async () => {
        try {
            const status = await ipcRenderer.invoke('get-pendant-status');
            updateConnectionStatus(status.connected, status.port);
            updateMachineStatus(status.machine);
            updateSoftwareStatus(status.software);
        } catch (error) {
            addLog(`Failed to get initial status: ${error.message}`, 'error');
        }
    }, 1000);
    
    addLog('Ready for all supported machines', 'info');
});

// Handle window resize for responsive design
window.addEventListener('resize', () => {
    // Adjust layout for smaller screens
    const container = document.querySelector('.app');
    if (!container) return;
    // Responsive adjustments handled by CSS media queries
});

console.log('Universal AIONMECH CNC Pendant Controller - Renderer Ready');
console.log('Supported machines: FireControl, CutControl, Mach3, Mach4, LinuxCNC, UCCNC, Carbide Motion, UGS, OpenBuilds, CNCjs');

// Update event wiring from main
ipcRenderer.on('update-status', () => {
    const el = document.getElementById('update-status');
    if (el) el.textContent = 'Checking for updates...';
});
ipcRenderer.on('update-available', () => {
    const el = document.getElementById('update-status');
    if (el) el.textContent = 'Update available. Downloading...';
});
ipcRenderer.on('update-not-available', () => {
    const el = document.getElementById('update-status');
    if (el) el.textContent = 'No updates available.';
});
ipcRenderer.on('update-download-progress', (_e, p) => {
    const el = document.getElementById('update-status');
    if (el) el.textContent = `Downloading: ${Math.round(p.percent || 0)}%`;
});
ipcRenderer.on('update-downloaded', () => {
    const el = document.getElementById('update-status');
    if (el) el.textContent = 'Update ready. Click Install to apply.';
});
ipcRenderer.on('update-error', (_e, info) => {
    const el = document.getElementById('update-status');
    if (el) el.textContent = `Update error: ${info?.message || 'Unknown error'}`;
});

// Update pendant state display
function updatePendantState(key, value) {
    if (pendantState.hasOwnProperty(key)) {
        pendantState[key] = value;
        
        // Update UI elements
        switch(key) {
            case 'axis':
                const axisEl = document.getElementById('current-axis');
                if (axisEl) {
                    axisEl.textContent = `${value}-AXIS`;
                    axisEl.style.color = value === 'X' ? '#f44336' : value === 'Y' ? '#4caf50' : '#2196f3';
                }
                addLog(`Axis changed to ${value}`, 'info');
                break;
                
            case 'distance':
                const distEl = document.getElementById('current-distance');
                if (distEl) distEl.textContent = value;
                addLog(`Jog distance: ${value}`, 'info');
                break;
                
            case 'speed':
                const speedEl = document.getElementById('current-speed');
                if (speedEl) speedEl.textContent = value;
                addLog(`Speed/Feed: ${value}`, 'info');
                break;
                
            case 'powerState':
                const powerEl = document.getElementById('power-state');
                if (powerEl) {
                    powerEl.textContent = value;
                    powerEl.style.color = value === 'ACTIVE' ? '#4caf50' : 
                                         value === 'COUNTDOWN' ? '#ff9800' : '#757575';
                }
                if (value === 'SLEEP') {
                    document.body.classList.add('pendant-sleep');
                    addLog('Pendant entered sleep mode', 'warning');
                } else {
                    document.body.classList.remove('pendant-sleep');
                    if (value === 'ACTIVE') addLog('Pendant active', 'info');
                }
                break;
                
            case 'estopState':
                const estopEl = document.getElementById('estop-state');
                if (estopEl) {
                    estopEl.textContent = value;
                    estopEl.style.color = value === 'RELEASED' ? '#4caf50' : '#f44336';
                }
                if (value === 'PRESSED') {
                    document.body.classList.add('estop-active');
                    addLog('🚨 EMERGENCY STOP ACTIVATED 🚨', 'error');
                } else {
                    document.body.classList.remove('estop-active');
                    if (value === 'RELEASED') addLog('E-Stop released', 'info');
                }
                break;
        }
    }
}

// Enhanced serial message processing
ipcRenderer.on('serial-data', (event, data) => {
    addLog(`ESP32: ${data}`, 'info');
    
    // Parse pendant state updates
    if (data.startsWith('AXIS:')) {
        updatePendantState('axis', data.split(':')[1]);
    } else if (data.startsWith('DISTANCE:')) {
        const dist = parseFloat(data.split(':')[1]);
        const units = pendantState.units || 'mm';
        updatePendantState('distance', `${dist}${units}`);
    } else if (data.startsWith('SPEED:')) {
        updatePendantState('speed', data.split(':')[1]);
    } else if (data.startsWith('FEEDRATE:')) {
        updatePendantState('speed', data.split(':')[1]);
    } else if (data.startsWith('POWER:')) {
        updatePendantState('powerState', data.split(':')[1]);
    } else if (data.startsWith('ESTOP:')) {
        const state = data.split(':')[1];
        updatePendantState('estopState', state.includes('PRESSED') ? 'PRESSED' : 'RELEASED');
    }
});