/*
  UNIVERSAL CNC PENDANT - ESP32 WROOM with Rotary Encoder
  Compatible with ALL major CNC control software:
  
  🔥 LANGMUIR SYSTEMS:
  - FireControl (CrossFire, CrossFire PRO, CrossFire XR Plasma)
  - CutControl (MR-1 Gantry Mill)
  
  🔧 INDUSTRIAL CNC SOFTWARE:
  - Mach3 / Mach4 (Artsoft CNC)
  - LinuxCNC (Open-source CNC control)
  - UCCNC (CNCdrive motion control)
  
  🛠️ HOBBY/PROSUMER CNC:
  - Carbide Motion (Shapeoko, Nomad)
  - Universal G-Code Sender (GRBL-based machines)
  - OpenBuilds CONTROL
  - CNCjs
  
  📐 SPECIALTY MACHINES:
  - 3D Printers (with jog support)
  - Laser Cutters/Engravers
  - Plasma Tables
  - Router Tables

  UNIVERSAL KEYBOARD SHORTCUTS (Arrow Keys + Page Up/Down):
  All CNC software uses standard keyboard shortcuts:
  - Jog X+: Right Arrow       - Jog X−: Left Arrow
  - Jog Y+: Up Arrow          - Jog Y−: Down Arrow  
  - Jog Z+: Page Up           - Jog Z−: Page Down
  - Common controls: Tab, Space, Enter, Escape
  
  SOFTWARE-SPECIFIC FEATURES:
  - CutControl: 1-4 keys for distance, F1-F4 for feed rate
  - Mach3/4: Keyboard macros supported
  - LinuxCNC: Standard keyboard jogging
  - GRBL: Arrow keys via sender software

  Hardware:
  - ESP32-D 38-pin development board with USB-C
  - 5V 4-Terminal Manual Pulse Encoder (60mm rotary knob)
  - Momentary push button for mode changes
  - GC9A1 240x240 round LCD display

  Control scheme:
  - Rotary encoder: Move in selected axis (sends official shortcuts)
  - Single press: Cycle axis (Z → Y → X → Z)
  - Double press: Cycle jog distance/step size
  - Triple press: Cycle feed rate/speed
  - Long hold: Enter settings mode (machine type selection)

  AUTO-DETECTION: Windows app detects CNC software and configures pendant:
  - Langmuir: FireControl (Plasma), CutControl (Mill)
  - Industrial: Mach3, Mach4, LinuxCNC, UCCNC  
  - Hobby: Carbide Motion, UGS, OpenBuilds, CNCjs
  - Manual mode for any other software

  Pin map (ESP32-D 38-pin with USB-C):
  - Encoder A: GPIO32 (with interrupt)
  - Encoder B: GPIO33 (for direction)
  - Mode button: GPIO27 (to GND, INPUT_PULLUP)
  - GC9A1: SCL=18, SDA=23, CS=15, DC=2, RST=21, BL=16

  Serial Protocol (115200 baud):
  FROM ESP32:
  - KEY:LEFT/RIGHT/UP/DOWN,1/0 (axis movement press/release)
  - KEY:PGUP/PGDN,1/0 (Z axis movement press/release)
  - KEY:1/2/3/4,1 (jog distance - software specific)
  - KEY:F1/F2/F3/F4,1 (feed rate - software specific)
  - KEY:TAB,1 (toggle nudge jog)
  - KEY:ALT_R/SPACE/ALT_S,1/0 (program control)
  - MACHINE:<name> (current machine type)
  - AXIS:X/Y/Z (axis change notification)
  - PENDANT:READY (startup signal)

  TO ESP32:
  - MACHINE:<type> (set mode - see MachineType enum)
  - LCD:SOLID,R,G,B (set solid background color)
  - LCD:WHEEL,H (set hue-based RGB wheel background)
  - LCD:BRIGHTNESS,B (set display brightness 0-255)
  - UNITS:MM/INCHES (unit preference)
  - SOFTWARE:CONNECTED/DISCONNECTED (software status)

  BATTERY MODE (Hidden - Uncomment to enable):
  - Low power management for 600mAh battery operation
  - Auto sleep after inactivity timeout (default 30 seconds)
  - Deep sleep mode reduces current to ~10µA
  - Wake on encoder movement or button press
  - Dimmed display and reduced refresh rates
  - Expected battery life: 48-72 hours with moderate use

  BLUETOOTH MODE (Hidden - Uncomment both to enable wireless):
  - BLE HID (Human Interface Device) for low latency
  - Connects to Windows PC as wireless keyboard
  - Auto-reconnect on power-up and wake from sleep
  - ~20ms latency for real-time jog control
  - Compatible with all Langmuir software (sends standard key codes)
  - Range: ~30 feet (10 meters) line of sight

  Build notes:
  - Install LovyanGFX via Library Manager
  - Select Board: ESP32 Dev Module (ESP32-WROOM)
  - Works with ALL Langmuir software versions
*/

#include <LovyanGFX.hpp>
// Uncomment the line below for battery-powered operation
// #define BATTERY_MODE
// Uncomment the line below for wireless Bluetooth operation (requires BATTERY_MODE)
// #define BLUETOOTH_MODE

#ifdef BLUETOOTH_MODE
#include "BLEDevice.h"
#include "BLEHIDDevice.h"
#include "HIDKeyboardTypes.h"
#include "BLECharacteristic.h"
#endif

// Pin Definitions for ESP32 WROOM 38-pin (USB-C)
#define PIN_ENCODER_A   32  // Encoder Phase A (with interrupt)
#define PIN_ENCODER_B   33  // Encoder Phase B (for direction)
#define PIN_MODE_BTN    27  // Mode Button (to GND, INPUT_PULLUP)

// GC9A1 LCD Pin Definitions
#define PIN_LCD_SCLK    18  // SCL → GPIO18 (SPI Clock)
#define PIN_LCD_MOSI    23  // SDA → GPIO23 (SPI MOSI)
#define PIN_LCD_CS      15  // CS  → GPIO15 (Chip Select)
#define PIN_LCD_DC      2   // DC  → GPIO2  (Data/Command)
#define PIN_LCD_RST     21  // RST → GPIO21 (Reset)
#define PIN_LCD_BL      16  // BL  → GPIO16 (Backlight PWM)

// Machine Types - Universal CNC Software Support
enum MachineType {
  MACHINE_FIRECONTROL = 0,  // Langmuir Plasma (CrossFire, PRO, XR)
  MACHINE_CUTCONTROL = 1,   // Langmuir Mill (MR-1)
  MACHINE_MACH3 = 2,        // Artsoft Mach3
  MACHINE_MACH4 = 3,        // Artsoft Mach4  
  MACHINE_LINUXCNC = 4,     // LinuxCNC (EMC2)
  MACHINE_UCCNC = 5,        // CNCdrive UCCNC
  MACHINE_CARBIDE = 6,      // Carbide Motion (Shapeoko/Nomad)
  MACHINE_UGS = 7,          // Universal G-Code Sender
  MACHINE_OPENBUILDS = 8,   // OpenBuilds CONTROL
  MACHINE_CNCJS = 9,        // CNCjs
  MACHINE_MANUAL = 10       // Manual/Generic mode
};

const int MACHINE_TYPE_COUNT = 11;  // Total number of machine types

// Axis Selection
enum AxisType {
  AXIS_Z = 0,    // Default: Z axis (Page Up/Down)
  AXIS_Y = 1,    // Y axis (Up/Down arrows)
  AXIS_X = 2     // X axis (Left/Right arrows)
};

// Speed Settings
enum SpeedType {
  SPEED_SLOW = 0,    // Precision movements
  SPEED_MEDIUM = 1,  // Normal speed
  SPEED_FAST = 2     // Rapid movements
};

// Jog Distance Settings (Universal)
enum JogDistance {
  JOG_FINE = 0,      // Fine: 1/16" or 0.1mm
  JOG_SMALL = 1,     // Small: 1/8" or 1mm  
  JOG_MEDIUM = 2,    // Medium: 1/4" or 10mm
  JOG_LARGE = 3      // Large: 1" or 100mm
};

// Feed Rate Settings (CutControl specific)
enum FeedRate {
  FEED_25 = 0,       // 25% speed
  FEED_50 = 1,       // 50% speed  
  FEED_75 = 2,       // 75% speed
  FEED_100 = 3       // 100% speed
};

#ifdef BATTERY_MODE
// Battery Power Management
enum PowerState {
  POWER_ACTIVE = 0,      // Normal operation
  POWER_DIM = 1,         // Dimmed display
  POWER_SLEEP = 2,       // Light sleep mode
  POWER_DEEP_SLEEP = 3   // Deep sleep mode
};
#endif

#ifdef BLUETOOTH_MODE
// Bluetooth HID variables
BLEHIDDevice* hid;
BLECharacteristic* input;
BLECharacteristic* output;
BLECharacteristic* feature;

bool bleConnected = false;
bool bleStarted = false;
unsigned long lastConnectionAttempt = 0;
const unsigned long BLE_RECONNECT_INTERVAL = 5000; // Try reconnect every 5 seconds
const char* deviceName = "Langmuir Pendant";

// Key mapping for BLE HID
enum HIDKeys {
  HID_LEFT = 0x50,      // Left Arrow
  HID_RIGHT = 0x4F,     // Right Arrow  
  HID_UP = 0x52,        // Up Arrow
  HID_DOWN = 0x51,      // Down Arrow
  HID_PGUP = 0x4B,      // Page Up
  HID_PGDN = 0x4E,      // Page Down
  HID_TAB = 0x2B,       // Tab
  HID_SPACE = 0x2C,     // Space
  HID_F1 = 0x3A,        // F1
  HID_F2 = 0x3B,        // F2
  HID_F3 = 0x3C,        // F3
  HID_F4 = 0x3D,        // F4
  HID_1 = 0x1E,         // 1
  HID_2 = 0x1F,         // 2
  HID_3 = 0x20,         // 3
  HID_4 = 0x21          // 4
};

// BLE connection callback
class BLEConnectionCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) {
    bleConnected = true;
    Serial.println("BLE: Client connected");
  }
  
  void onDisconnect(BLEServer* server) {
    bleConnected = false;
    Serial.println("BLE: Client disconnected - starting advertising");
    server->startAdvertising();
  }
};
#endif

// Global Variables
MachineType currentMachine = MACHINE_FIRECONTROL;  // Default to FireControl
AxisType currentAxis = AXIS_Z;
SpeedType currentSpeed = SPEED_MEDIUM;
JogDistance currentJogDistance = JOG_SMALL;    // Default to small steps
FeedRate currentFeedRate = FEED_50;            // Default to 50%
volatile long encoderPosition = 0;
volatile bool encoderMoved = false;
int lastEncoderPos = 0;

// Units and software integration
bool useImperialUnits = false;  // Default to metric (mm)
bool softwareConnected = false;

// Button handling with multi-press detection
unsigned long btnPressTime = 0;
unsigned long lastMultiPressTime = 0;
bool modeButtonPressed = false;
bool modeButtonProcessed = false;
int btnPressCount = 0;
const unsigned long MULTI_PRESS_WINDOW = 500;  // ms window for multi-press detection
const unsigned long LONG_PRESS_THRESHOLD = 2000; // ms for machine selection mode

// Timing constants
const unsigned long DEBOUNCE_DELAY = 12;      // ms for switch debouncing
const unsigned long MOVEMENT_TIMEOUT = 150;   // ms between encoder movements

// UI Animation variables
unsigned long lastPressFlashTime = 0;
int pressFlashCount = 0;
bool showPressIndicator = false;
int lastJogDirection = 0;  // -1 = negative, 0 = none, 1 = positive

#ifdef BATTERY_MODE
// Battery power management variables
PowerState currentPowerState = POWER_ACTIVE;
unsigned long lastActivityTime = 0;
unsigned long lastHeartbeat = 0;
const unsigned long INACTIVITY_TIMEOUT = 30000;     // 30 seconds to dim
const unsigned long SLEEP_TIMEOUT = 60000;          // 60 seconds to sleep
const unsigned long DEEP_SLEEP_TIMEOUT = 300000;    // 5 minutes to deep sleep
const unsigned long HEARTBEAT_INTERVAL = 10000;     // Battery status every 10s
const int BATTERY_BRIGHTNESS = 128;                 // Dimmed brightness (50%)
const int NORMAL_BRIGHTNESS = 200;                  // Normal brightness (78%)
bool batteryLowPowerMode = false;
int batteryLevel = 100;  // Simulated battery level (0-100%)

// Wake-up detection
RTC_DATA_ATTR int bootCount = 0;
#endif

// Background settings
uint8_t bgR = 0, bgG = 0, bgB = 0;
bool useRGBWheel = false;
float rgbHue = 0.0;

// ===================== 2026 THEME =====================
static inline uint16_t RGB565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Universal theme colors
const uint16_t THEME_BG_DARK      = RGB565(8, 10, 14);
const uint16_t THEME_CARD         = RGB565(20, 26, 36);
const uint16_t THEME_RING_1       = RGB565(50, 60, 80);
const uint16_t THEME_RING_2       = RGB565(90, 110, 140);
const uint16_t THEME_RING_3       = RGB565(140, 160, 190);
const uint16_t THEME_TEXT_MAIN    = RGB565(240, 245, 255);
const uint16_t THEME_TEXT_SUB     = RGB565(160, 175, 200);

// Machine-specific accent colors
// Machine-specific accent colors
const uint16_t THEME_PLASMA_ORANGE = RGB565(255, 140, 0);   // FireControl (Langmuir Plasma)
const uint16_t THEME_MILL_GREEN    = RGB565(0, 220, 120);   // CutControl (Langmuir Mill)
const uint16_t THEME_MACH_BLUE     = RGB565(0, 120, 255);   // Mach3/Mach4 (Artsoft blue)
const uint16_t THEME_LINUX_RED     = RGB565(220, 50, 50);   // LinuxCNC (penguin red)
const uint16_t THEME_UCCNC_CYAN    = RGB565(0, 200, 220);   // UCCNC (cyan)
const uint16_t THEME_CARBIDE_ORANGE= RGB565(255, 100, 0);   // Carbide Motion
const uint16_t THEME_UGS_TEAL      = RGB565(0, 180, 160);   // Universal G-Code Sender
const uint16_t THEME_OPENBUILDS_GREEN = RGB565(100, 200, 50); // OpenBuilds
const uint16_t THEME_CNCJS_PURPLE  = RGB565(140, 80, 200);  // CNCjs
const uint16_t THEME_MANUAL_GRAY   = RGB565(120, 130, 150); // Manual/Generic mode

// Forward declarations
uint16_t getMachineAccentColor();
const char* getMachineName();
const char* getMachineDisplayName();

// LCD Configuration for GC9A1
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel_instance;
  lgfx::Bus_SPI _bus_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = VSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_LCD_SCLK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = PIN_LCD_DC;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = PIN_LCD_CS;
      cfg.pin_rst = PIN_LCD_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;
      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};

LGFX lcd;

// ===== Glass / Transparency Simulation Helpers =====
static inline void c565_to_rgb8(uint16_t c, uint8_t &r, uint8_t &g, uint8_t &b) {
  r = ((c >> 11) & 0x1F) * 255 / 31;
  g = ((c >> 5) & 0x3F) * 255 / 63;
  b = (c & 0x1F) * 255 / 31;
}
static inline uint16_t rgb8_to_565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static inline uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t alpha) {
  if (alpha == 255) return fg;
  if (alpha == 0) return bg;
  uint8_t fr, fg8, fb, br, bg8, bb;
  c565_to_rgb8(fg, fr, fg8, fb);
  c565_to_rgb8(bg, br, bg8, bb);
  uint8_t r = (uint8_t)((fr * alpha + br * (255 - alpha)) / 255);
  uint8_t g = (uint8_t)((fg8 * alpha + bg8 * (255 - alpha)) / 255);
  uint8_t b = (uint8_t)((fb * alpha + bb * (255 - alpha)) / 255);
  return rgb8_to_565(r, g, b);
}

void drawGlassCirclePanel(int cx, int cy, int r, uint16_t tint, uint16_t bgBase) {
  const int layers = 14;
  for (int i = 0; i < layers; ++i) {
    int rr = r - i;
    if (rr <= 0) break;
    uint8_t alpha = (uint8_t)(200 - (i * 180 / layers));
    uint16_t col = blend565(tint, bgBase, alpha);
    lcd.drawCircle(cx, cy, rr, col);
    if (i % 2 == 0) {
      lcd.fillCircle(cx, cy, rr-1, blend565(tint, bgBase, (uint8_t)min(220, (int)alpha + 20)));
    }
  }
  lcd.drawCircle(cx-6, cy-10, r-10, blend565(RGB565(255,255,255), bgBase, 90));
  lcd.fillCircle(cx-12, cy-18, r/6, blend565(RGB565(255,255,255), bgBase, 70));
}

void drawGlassPill(int x, int y, int w, int h, uint16_t tint, uint16_t bgBase, uint16_t outline) {
  lcd.fillRoundRect(x+1, y+2, w, h, h/2, blend565(RGB565(0,0,0), bgBase, 140));
  const int insetLayers = 8;
  for (int i = 0; i < insetLayers; ++i) {
    int ix = x + i;
    int iy = y + i;
    int iw = w - i*2;
    int ih = h - i*2;
    if (iw <= 0 || ih <= 0) break;
    uint8_t alpha = (uint8_t)(160 - abs(i - insetLayers/2) * 30);
    uint16_t col = blend565(tint, bgBase, alpha);
    lcd.fillRoundRect(ix, iy, iw, ih, ih/2, col);
  }
  lcd.fillRoundRect(x+3, y+3, w-6, h/3, (h/3)/2, blend565(RGB565(255,255,255), bgBase, 80));
  lcd.drawRoundRect(x, y, w, h, h/2, outline);
}

// Draw directional chevron/arrow
void drawChevron(int cx, int cy, int size, float angle, uint16_t color, bool filled) {
  float rad = angle * PI / 180.0;
  float cosA = cos(rad);
  float sinA = sin(rad);
  
  // Arrow points
  int tipX = cx + (int)(size * cosA);
  int tipY = cy + (int)(size * sinA);
  int leftX = cx + (int)(size * 0.5 * cos(rad + 2.5));
  int leftY = cy + (int)(size * 0.5 * sin(rad + 2.5));
  int rightX = cx + (int)(size * 0.5 * cos(rad - 2.5));
  int rightY = cy + (int)(size * 0.5 * sin(rad - 2.5));
  
  if (filled) {
    lcd.fillTriangle(tipX, tipY, leftX, leftY, rightX, rightY, color);
  } else {
    lcd.drawLine(tipX, tipY, leftX, leftY, color);
    lcd.drawLine(tipX, tipY, rightX, rightY, color);
  }
}

// Draw directional arrows around center based on current axis
void drawDirectionalArrows(int activeDirection) {
  uint16_t accentColor = getMachineAccentColor();
  uint16_t dimColor = blend565(accentColor, THEME_BG_DARK, 60);
  uint16_t brightColor = blend565(accentColor, RGB565(255,255,255), 180);
  
  int cx = 120, cy = 120;
  int arrowDist = 85;  // Distance from center
  int arrowSize = 12;
  
  // Determine which arrows to highlight based on axis
  bool showHorizontal = (currentAxis == AXIS_X);
  bool showVertical = (currentAxis == AXIS_Y);
  bool showUpDown = (currentAxis == AXIS_Z);
  
  // Draw 4 directional arrows
  // Right arrow (X+)
  uint16_t rightColor = showHorizontal ? (activeDirection > 0 ? brightColor : accentColor) : dimColor;
  drawChevron(cx + arrowDist, cy, arrowSize, 0, rightColor, activeDirection > 0 && showHorizontal);
  
  // Left arrow (X-)
  uint16_t leftColor = showHorizontal ? (activeDirection < 0 ? brightColor : accentColor) : dimColor;
  drawChevron(cx - arrowDist, cy, arrowSize, 180, leftColor, activeDirection < 0 && showHorizontal);
  
  // Up arrow (Y+ or Z+)
  uint16_t upColor = (showVertical || showUpDown) ? (activeDirection > 0 ? brightColor : accentColor) : dimColor;
  drawChevron(cx, cy - arrowDist, arrowSize, -90, upColor, activeDirection > 0 && (showVertical || showUpDown));
  
  // Down arrow (Y- or Z-)
  uint16_t downColor = (showVertical || showUpDown) ? (activeDirection < 0 ? brightColor : accentColor) : dimColor;
  drawChevron(cx, cy + arrowDist, arrowSize, 90, downColor, activeDirection < 0 && (showVertical || showUpDown));
  
  // Draw axis labels near arrows
  lcd.setFont(&fonts::Font0);
  lcd.setTextDatum(middle_center);
  
  if (showHorizontal) {
    lcd.setTextColor(accentColor, THEME_BG_DARK);
    lcd.drawString("+", cx + arrowDist + 18, cy);
    lcd.drawString("-", cx - arrowDist - 18, cy);
  } else if (showVertical) {
    lcd.setTextColor(accentColor, THEME_BG_DARK);
    lcd.drawString("+", cx, cy - arrowDist - 14);
    lcd.drawString("-", cx, cy + arrowDist + 14);
  } else if (showUpDown) {
    lcd.setTextColor(accentColor, THEME_BG_DARK);
    lcd.drawString("UP", cx, cy - arrowDist - 14);
    lcd.drawString("DN", cx, cy + arrowDist + 14);
  }
}

// Draw speed arc indicator (replaces pill)
void drawSpeedArc() {
  uint16_t accentColor = getMachineAccentColor();
  int cx = 120, cy = 120;
  int outerR = 112;
  int innerR = 100;
  
  // Clear the arc area first to prevent artifacts
  for (float a = 205; a <= 335; a += 1.5) {
    float rad = a * PI / 180.0;
    for (int r = innerR - 1; r <= outerR + 1; r++) {
      int x = cx + (int)(r * cos(rad));
      int y = cy + (int)(r * sin(rad));
      lcd.drawPixel(x, y, THEME_BG_DARK);
    }
  }
  
  // Arc spans from 210 to 330 degrees (bottom arc)
  float startAngle = 210;
  float endAngle = 330;
  float totalArc = endAngle - startAngle;
  
  // Background arc (dim)
  for (float a = startAngle; a <= endAngle; a += 2) {
    float rad = a * PI / 180.0;
    for (int r = innerR; r <= outerR; r++) {
      int x = cx + (int)(r * cos(rad));
      int y = cy + (int)(r * sin(rad));
      lcd.drawPixel(x, y, THEME_RING_1);
    }
  }
  
  // Speed segments (3 segments)
  uint16_t segColors[] = {RGB565(0, 220, 100), RGB565(255, 200, 0), RGB565(255, 60, 60)};
  float segWidth = totalArc / 3.0;
  
  for (int seg = 0; seg <= (int)currentSpeed; seg++) {
    float segStart = startAngle + seg * segWidth;
    float segEnd = segStart + segWidth - 3;  // Small gap between segments
    uint16_t segColor = (seg <= (int)currentSpeed) ? segColors[seg] : THEME_RING_1;
    
    for (float a = segStart; a <= segEnd; a += 1.5) {
      float rad = a * PI / 180.0;
      for (int r = innerR + 2; r <= outerR - 2; r++) {
        int x = cx + (int)(r * cos(rad));
        int y = cy + (int)(r * sin(rad));
        lcd.drawPixel(x, y, segColor);
      }
    }
  }
  
  // Speed label at bottom center
  const char* speedLabels[] = {"SLOW", "MED", "FAST"};
  uint16_t speedColors[] = {RGB565(0, 220, 100), RGB565(255, 200, 0), RGB565(255, 60, 60)};
  
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(speedColors[(int)currentSpeed], THEME_BG_DARK);
  lcd.drawString(speedLabels[(int)currentSpeed], 120, 205);
}

// Draw button press feedback indicator
void drawPressIndicator(int pressCount) {
  if (pressCount <= 0) return;
  
  int cx = 120, cy = 220;
  uint16_t accentColor = getMachineAccentColor();
  
  // Draw dots for press count
  for (int i = 0; i < min(pressCount, 3); i++) {
    int dotX = cx - 15 + i * 15;
    lcd.fillCircle(dotX, cy, 5, accentColor);
    lcd.drawCircle(dotX, cy, 6, RGB565(255, 255, 255));
  }
  
  // Label
  lcd.setFont(&fonts::Font0);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(THEME_TEXT_SUB, THEME_BG_DARK);
  
  if (pressCount == 1) lcd.drawString("AXIS", 120, 230);
  else if (pressCount == 2) lcd.drawString("DIST", 120, 230);
  else if (pressCount >= 3) lcd.drawString("SPEED", 120, 230);
}

// Clear press indicator area
void clearPressIndicator() {
  lcd.fillRect(80, 212, 80, 26, THEME_BG_DARK);
}

// Encoder interrupt service routine
void IRAM_ATTR encoderISR() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = micros();  // Use micros() for ISR - more reliable
  
  // Debounce: ignore if less than 5000 microseconds (5ms) since last interrupt
  if (interruptTime - lastInterruptTime > 5000) {
    if (digitalRead(PIN_ENCODER_B) == HIGH) {
      encoderPosition++;
    } else {
      encoderPosition--;
    }
    encoderMoved = true;
    lastInterruptTime = interruptTime;
  }
}

// Get machine-specific accent color
uint16_t getMachineAccentColor() {
  switch (currentMachine) {
    case MACHINE_FIRECONTROL: return THEME_PLASMA_ORANGE;
    case MACHINE_CUTCONTROL:  return THEME_MILL_GREEN;
    case MACHINE_MACH3:       return THEME_MACH_BLUE;
    case MACHINE_MACH4:       return THEME_MACH_BLUE;
    case MACHINE_LINUXCNC:    return THEME_LINUX_RED;
    case MACHINE_UCCNC:       return THEME_UCCNC_CYAN;
    case MACHINE_CARBIDE:     return THEME_CARBIDE_ORANGE;
    case MACHINE_UGS:         return THEME_UGS_TEAL;
    case MACHINE_OPENBUILDS:  return THEME_OPENBUILDS_GREEN;
    case MACHINE_CNCJS:       return THEME_CNCJS_PURPLE;
    case MACHINE_MANUAL:      return THEME_MANUAL_GRAY;
    default: return THEME_MANUAL_GRAY;
  }
}

// Get machine name for serial protocol
const char* getMachineName() {
  switch (currentMachine) {
    case MACHINE_FIRECONTROL: return "FIRECONTROL";
    case MACHINE_CUTCONTROL:  return "CUTCONTROL";
    case MACHINE_MACH3:       return "MACH3";
    case MACHINE_MACH4:       return "MACH4";
    case MACHINE_LINUXCNC:    return "LINUXCNC";
    case MACHINE_UCCNC:       return "UCCNC";
    case MACHINE_CARBIDE:     return "CARBIDE";
    case MACHINE_UGS:         return "UGS";
    case MACHINE_OPENBUILDS:  return "OPENBUILDS";
    case MACHINE_CNCJS:       return "CNCJS";
    case MACHINE_MANUAL:      return "MANUAL";
    default: return "UNKNOWN";
  }
}

// Get machine display name for UI (short name for LCD)
const char* getMachineDisplayName() {
  switch (currentMachine) {
    case MACHINE_FIRECONTROL: return "PLASMA";
    case MACHINE_CUTCONTROL:  return "MILL";
    case MACHINE_MACH3:       return "MACH3";
    case MACHINE_MACH4:       return "MACH4";
    case MACHINE_LINUXCNC:    return "LINUX";
    case MACHINE_UCCNC:       return "UCCNC";
    case MACHINE_CARBIDE:     return "CARBIDE";
    case MACHINE_UGS:         return "UGS";
    case MACHINE_OPENBUILDS:  return "OPENBLD";
    case MACHINE_CNCJS:       return "CNCJS";
    case MACHINE_MANUAL:      return "MANUAL";
    default: return "CNC";
  }
}

#ifdef BATTERY_MODE
// Battery Management Functions

// Update activity timestamp for power management
void updateActivity() {
  lastActivityTime = millis();
  if (currentPowerState != POWER_ACTIVE) {
    // Wake up from low power mode
    currentPowerState = POWER_ACTIVE;
    lcd.setBrightness(NORMAL_BRIGHTNESS);
    drawSolidBackground();
    drawAxisDisplay();
    drawSpeedIndicator();
    drawConnectionStatus();
  }
}

// Check battery level (simulated - replace with actual ADC reading)
void updateBatteryLevel() {
  // Simulate battery drain - replace with actual voltage measurement
  static unsigned long lastBatteryUpdate = 0;
  unsigned long now = millis();
  
  if (now - lastBatteryUpdate > 60000) { // Update every minute
    batteryLevel = max(0, batteryLevel - 1); // Drain 1% per minute (adjust for real usage)
    lastBatteryUpdate = now;
    
    if (batteryLevel < 20) {
      batteryLowPowerMode = true;
    }
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
  Serial.println("POWER:DEEP_SLEEP");
  
  // Configure wake-up sources
  // Encoder A is INPUT_PULLUP, so wake on LOW (0) when encoder moves
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_32, 0);
  // Button is INPUT_PULLUP, wakes when pressed (goes LOW)
  esp_sleep_enable_ext1_wakeup(1ULL << PIN_MODE_BTN, ESP_EXT1_WAKEUP_ALL_LOW);
  
  // Turn off display
  lcd.setBrightness(0);
  lcd.fillScreen(RGB565(0, 0, 0));
  
  // Deep sleep
  esp_deep_sleep_start();
}

// Manage power states based on activity
void managePowerState() {
  unsigned long now = millis();
  unsigned long inactiveTime = now - lastActivityTime;
  
  switch (currentPowerState) {
    case POWER_ACTIVE:
      if (inactiveTime > INACTIVITY_TIMEOUT) {
        currentPowerState = POWER_DIM;
        lcd.setBrightness(BATTERY_BRIGHTNESS);
        Serial.println("POWER:DIM");
      }
      break;
      
    case POWER_DIM:
      if (inactiveTime > SLEEP_TIMEOUT) {
        currentPowerState = POWER_SLEEP;
        lcd.setBrightness(BATTERY_BRIGHTNESS / 2);
        Serial.println("POWER:SLEEP");
      }
      break;
      
    case POWER_SLEEP:
      if (inactiveTime > DEEP_SLEEP_TIMEOUT) {
        enterDeepSleep();
      }
      break;
      
    case POWER_DEEP_SLEEP:
      // Should not reach here - device would be sleeping
      break;
  }
  
  // Send periodic battery status
  if (now - lastHeartbeat > HEARTBEAT_INTERVAL) {
    Serial.print("BATTERY:");
    Serial.print(batteryLevel);
    Serial.print("%,POWER:");
    Serial.println(currentPowerState == POWER_ACTIVE ? "ACTIVE" : 
                   currentPowerState == POWER_DIM ? "DIM" : "SLEEP");
    lastHeartbeat = now;
  }
}

// Draw battery indicator
void drawBatteryIndicator() {
  int x = 200, y = 10;
  int w = 30, h = 12;
  
  // Battery outline
  lcd.drawRect(x, y, w-4, h, RGB565(255, 255, 255));
  lcd.fillRect(x+w-4, y+3, 4, h-6, RGB565(255, 255, 255)); // Terminal
  
  // Battery fill based on level
  int fillWidth = (w-6) * batteryLevel / 100;
  uint16_t fillColor = batteryLevel > 20 ? RGB565(0, 255, 0) : RGB565(255, 0, 0);
  if (fillWidth > 0) {
    lcd.fillRect(x+2, y+2, fillWidth, h-4, fillColor);
  }
  
  // Battery percentage text
  lcd.setTextColor(RGB565(255, 255, 255));
  lcd.setTextSize(1);
  lcd.setCursor(x-20, y+2);
  lcd.printf("%d%%", batteryLevel);
}
#endif

#ifdef BLUETOOTH_MODE
// Initialize Bluetooth HID
void initBluetooth() {
  Serial.println("BLE: Initializing...");
  
  BLEDevice::init(deviceName);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new BLEConnectionCallbacks());
  
  hid = new BLEHIDDevice(server);
  input = hid->inputReport(1); // Report ID 1
  output = hid->outputReport(1);
  feature = hid->featureReport(1);
  
  // Set HID descriptor for keyboard
  const uint8_t hidReportDescriptor[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  // Report ID (1)
    0x05, 0x07,  // Usage Page (Key Codes)
    0x19, 0xE0,  // Usage Minimum (224)
    0x29, 0xE7,  // Usage Maximum (231)
    0x15, 0x00,  // Logical Minimum (0)
    0x25, 0x01,  // Logical Maximum (1)
    0x75, 0x01,  // Report Size (1)
    0x95, 0x08,  // Report Count (8)
    0x81, 0x02,  // Input (Data, Variable, Absolute)
    0x95, 0x01,  // Report Count (1)
    0x75, 0x08,  // Report Size (8)
    0x81, 0x01,  // Input (Constant)
    0x95, 0x06,  // Report Count (6)
    0x75, 0x08,  // Report Size (8)
    0x15, 0x00,  // Logical Minimum (0)
    0x25, 0x65,  // Logical Maximum (101)
    0x05, 0x07,  // Usage Page (Key Codes)
    0x19, 0x00,  // Usage Minimum (0)
    0x29, 0x65,  // Usage Maximum (101)
    0x81, 0x00,  // Input (Data, Array)
    0xC0         // End Collection
  };
  
  hid->reportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));
  hid->startServices();
  
  BLEAdvertising* advertising = server->getAdvertising();
  advertising->setAppearance(HID_KEYBOARD);
  advertising->addServiceUUID(hid->hidService()->getUUID());
  advertising->start();
  
  bleStarted = true;
  Serial.println("BLE: Advertising started");
}

// Send key press via Bluetooth
void sendBluetoothKey(uint8_t key, bool press) {
  if (!bleConnected || !bleStarted) return;
  
  uint8_t report[8] = {0}; // Modifier, Reserved, Key1, Key2, Key3, Key4, Key5, Key6
  
  if (press && key != 0) {
    // Handle modifier keys (Alt+R, Alt+S)
    if (key == 0x15) { // Alt+R
      report[0] = 0x04; // Left Alt
      report[2] = 0x15; // R key
    } else if (key == 0x16) { // Alt+S  
      report[0] = 0x04; // Left Alt
      report[2] = 0x16; // S key
    } else {
      report[2] = key; // Regular key
    }
  }
  
  input->setValue(report, 8);
  input->notify();
  
  // Small delay for key recognition
  delay(20);
}

// Send keyboard command via Bluetooth instead of Serial
void sendBluetoothCommand(const char* command) {
  if (!bleConnected) {
    // Fallback to serial if not connected
    Serial.println(command);
    return;
  }
  
  String cmd = String(command);
  
  // Map KEY commands to HID codes
  if (cmd.startsWith("KEY:")) {
    String keyName = cmd.substring(4);
    keyName = keyName.substring(0, keyName.indexOf(','));
    bool press = cmd.endsWith(",1");
    
    uint8_t hidKey = 0;
    if (keyName == "LEFT") hidKey = HID_LEFT;
    else if (keyName == "RIGHT") hidKey = HID_RIGHT;
    else if (keyName == "UP") hidKey = HID_UP;
    else if (keyName == "DOWN") hidKey = HID_DOWN;
    else if (keyName == "PGUP") hidKey = HID_PGUP;
    else if (keyName == "PGDN") hidKey = HID_PGDN;
    else if (keyName == "TAB") hidKey = HID_TAB;
    else if (keyName == "SPACE") hidKey = HID_SPACE;
    else if (keyName == "F1") hidKey = HID_F1;
    else if (keyName == "F2") hidKey = HID_F2;
    else if (keyName == "F3") hidKey = HID_F3;
    else if (keyName == "F4") hidKey = HID_F4;
    else if (keyName == "1") hidKey = HID_1;
    else if (keyName == "2") hidKey = HID_2;
    else if (keyName == "3") hidKey = HID_3;
    else if (keyName == "4") hidKey = HID_4;
    else if (keyName == "ALT_R") hidKey = 0x15; // Special Alt+R
    else if (keyName == "ALT_S") hidKey = 0x16; // Special Alt+S
    
    if (hidKey != 0) {
      sendBluetoothKey(hidKey, press);
    }
  }
}

// Check Bluetooth connection and manage reconnection
void manageBluetooth() {
  if (!bleStarted) return;
  
  unsigned long now = millis();
  
  if (!bleConnected && (now - lastConnectionAttempt > BLE_RECONNECT_INTERVAL)) {
    // Attempt to restart advertising if disconnected
    BLEDevice::getServer()->startAdvertising();
    lastConnectionAttempt = now;
    Serial.println("BLE: Restarting advertising");
  }
}

// Draw Bluetooth status indicator
void drawBluetoothStatus() {
  int x = 10, y = 10;
  uint16_t color = bleConnected ? RGB565(0, 150, 255) : RGB565(100, 100, 100);
  
  // Bluetooth icon (simplified)
  lcd.drawLine(x+5, y, x+5, y+10, color);
  lcd.drawLine(x+2, y+3, x+8, y+7, color);
  lcd.drawLine(x+8, y+3, x+2, y+7, color);
  lcd.drawPixel(x+1, y+2, color);
  lcd.drawPixel(x+1, y+8, color);
  lcd.drawPixel(x+9, y+2, color);
  lcd.drawPixel(x+9, y+8, color);
  
  // Connection status text
  lcd.setTextColor(color);
  lcd.setTextSize(1);
  lcd.setCursor(x+15, y+2);
  lcd.print(bleConnected ? "BLE" : "---");
}
#endif

// Universal key sending function (Bluetooth or Serial)
void sendKeyCommand(const char* command) {
#ifdef BLUETOOTH_MODE
  sendBluetoothCommand(command);
#else
  Serial.println(command);
#endif
}

// Get distance text with appropriate units and machine compatibility
String getDistanceText() {
  if (currentMachine == MACHINE_FIRECONTROL) {
    // FireControl uses step sizes, not exact distances
    if (useImperialUnits) {
      switch (currentJogDistance) {
        case JOG_FINE:   return "1/16\"";
        case JOG_SMALL:  return "1/8\""; 
        case JOG_MEDIUM: return "1/4\"";
        case JOG_LARGE:  return "1\"";
        default: return "1/8\"";
      }
    } else {
      switch (currentJogDistance) {
        case JOG_FINE:   return "FINE";
        case JOG_SMALL:  return "SMALL";
        case JOG_MEDIUM: return "MEDIUM"; 
        case JOG_LARGE:  return "LARGE";
        default: return "SMALL";
      }
    }
  } else {
    // CutControl and others use exact distances
    if (useImperialUnits) {
      switch (currentJogDistance) {
        case JOG_FINE:   return "0.001\"";
        case JOG_SMALL:  return "0.010\"";
        case JOG_MEDIUM: return "0.100\"";
        case JOG_LARGE:  return "1.000\"";
        default: return "0.010\"";
      }
    } else {
      switch (currentJogDistance) {
        case JOG_FINE:   return "0.1mm";
        case JOG_SMALL:  return "1mm";
        case JOG_MEDIUM: return "10mm";
        case JOG_LARGE:  return "100mm";
        default: return "1mm";
      }
    }
  }
}

// HSV to RGB conversion
void hsv_to_rgb(float h, float s, float v, uint8_t &r, uint8_t &g, uint8_t &b) {
  float c = v * s;
  float x = c * (1 - abs(fmod(h / 60.0, 2) - 1));
  float m = v - c;
  float r1, g1, b1;
  
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
  
  r = (uint8_t)((r1 + m) * 255);
  g = (uint8_t)((g1 + m) * 255);
  b = (uint8_t)((b1 + m) * 255);
}

// Convert RGB888 to RGB565
uint16_t rgb888_to_565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Draw RGB wheel background
void drawRGBWheel() {
  const int cx = 120, cy = 120, R = 118;

  for (int y = 0; y < 240; y++) {
    int dy = y - cy;
    int dx_start = -1, dx_end = -1;
    
    for (int x = 0; x < 240; x++) {
      int dx = x - cx;
      float r = sqrtf(dx * dx + dy * dy);

      if (r <= R) {
        if (dx_start == -1) dx_start = x;
        dx_end = x;
      } else if (dx_start != -1) {
        break;
      }
    }
    
    if (dx_start != -1) {
      for (int x = dx_start; x <= dx_end; x++) {
        int dx = x - cx;
        float r = sqrtf(dx * dx + dy * dy);
        
        float angle = atan2f((float)dy, (float)dx) * 180.0f / PI;
        if (angle < 0) angle += 360.0f;

        float hue = fmodf(angle + rgbHue, 360.0f);
        float sat = min(1.0f, r / (float)R);
        float val = 0.2f + 0.8f * (r / (float)R);

        uint8_t R8, G8, B8;
        hsv_to_rgb(hue, sat, val, R8, G8, B8);
        
        R8 = min(255, (int)(R8 * 1.2f));
        G8 = min(255, (int)(G8 * 1.2f));
        B8 = min(255, (int)(B8 * 1.2f));
        
        lcd.writePixel(x, y, rgb888_to_565(R8, G8, B8));
      }
    }
  }
}

// Draw solid color background
void drawSolidBackground() {
  uint16_t color = useRGBWheel ? THEME_BG_DARK : rgb888_to_565(bgR, bgG, bgB);
  if (bgR == 0 && bgG == 0 && bgB == 0) {
    color = THEME_BG_DARK;
  }
  lcd.fillScreen(color);
}

// Draw current axis display with machine-specific styling
void drawAxisDisplay() {
  uint16_t accentColor = getMachineAccentColor();
  uint16_t bgColor = THEME_BG_DARK;
  
  // Clear center area
  lcd.fillCircle(120, 120, 95, THEME_BG_DARK);
  
  // Glassy central card with machine-specific tint
  drawGlassCirclePanel(120, 120, 50, blend565(accentColor, THEME_CARD, 100), THEME_BG_DARK);
  
  // Draw themed outer rings
  lcd.drawCircle(120, 120, 118, THEME_RING_1);
  lcd.drawCircle(120, 120, 117, THEME_RING_2);
  lcd.drawCircle(120, 120, 116, THEME_RING_3);
  
  // Draw directional arrows around center
  drawDirectionalArrows(lastJogDirection);

  // Axis text and colors - using distinct bright colors
  const char* axisText;
  uint16_t axisColor;

  switch (currentAxis) {
    case AXIS_Z:
      axisText = "Z";
      axisColor = RGB565(80, 140, 255); // Bright Blue
      break;
    case AXIS_Y:
      axisText = "Y"; 
      axisColor = RGB565(0, 255, 120); // Bright Green
      break;
    case AXIS_X:
      axisText = "X";
      axisColor = RGB565(255, 80, 80); // Bright Red
      break;
  }

  // Draw large axis letter with glow effect
  lcd.setTextDatum(middle_center);
  lcd.setFont(&fonts::Font7);  // Large font for axis
  
  // Glow effect
  for (int dx = -2; dx <= 2; dx++) {
    for (int dy = -2; dy <= 2; dy++) {
      if (dx != 0 || dy != 0) {
        lcd.setTextColor(blend565(axisColor, THEME_BG_DARK, 80), bgColor);
        lcd.drawString(axisText, 120 + dx, 105 + dy);
      }
    }
  }
  lcd.setTextColor(axisColor, bgColor);
  lcd.drawString(axisText, 120, 105);
  
  // Axis label below letter
  lcd.setFont(&fonts::Font0);
  lcd.setTextColor(THEME_TEXT_SUB, bgColor);
  lcd.drawString("AXIS", 120, 135);
  
  // Draw distance in styled card (left side)
  String distanceText = getDistanceText();
  int distX = 35, distY = 170;
  
  // Distance card background
  drawGlassPill(distX - 30, distY - 12, 60, 24, THEME_CARD, THEME_BG_DARK, RGB565(255, 200, 0));
  
  // Distance icon (ruler)
  lcd.fillRect(distX - 22, distY - 4, 12, 2, RGB565(255, 200, 0));
  lcd.fillRect(distX - 22, distY - 2, 2, 6, RGB565(255, 200, 0));
  lcd.fillRect(distX - 16, distY - 2, 2, 4, RGB565(255, 200, 0));
  lcd.fillRect(distX - 10, distY - 2, 2, 6, RGB565(255, 200, 0));
  
  lcd.setFont(&fonts::Font0);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(RGB565(255, 220, 100), THEME_BG_DARK);
  lcd.drawString(distanceText.c_str(), distX + 8, distY);
  
  // Draw feed rate in styled card (right side) - CutControl only
  if (currentMachine == MACHINE_CUTCONTROL) {
    const char* feedText = "";
    switch (currentFeedRate) {
      case FEED_25: feedText = "25%"; break;
      case FEED_50: feedText = "50%"; break;
      case FEED_75: feedText = "75%"; break;
      case FEED_100: feedText = "100%"; break;
    }
    
    int feedX = 205, feedY = 170;
    
    // Feed card background
    drawGlassPill(feedX - 30, feedY - 12, 60, 24, THEME_CARD, THEME_BG_DARK, RGB565(0, 200, 255));
    
    // Feed icon (gauge)
    lcd.drawArc(feedX - 18, feedY, 6, 4, 180, 360, RGB565(0, 200, 255));
    lcd.fillCircle(feedX - 18, feedY, 2, RGB565(0, 200, 255));
    
    lcd.setTextColor(RGB565(100, 220, 255), THEME_BG_DARK);
    lcd.drawString(feedText, feedX + 5, feedY);
  }

  // Draw machine type badge at very bottom with icon
  String machineText = getMachineDisplayName();
  uint16_t machineColor = getMachineAccentColor();
  
  // Larger, more prominent machine badge
  drawGlassPill(60, 185, 120, 22, blend565(machineColor, THEME_CARD, 120), THEME_BG_DARK, machineColor);
  
  // Machine icon based on type
  int iconX = 75, iconY = 196;
  lcd.setTextColor(machineColor, THEME_BG_DARK);
  
  switch (currentMachine) {
    case MACHINE_FIRECONTROL:
      // Flame icon
      lcd.fillTriangle(iconX, iconY+4, iconX-4, iconY-4, iconX+4, iconY-4, machineColor);
      lcd.fillCircle(iconX, iconY-2, 3, machineColor);
      break;
    case MACHINE_CUTCONTROL:
      // Mill icon (gear)
      lcd.drawCircle(iconX, iconY, 5, machineColor);
      lcd.fillCircle(iconX, iconY, 2, machineColor);
      break;
    case MACHINE_MACH3:
    case MACHINE_MACH4:
      // M icon for Mach
      lcd.drawLine(iconX-4, iconY+4, iconX-4, iconY-4, machineColor);
      lcd.drawLine(iconX-4, iconY-4, iconX, iconY, machineColor);
      lcd.drawLine(iconX, iconY, iconX+4, iconY-4, machineColor);
      lcd.drawLine(iconX+4, iconY-4, iconX+4, iconY+4, machineColor);
      break;
    case MACHINE_LINUXCNC:
      // Penguin/triangle for Linux
      lcd.fillTriangle(iconX, iconY-5, iconX-4, iconY+4, iconX+4, iconY+4, machineColor);
      break;
    case MACHINE_UCCNC:
      // U icon
      lcd.drawLine(iconX-3, iconY-4, iconX-3, iconY+2, machineColor);
      lcd.drawLine(iconX-3, iconY+2, iconX+3, iconY+2, machineColor);
      lcd.drawLine(iconX+3, iconY+2, iconX+3, iconY-4, machineColor);
      break;
    case MACHINE_CARBIDE:
      // Diamond icon
      lcd.drawLine(iconX, iconY-5, iconX+5, iconY, machineColor);
      lcd.drawLine(iconX+5, iconY, iconX, iconY+5, machineColor);
      lcd.drawLine(iconX, iconY+5, iconX-5, iconY, machineColor);
      lcd.drawLine(iconX-5, iconY, iconX, iconY-5, machineColor);
      break;
    case MACHINE_UGS:
    case MACHINE_OPENBUILDS:
    case MACHINE_CNCJS:
      // Generic CNC icon (crosshairs)
      lcd.drawLine(iconX-5, iconY, iconX+5, iconY, machineColor);
      lcd.drawLine(iconX, iconY-5, iconX, iconY+5, machineColor);
      lcd.drawCircle(iconX, iconY, 3, machineColor);
      break;
    default:
      // Manual - wrench
      lcd.drawLine(iconX-4, iconY-4, iconX+4, iconY+4, machineColor);
      lcd.fillCircle(iconX-4, iconY-4, 2, machineColor);
      break;
  }
  
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(machineColor, THEME_BG_DARK);
  lcd.drawString(machineText.c_str(), 130, 196);
}

// Draw speed indicator - now uses arc display
void drawSpeedIndicator() {
  drawSpeedArc();
}

// Draw connection status with machine indication - Enhanced version
void drawConnectionStatus() {
  static unsigned long lastPulse = 0;
  static bool pulseState = false;
  static int pulseSize = 0;
  uint16_t accentColor = getMachineAccentColor();
  
  unsigned long now = millis();
  if (now - lastPulse > 500) {
    pulseState = !pulseState;
    pulseSize = pulseState ? 1 : 0;
    lastPulse = now;
  }
  
  // Clear status area
  lcd.fillRect(165, 2, 73, 36, THEME_BG_DARK);
  lcd.fillRect(2, 2, 55, 36, THEME_BG_DARK);
  
  uint16_t statusColor;
  const char* statusText;
  bool isConnected = false;
  
  if (Serial && softwareConnected) {
    statusColor = accentColor;
    statusText = "LIVE";
    isConnected = true;
  } else if (Serial) {
    statusColor = RGB565(255, 200, 0);
    statusText = "USB";
  } else {
    statusColor = RGB565(255, 60, 60);
    statusText = "OFF";
  }
  
  // Connection status card (top right)
  drawGlassPill(168, 5, 68, 30, THEME_CARD, THEME_BG_DARK, statusColor);
  
  // Animated status indicator
  int dotX = 225, dotY = 20;
  
  // Pulsing ring effect for connected state
  if (isConnected && pulseState) {
    lcd.drawCircle(dotX, dotY, 10 + pulseSize, blend565(statusColor, THEME_BG_DARK, 100));
    lcd.drawCircle(dotX, dotY, 11 + pulseSize, blend565(statusColor, THEME_BG_DARK, 60));
  }
  
  // Main status dot with gradient effect
  lcd.fillCircle(dotX, dotY, 8, statusColor);
  lcd.fillCircle(dotX - 2, dotY - 2, 3, blend565(RGB565(255,255,255), statusColor, 150));
  lcd.drawCircle(dotX, dotY, 9, THEME_RING_3);
  
  // USB icon if USB connected
  if (Serial) {
    int iconX = 175, iconY = 20;
    lcd.fillRoundRect(iconX, iconY - 4, 8, 8, 1, THEME_TEXT_SUB);
    lcd.fillRect(iconX + 8, iconY - 2, 3, 4, THEME_TEXT_SUB);
    lcd.fillRect(iconX + 2, iconY - 6, 4, 2, THEME_TEXT_SUB);
  }
  
  // Status text
  lcd.setTextColor(statusColor, THEME_BG_DARK);
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(middle_center);
  lcd.drawString(statusText, 198, 20);
  
  // Units indicator (top left) - Enhanced with icon
  String unitsText = useImperialUnits ? "IN" : "MM";
  uint16_t unitsColor = useImperialUnits ? RGB565(255, 100, 180) : RGB565(100, 200, 255);
  
  // Units card
  drawGlassPill(5, 5, 50, 30, THEME_CARD, THEME_BG_DARK, unitsColor);
  
  // Ruler icon
  int rulerX = 15, rulerY = 20;
  lcd.fillRect(rulerX, rulerY - 6, 2, 12, unitsColor);
  lcd.fillRect(rulerX, rulerY - 6, 8, 2, unitsColor);
  lcd.fillRect(rulerX + 4, rulerY - 4, 2, 4, unitsColor);
  lcd.fillRect(rulerX, rulerY + 4, 8, 2, unitsColor);
  
  // Units text
  lcd.setTextColor(unitsColor, THEME_BG_DARK);
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(middle_center);
  lcd.drawString(unitsText.c_str(), 40, 20);
}

// Draw enhanced boot screen with machine detection
void drawBootScreen() {
  lcd.fillScreen(0x0000);

  // Draw concentric rings
  for (int r = 115; r <= 118; r++) {
    uint16_t ringColor = (r == 118) ? getMachineAccentColor() : 
                        (r == 117) ? blend565(getMachineAccentColor(), THEME_BG_DARK, 180) : 
                        blend565(getMachineAccentColor(), THEME_BG_DARK, 120);
    lcd.drawCircle(120, 120, r, ringColor);
  }

  // Title
  lcd.setTextColor(getMachineAccentColor(), 0x0000);
  lcd.setTextDatum(middle_center);
  lcd.setFont(&fonts::Font8);
  lcd.drawString("UNIVERSAL", 120, 75);

  lcd.setTextColor(THEME_TEXT_MAIN, 0x0000);
  lcd.setFont(&fonts::Font4);
  lcd.drawString("PENDANT", 120, 100);
  
  lcd.setTextColor(getMachineAccentColor(), 0x0000);
  lcd.setFont(&fonts::Font2);
  lcd.drawString("Langmuir Systems", 120, 120);

  // Machine type indicator
  lcd.setTextColor(THEME_TEXT_SUB, 0x0000);
  lcd.setFont(&fonts::Font2);
  lcd.drawString(getMachineName(), 120, 140);

  // Controls hint
  lcd.setFont(&fonts::Font0);
  
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      if (dx != 0 || dy != 0) {
        lcd.setTextColor(0x0000, 0x0000);
        lcd.drawString("Rotate: Jog | Press: Mode", 120 + dx, 165 + dy);
        lcd.drawString("Double: Distance | Triple: Speed", 120 + dx, 175 + dy);
        lcd.drawString("Hold: Machine Select", 120 + dx, 185 + dy);
#ifdef BATTERY_MODE
        lcd.drawString("BATTERY MODE ENABLED", 120 + dx, 195 + dy);
#endif
      }
    }
  }
  
  lcd.setTextColor(0xFFE0, 0x0000);
  lcd.drawString("Rotate: Jog | Press: Mode", 120, 165);
  lcd.drawString("Double: Distance | Triple: Speed", 120, 175);
  lcd.drawString("Hold: Machine Select", 120, 185);
  
#ifdef BATTERY_MODE
  lcd.setTextColor(RGB565(0, 255, 0), 0x0000);
  lcd.drawString("BATTERY MODE ENABLED", 120, 195);
#endif

  // Loading animation with machine colors
  uint16_t dotColors[] = {
    THEME_PLASMA_ORANGE, THEME_MILL_GREEN, THEME_MACH_BLUE, THEME_LINUX_RED
  };
  for (int i = 0; i < 4; i++) {
    lcd.fillCircle(90 + i * 20, 205, 4, dotColors[i]);
    lcd.drawCircle(90 + i * 20, 205, 5, 0xFFFF);
    delay(200);
  }
  
  // Final pulse
  for (int r = 0; r < 5; r++) {
    lcd.drawCircle(120, 120, 118 + r, getMachineAccentColor());
    delay(50);
  }
}

// Handle rotary encoder movement and send keyboard commands
void handleEncoder() {
  if (!encoderMoved) return;
  
#ifdef BATTERY_MODE
  updateActivity(); // Track activity for power management
#endif
  
  long currentPos = encoderPosition;
  int movement = currentPos - lastEncoderPos;
  
  if (movement != 0) {
    lastEncoderPos = currentPos;
    encoderMoved = false;
    
    bool positive = movement > 0;
    
    // Update jog direction for visual feedback
    lastJogDirection = positive ? 1 : -1;
    
    // Minimal key hold time - Windows app handles the actual timing
    // 8ms is enough for USB serial to transmit and Windows to register
    const int KEY_HOLD_MS = 8;
    
    switch (currentAxis) {
      case AXIS_Z:
        sendKeyCommand(positive ? "KEY:PGUP,1" : "KEY:PGDN,1");
        delay(KEY_HOLD_MS);
        sendKeyCommand(positive ? "KEY:PGUP,0" : "KEY:PGDN,0");
        break;
        
      case AXIS_Y:
        sendKeyCommand(positive ? "KEY:UP,1" : "KEY:DOWN,1");
        delay(KEY_HOLD_MS);
        sendKeyCommand(positive ? "KEY:UP,0" : "KEY:DOWN,0");
        break;
        
      case AXIS_X:
        sendKeyCommand(positive ? "KEY:RIGHT,1" : "KEY:LEFT,1");
        delay(KEY_HOLD_MS);
        sendKeyCommand(positive ? "KEY:RIGHT,0" : "KEY:LEFT,0");
        break;
    }
    
    drawAxisDisplay();
    drawSpeedIndicator();
    
    // Brief visual feedback for direction arrow (non-blocking would be better but this is minimal)
    lastJogDirection = 0;
    drawDirectionalArrows(0);
  }
}

// Handle mode button with machine selection
void handleModeButton() {
  bool buttonState = !digitalRead(PIN_MODE_BTN);
  static bool lastButtonState = false;
  static unsigned long debounceTime = 0;
  unsigned long now = millis();
  
  if (buttonState != lastButtonState) {
    debounceTime = now;
#ifdef BATTERY_MODE
    if (buttonState) updateActivity(); // Track activity for power management
#endif
  }
  
  if ((now - debounceTime) > DEBOUNCE_DELAY) {
    if (buttonState && !modeButtonPressed) {
      modeButtonPressed = true;
      modeButtonProcessed = false;
      btnPressTime = now;
    } else if (!buttonState && modeButtonPressed) {
      modeButtonPressed = false;
      unsigned long pressDuration = now - btnPressTime;
      
      if (!modeButtonProcessed) {
        if (pressDuration >= LONG_PRESS_THRESHOLD) {
          // Long press - cycle machine type
          currentMachine = (MachineType)((currentMachine + 1) % MACHINE_TYPE_COUNT);
          Serial.print("MACHINE:");
          Serial.println(getMachineName());
          drawBootScreen();
          delay(1000);
          drawSolidBackground();
          drawAxisDisplay();
          drawSpeedIndicator();
          drawConnectionStatus();
          modeButtonProcessed = true;
        } else if (pressDuration < MULTI_PRESS_WINDOW) {
          btnPressCount++;
          lastMultiPressTime = now;
        }
        // Note: Presses between 500ms and 2000ms are handled by multi-press timeout
        // This prevents duplicate axis changes
      }
    }
    
    // Handle multi-press timeout
    if (btnPressCount > 0 && (now - lastMultiPressTime) > MULTI_PRESS_WINDOW) {
      // Clear press indicator before action
      clearPressIndicator();
      
      if (btnPressCount == 1) {
        // Single press - change axis
        currentAxis = (AxisType)((currentAxis + 1) % 3);
        Serial.print("AXIS:");
        Serial.println(currentAxis == AXIS_Z ? "Z" : currentAxis == AXIS_Y ? "Y" : "X");
        lastJogDirection = 0;  // Reset direction
        drawAxisDisplay();
        drawSpeedIndicator();
      } else if (btnPressCount == 2) {
        // Double press - change jog distance
        currentJogDistance = (JogDistance)((currentJogDistance + 1) % 4);
        if (currentMachine == MACHINE_CUTCONTROL) {
          // Send distance key for CutControl (press and release) - 8ms is enough
          switch(currentJogDistance) {
            case JOG_FINE:   sendKeyCommand("KEY:1,1"); delay(8); sendKeyCommand("KEY:1,0"); break;
            case JOG_SMALL:  sendKeyCommand("KEY:2,1"); delay(8); sendKeyCommand("KEY:2,0"); break;
            case JOG_MEDIUM: sendKeyCommand("KEY:3,1"); delay(8); sendKeyCommand("KEY:3,0"); break;
            case JOG_LARGE:  sendKeyCommand("KEY:4,1"); delay(8); sendKeyCommand("KEY:4,0"); break;
          }
        }
        drawAxisDisplay();
      } else if (btnPressCount >= 3) {
        // Triple press - change feed rate (CutControl only) or speed
        if (currentMachine == MACHINE_CUTCONTROL) {
          currentFeedRate = (FeedRate)((currentFeedRate + 1) % 4);
          // Send feed rate key (press and release) - 8ms is enough
          switch(currentFeedRate) {
            case FEED_25:  sendKeyCommand("KEY:F1,1"); delay(8); sendKeyCommand("KEY:F1,0"); break;
            case FEED_50:  sendKeyCommand("KEY:F2,1"); delay(8); sendKeyCommand("KEY:F2,0"); break;
            case FEED_75:  sendKeyCommand("KEY:F3,1"); delay(8); sendKeyCommand("KEY:F3,0"); break;
            case FEED_100: sendKeyCommand("KEY:F4,1"); delay(8); sendKeyCommand("KEY:F4,0"); break;
          }
          drawAxisDisplay();
        } else {
          // Other machines - change speed
          currentSpeed = (SpeedType)((currentSpeed + 1) % 3);
          drawSpeedIndicator();
        }
      }
      btnPressCount = 0;
      showPressIndicator = false;
      modeButtonProcessed = true;
    }
    
    // Track last shown press count for update detection
    static int lastShownCount = 0;
    
    // Show press count indicator during multi-press window
    if (btnPressCount > 0 && !showPressIndicator) {
      showPressIndicator = true;
      drawPressIndicator(btnPressCount);
      lastShownCount = btnPressCount;
    } else if (btnPressCount > 0 && showPressIndicator) {
      // Update indicator if press count changed
      if (btnPressCount != lastShownCount) {
        clearPressIndicator();
        drawPressIndicator(btnPressCount);
        lastShownCount = btnPressCount;
      }
    } else if (btnPressCount == 0) {
      // Reset when no presses pending
      lastShownCount = 0;
    }
  }
  
  lastButtonState = buttonState;
}

// Process commands from Windows app with machine detection
void processSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command.startsWith("LCD:SOLID,")) {
      int firstComma = command.indexOf(',', 10);
      int secondComma = command.indexOf(',', firstComma + 1);
      
      if (firstComma > 0 && secondComma > 0) {
        bgR = command.substring(10, firstComma).toInt();
        bgG = command.substring(firstComma + 1, secondComma).toInt();
        bgB = command.substring(secondComma + 1).toInt();
        useRGBWheel = false;
        
        drawSolidBackground();
        drawAxisDisplay();
        drawSpeedIndicator();
        drawConnectionStatus();
      }
    }
    else if (command.startsWith("LCD:WHEEL,")) {
      rgbHue = command.substring(10).toFloat();
      useRGBWheel = true;
      
      drawRGBWheel();
      drawAxisDisplay();
      drawSpeedIndicator();
      drawConnectionStatus();
    }
    else if (command.startsWith("MACHINE:")) {
      String machineType = command.substring(8);
      MachineType newMachine = currentMachine;
      
      // Langmuir Systems
      if (machineType == "FIRECONTROL") newMachine = MACHINE_FIRECONTROL;
      else if (machineType == "CUTCONTROL") newMachine = MACHINE_CUTCONTROL;
      // Industrial CNC
      else if (machineType == "MACH3") newMachine = MACHINE_MACH3;
      else if (machineType == "MACH4") newMachine = MACHINE_MACH4;
      else if (machineType == "LINUXCNC") newMachine = MACHINE_LINUXCNC;
      else if (machineType == "UCCNC") newMachine = MACHINE_UCCNC;
      // Hobby/Prosumer
      else if (machineType == "CARBIDE") newMachine = MACHINE_CARBIDE;
      else if (machineType == "UGS") newMachine = MACHINE_UGS;
      else if (machineType == "OPENBUILDS") newMachine = MACHINE_OPENBUILDS;
      else if (machineType == "CNCJS") newMachine = MACHINE_CNCJS;
      // Generic
      else if (machineType == "MANUAL") newMachine = MACHINE_MANUAL;
      
      if (newMachine != currentMachine) {
        currentMachine = newMachine;
        drawSolidBackground();
        drawAxisDisplay();
        drawSpeedIndicator();
        drawConnectionStatus();
        
        Serial.print("MACHINE:");
        Serial.println(getMachineName());
      }
    }
    else if (command.startsWith("UNITS:")) {
      String units = command.substring(6);
      bool newUnits = (units == "INCHES" || units == "IN");
      
      if (newUnits != useImperialUnits) {
        useImperialUnits = newUnits;
        drawAxisDisplay();
        drawConnectionStatus();
        
        Serial.print("UNITS:");
        Serial.println(useImperialUnits ? "INCHES" : "MM");
      }
    }
    else if (command.startsWith("SOFTWARE:")) {
      bool newStatus = command.substring(9) == "CONNECTED";
      
      if (newStatus != softwareConnected) {
        softwareConnected = newStatus;
        drawConnectionStatus();
        
        Serial.print("SOFTWARE:");
        Serial.println(softwareConnected ? "CONNECTED" : "DISCONNECTED");
      }
    }
    else if (command.startsWith("LCD:BRIGHTNESS,")) {
      int brightness = command.substring(15).toInt();
      brightness = constrain(brightness, 0, 255);
      lcd.setBrightness(brightness);
      
      Serial.print("BRIGHTNESS:");
      Serial.println(brightness);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  
#ifdef BATTERY_MODE
  // Battery mode initialization
  ++bootCount;
  Serial.println("BATTERY_MODE: Enabled");
  Serial.printf("Boot count: %d\n", bootCount);
  
  // Check wake-up reason
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Wake: Encoder movement");
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("Wake: Button press");
      break;
    default:
      Serial.println("Wake: Power on");
      batteryLevel = 100; // Reset battery on fresh boot
      break;
  }
  
  lastActivityTime = millis();
  currentPowerState = POWER_ACTIVE;
#endif

#ifdef BLUETOOTH_MODE
  // Initialize Bluetooth
  initBluetooth();
#endif

  Serial.println("Universal Langmuir CNC Pendant starting...");

  pinMode(PIN_ENCODER_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_B, INPUT_PULLUP);
  pinMode(PIN_MODE_BTN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), encoderISR, RISING);

  delay(100);

  Serial.println("Initializing LCD...");
  lcd.init();
  lcd.setRotation(0);
  lcd.setColorDepth(16);
  
#ifdef BATTERY_MODE
  lcd.setBrightness(batteryLowPowerMode ? BATTERY_BRIGHTNESS : NORMAL_BRIGHTNESS);
#else
  lcd.setBrightness(200);
#endif

  drawBootScreen();
  delay(2000);
  
  drawSolidBackground();
  drawAxisDisplay();
  drawSpeedIndicator();
  drawConnectionStatus();

#ifdef BATTERY_MODE
  Serial.println("PENDANT:READY,BATTERY_MODE");
  Serial.printf("BATTERY:%d%%\n", batteryLevel);
#else
  Serial.println("PENDANT:READY");
#endif
  Serial.print("MACHINE:");
  Serial.println(getMachineName());
  Serial.println("Universal CNC Pendant - Supports ALL major CNC software");
  Serial.println("Langmuir | Mach3/4 | LinuxCNC | UCCNC | Carbide | UGS | OpenBuilds | CNCjs");
  Serial.print("Current: ");
  Serial.print(getMachineDisplayName());
  Serial.println(" mode");
}

void loop() {
  handleModeButton();
  handleEncoder();
  processSerialCommands();
  
#ifdef BLUETOOTH_MODE
  // Manage Bluetooth connection
  manageBluetooth();
#endif
  
#ifdef BATTERY_MODE
  // Battery power management
  managePowerState();
  updateBatteryLevel();
#endif
  
  // Periodic status update
  static unsigned long lastStatusUpdate = 0;
  if (millis() - lastStatusUpdate > 1000) {
    drawConnectionStatus();
#ifdef BATTERY_MODE
    drawBatteryIndicator();
#endif
    lastStatusUpdate = millis();
  }
  
#ifdef BATTERY_MODE
  // Longer delay in battery mode to save power
  delay(currentPowerState == POWER_ACTIVE ? 1 : 10);
#else
  // Minimal delay - just yield to prevent watchdog, encoder is interrupt-driven
  delay(1);
#endif
}