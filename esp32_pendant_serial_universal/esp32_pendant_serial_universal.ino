/*
  UNIVERSAL AIONMECH CNC PENDANT – ESP32 WROOM (LIVE POSITION READY, NO-FLICKER)
  Display: GC9A1 240x240, LovyanGFX
  Serial: 115200 (newline '\n')
  Wiring:
    Encoder A : GPIO32 (interrupt, rising)
    Encoder B : GPIO33
    Button 1  : GPIO25 (to GND, INPUT_PULLUP, active-low) - Axis/Distance
    Button 2  : GPIO4  (to GND, INPUT_PULLUP, active-low) - Speed/View
    Toggle SW : GPIO27 (to GND, INPUT_PULLUP, active-low) - Power/Sleep
    E-STOP    : GPIO14 (NC contact to GND, INPUT_PULLUP, active-low) - 19mm Emergency Stop
    LCD SCLK  : GPIO18
    LCD MOSI  : GPIO23
    LCD CS    : GPIO15
    LCD DC    : GPIO2  (forced HIGH before init)
    LCD RST   : GPIO21
  Build switches:
    // #define BATTERY_MODE
    // #define BLUETOOTH_MODE
*/

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <math.h>

// ===== Optional BLE (kept off unless you enable it) =====
#ifdef BLUETOOTH_MODE
  #include "BLEDevice.h"
  #include "BLEHIDDevice.h"
  #include "HIDKeyboardTypes.h"
  #include "BLECharacteristic.h"
#endif

// ================= Pins =================
#define PIN_ENCODER_A   32
#define PIN_ENCODER_B   33
#define PIN_BUTTON_1    25  // Axis/Distance (was PIN_MODE_BTN)
#define PIN_BUTTON_2    4   // Speed/View - CHANGED from 26 to avoid conflict
#define PIN_TOGGLE_SW   27  // Power/Sleep - CHANGED from 15 to avoid LCD CS conflict
#define PIN_ESTOP       14  // Emergency Stop (19mm mushroom button, NC contact)
#define PIN_LCD_SCLK    18  // FIXED: Was 19
#define PIN_LCD_MOSI    23  // FIXED: Was 18
#define PIN_LCD_CS      15  // FIXED: Was 26 (conflicted with button)
#define PIN_LCD_DC       2
#define PIN_LCD_RST     21

// ================ Enums =================
enum MachineType { MACHINE_FIRECONTROL=0, MACHINE_CUTCONTROL=1, MACHINE_MANUAL=2, MACHINE_MACH3=3, MACHINE_MACH4=4, MACHINE_LINUXCNC=5, MACHINE_UCCNC=6, MACHINE_CARBIDE=7, MACHINE_UGS=8 };
enum AxisType    { AXIS_Z=0, AXIS_Y=1, AXIS_X=2 };
enum SpeedType   { SPEED_SLOW=0, SPEED_MEDIUM=1, SPEED_FAST=2 };
enum JogDistance { JOG_FINE=0, JOG_SMALL=1, JOG_MEDIUM=2, JOG_LARGE=3 };
enum FeedRate    { FEED_25=0, FEED_50=1, FEED_75=2, FEED_100=3 };
enum CoordView   { VIEW_WCS=0, VIEW_MCS=1 };
enum MotionState { MS_IDLE=0, MS_RUN=1, MS_HOLD=2 };
enum CommProtocol { PROTOCOL_LANGMUIR=0, PROTOCOL_GRBL=1, PROTOCOL_MACH=2, PROTOCOL_LINUXCNC=3 };
enum EstopState  { ESTOP_RELEASED=0, ESTOP_PRESSED=1, ESTOP_FAULT=2 };

#ifdef BATTERY_MODE
enum PowerState  { POWER_ACTIVE=0, POWER_DIM=1, POWER_SLEEP=2, POWER_DEEP_SLEEP=3 };
#else
enum PowerState  { POWER_ACTIVE=0, POWER_COUNTDOWN=1, POWER_SLEEP=2 };
#endif

// ================ Globals ================
#define FW_VERSION "ULP-ESP32 v2.0.0-UNIVERSAL"

MachineType currentMachine = MACHINE_FIRECONTROL;
CommProtocol currentProtocol = PROTOCOL_LANGMUIR;
AxisType    currentAxis    = AXIS_Z;
SpeedType   currentSpeed   = SPEED_MEDIUM;
JogDistance currentJogDistance = JOG_SMALL;
FeedRate    currentFeedRate    = FEED_50;

volatile long encAedges=0;
volatile long encoderPosition=0;
int lastEncoderPos=0;
int encoderScale=1;
volatile long scaledEncoderPosition=0;

int  currentThemeVariant=0;

bool useImperialUnits=false;
bool softwareConnected=false;
bool hostArmed=false;
unsigned long lastHostPing=0;
const unsigned long HOST_TIMEOUT_MS=3500;   // little more forgiving in noisy shops

// Motion gate from host
MotionState hostMotionState = MS_IDLE;

// Local fallback position accumulators (only used if host data stale)
float xPosition=0, yPosition=0, zPosition=0;

// Host live positions + timestamps (WCS and MCS)
struct XYZ { float x=0, y=0, z=0; };
XYZ hostWCS, hostMCS;
unsigned long tHostWCS=0, tHostMCS=0;        // millis timestamps
const unsigned long HOST_POS_STALE_MS = 1200; // if older than this, considered stale

// Active work offset (optional)
XYZ offG54, offG55, offG56, offG57, offG58, offG59;
const XYZ* getOffsetPtr(const String& g){
  if(g=="G54") return &offG54; if(g=="G55") return &offG55; if(g=="G56") return &offG56;
  if(g=="G57") return &offG57; if(g=="G58") return &offG58; if(g=="G59") return &offG59; return nullptr;
}

// WCS selection + view mode
String activeWCS = "G54";
CoordView coordView = VIEW_WCS;
bool viewLockedByHost = false;  // if host issued COORD:VIEW, local 6x click won't toggle

// Anti-ghosting and UI timing
unsigned long btn1PressTime=0, btn2PressTime=0, sleepCountdownStart=0;
bool button1Pressed=false, button1Processed=false;
bool button2Pressed=false, button2Processed=false;
bool toggleSwitchState=true, lastToggleState=true;  // true = ON, false = OFF
const unsigned long LONG_PRESS_THRESHOLD=1000;  // 1 second for long press
const unsigned long DEBOUNCE_DELAY=50;
const unsigned long SLEEP_COUNTDOWN_TIME=5000;  // 5 seconds

// Power management
PowerState currentPowerState = POWER_ACTIVE;
unsigned long lastActivityTime=0;
int sleepCountdown = 0;

// E-stop safety system
EstopState currentEstopState = ESTOP_RELEASED;
bool estopPressed = false, lastEstopState = false;
unsigned long estopPressTime = 0;
const unsigned long ESTOP_DEBOUNCE = 100;  // 100ms debounce for E-stop
bool systemLocked = false;  // Global safety lock when E-stop is active

// Note: BATTERY_MODE extends PowerState with DIM and DEEP_SLEEP states
#ifdef BATTERY_MODE
unsigned long lastHeartbeat=0;
const unsigned long INACTIVITY_TIMEOUT=30000;
const unsigned long SLEEP_TIMEOUT=60000;
const unsigned long DEEP_SLEEP_TIMEOUT=300000;
const unsigned long HEARTBEAT_INTERVAL=10000;
const int BATTERY_BRIGHTNESS=128;
const int NORMAL_BRIGHTNESS=200;
bool batteryLowPowerMode=false;
int  batteryLevel=100;
RTC_DATA_ATTR int bootCount=0;
#endif

uint8_t bgR=0,bgG=0,bgB=0;
bool useRGBWheel=false;
float rgbHue=0.0f;

// Optional BLE plumbing
#ifdef BLUETOOTH_MODE
BLEHIDDevice* hid=nullptr; BLECharacteristic* input=nullptr; BLECharacteristic* output=nullptr; BLECharacteristic* feature=nullptr; BLEServer* gServer=nullptr;
bool bleConnected=false, bleStarted=false;
unsigned long lastConnectionAttempt=0;
const unsigned long BLE_RECONNECT_INTERVAL=5000;
const char* deviceName="AIONMECH Pendant";
#endif

// ============== Colors / UI ==============
static inline uint16_t RGB565(uint8_t r,uint8_t g,uint8_t b){ return ((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3); }
const uint16_t THEME_BG_DARK   = RGB565(8,10,14);
const uint16_t THEME_CARD      = RGB565(20,26,36);
const uint16_t THEME_RING_1    = RGB565(50,60,80);
const uint16_t THEME_RING_2    = RGB565(90,110,140);
const uint16_t THEME_RING_3    = RGB565(140,160,190);
const uint16_t THEME_TEXT_MAIN = RGB565(240,245,255);
const uint16_t THEME_TEXT_SUB  = RGB565(160,175,200);
const uint16_t THEME_PLASMA_ORANGE=RGB565(255,140,0);
const uint16_t THEME_PLASMA_RED   =RGB565(255, 65,85);
const uint16_t THEME_PLASMA_BLUE  =RGB565(  0,145,255);
const uint16_t THEME_MILL_GREEN   =RGB565(  0,220,120);
const uint16_t THEME_MILL_CYAN    =RGB565(  0,200,180);
const uint16_t THEME_MILL_TEAL    =RGB565(  0,180,140);
const uint16_t THEME_MANUAL_PURPLE=RGB565(160,  0,255);
const uint16_t THEME_MANUAL_PINK  =RGB565(255, 20,140);
const uint16_t THEME_MANUAL_GOLD  =RGB565(255,200,  0);

// LovyanGFX panel
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel; lgfx::Bus_SPI _bus;
public:
  LGFX(void){
    auto b=_bus.config();
    b.spi_host=VSPI_HOST; b.spi_mode=0; b.freq_write=40000000; b.freq_read=16000000; b.spi_3wire=false; b.use_lock=true; b.dma_channel=SPI_DMA_CH_AUTO;
    b.pin_sclk=PIN_LCD_SCLK; b.pin_mosi=PIN_LCD_MOSI; b.pin_miso=-1; b.pin_dc=PIN_LCD_DC;
    _bus.config(b); _panel.setBus(&_bus);
    auto p=_panel.config();
    p.pin_cs=PIN_LCD_CS; p.pin_rst=PIN_LCD_RST; p.pin_busy=-1;
    p.panel_width=240; p.panel_height=240; p.offset_x=0; p.offset_y=0; p.offset_rotation=0;
    p.readable=false; p.invert=true; p.rgb_order=false; p.dlen_16bit=false; p.bus_shared=true;
    _panel.config(p);
    setPanel(&_panel);
  }
};
LGFX lcd;
LGFX_Sprite spr(&lcd);  // Sprite for flicker-free double-buffering

// View modes
enum ViewMode { VIEW_NORMAL, VIEW_HELP };
ViewMode currentViewMode = VIEW_NORMAL;

// UI state tracking to minimize redraws
static bool needsFullRedraw = true;
static bool needsAxisRedraw = false;
static bool needsSpeedRedraw = false;
static bool needsStatusRedraw = false;
static unsigned long lastStatusRedraw = 0;
static unsigned long lastPosRedraw = 0;
static AxisType lastDrawnAxis = AXIS_Z;
static SpeedType lastDrawnSpeed = SPEED_MEDIUM;
static JogDistance lastDrawnDistance = JOG_SMALL;
static bool lastDrawnConnected = false;
static float lastDrawnX = -9999, lastDrawnY = -9999, lastDrawnZ = -9999;

// Both-buttons-held tracking for help screen
static bool bothButtonsHeld = false;
static unsigned long bothButtonsTime = 0;
const unsigned long HELP_HOLD_TIME = 1500;  // 1.5s to enter help

// Batch + clip
inline void UI_Begin(){ lcd.startWrite(); }
inline void UI_End(){ lcd.endWrite(); }
inline void SetClip(int x,int y,int w,int h){ lcd.setClipRect(x,y,w,h); }
inline void ClearClip(){ lcd.clearClipRect(); }

// UI regions - reorganized for better fit
const int SCREEN_CX = 120, SCREEN_CY = 120;
const int CARD_RADIUS = 52;  // Slightly smaller center card
const int STATUS_Y = 18;
const int POS_AREA_Y = 172;  // Position display area at bottom (below center card)

// Utilities
static inline void c565_to_rgb8(uint16_t c,uint8_t &r,uint8_t &g,uint8_t &b){ r=((c>>11)&0x1F)*255/31; g=((c>>5)&0x3F)*255/63; b=(c&0x1F)*255/31; }
static inline uint16_t rgb8_to_565(uint8_t r,uint8_t g,uint8_t b){ return ((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3); }
static inline uint16_t blend565(uint16_t fg,uint16_t bg,uint8_t a){
  if(a==255) return fg; if(a==0) return bg;
  uint8_t fr,fg8,fb, br,bg8,bb; c565_to_rgb8(fg,fr,fg8,fb); c565_to_rgb8(bg,br,bg8,bb);
  uint8_t rr=(uint8_t)((fr*a+br*(255-a))/255), gg=(uint8_t)((fg8*a+bg8*(255-a))/255), bb2=(uint8_t)((fb*a+bb*(255-a))/255);
  return rgb8_to_565(rr,gg,bb2);
}

uint16_t getMachineAccentColor(){
  switch(currentMachine){
    case MACHINE_FIRECONTROL: return (currentThemeVariant==1)?THEME_PLASMA_RED:(currentThemeVariant==2)?THEME_PLASMA_BLUE:THEME_PLASMA_ORANGE;
    case MACHINE_CUTCONTROL : return (currentThemeVariant==1)?THEME_MILL_CYAN :(currentThemeVariant==2)?THEME_MILL_TEAL :THEME_MILL_GREEN;
    case MACHINE_MACH3      : return RGB565(255,128,0);   // Orange
    case MACHINE_MACH4      : return RGB565(0,160,255);   // Blue  
    case MACHINE_LINUXCNC   : return RGB565(255,80,80);   // Red
    case MACHINE_UCCNC      : return RGB565(80,255,80);   // Green
    case MACHINE_CARBIDE    : return RGB565(255,200,0);   // Gold
    case MACHINE_UGS        : return RGB565(160,80,255);  // Purple
    default                 : return (currentThemeVariant==1)?THEME_MANUAL_PINK:(currentThemeVariant==2)?THEME_MANUAL_GOLD:THEME_MANUAL_PURPLE;
  }
}

const char* getMachineName(){ 
  switch(currentMachine){
    case MACHINE_FIRECONTROL: return "FIRECONTROL";
    case MACHINE_CUTCONTROL:  return "CUTCONTROL";
    case MACHINE_MACH3:       return "MACH3";
    case MACHINE_MACH4:       return "MACH4";
    case MACHINE_LINUXCNC:    return "LINUXCNC";
    case MACHINE_UCCNC:       return "UCCNC";
    case MACHINE_CARBIDE:     return "CARBIDE";
    case MACHINE_UGS:         return "UGS";
    case MACHINE_MANUAL:      return "MANUAL";
    default: return "UNKNOWN";
  }
}

const char* getMachineDisplayName(){ 
  switch(currentMachine){
    case MACHINE_FIRECONTROL: return "PLASMA";
    case MACHINE_CUTCONTROL:  return "MILL";
    case MACHINE_MACH3:       return "MACH3";
    case MACHINE_MACH4:       return "MACH4";
    case MACHINE_LINUXCNC:    return "LINUX";
    case MACHINE_UCCNC:       return "UCCNC";
    case MACHINE_CARBIDE:     return "CARBIDE";
    case MACHINE_UGS:         return "UGS";
    case MACHINE_MANUAL:      return "MANUAL";
    default: return "UNKNOWN";
  }
}

// Universal protocol support
void sendUniversalEstopCommand(bool estopActive){
  // Send appropriate E-stop/reset commands for each CNC platform
  if(estopActive){
    systemLocked = true;
    switch(currentProtocol){
      case PROTOCOL_LANGMUIR:
        Serial.println("ESTOP:ACTIVE");
        Serial.println("PAUSE");  // FireControl/CutControl emergency pause
        break;
      case PROTOCOL_GRBL:
        Serial.write(0x85);  // GRBL real-time jog cancel
        Serial.println("!");  // GRBL feed hold
        Serial.println("$X");  // GRBL alarm clear (for when released)
        break;
      case PROTOCOL_MACH:
        Serial.println("G80");  // Cancel canned cycles
        Serial.println("M0");   // Program stop
        break;
      case PROTOCOL_LINUXCNC:
        Serial.println("M1");   // Optional stop
        Serial.println("G80");  // Cancel modal commands
        break;
    }
    Serial.println("ESTOP:PRESSED");
  } else {
    systemLocked = false;
    switch(currentProtocol){
      case PROTOCOL_LANGMUIR:
        // Let user manually resume in software
        break;
      case PROTOCOL_GRBL:
        Serial.println("$X");  // Clear alarms
        Serial.println("~");   // Cycle start/resume
        break;
      case PROTOCOL_MACH:
        // Mach requires manual cycle start usually
        break;
      case PROTOCOL_LINUXCNC:
        // LinuxCNC requires manual resume
        break;
    }
    Serial.println("ESTOP:RELEASED");
  }
}

void sendUniversalJogCommand(char axis, bool positive, float distance){
  // SAFETY: Block all motion commands if E-stop is active
  if(systemLocked || currentEstopState != ESTOP_RELEASED){
    Serial.println("ESTOP:MOTION_BLOCKED");
    return;
  }
  
  switch(currentProtocol){
    case PROTOCOL_LANGMUIR:
      Serial.printf("JOG:%c,%c%.4f\n", axis, positive?'+':'-', distance);
      Serial.printf("MOVE:%c,%c%.4f%s\n", axis, positive?'+':'-', distance, useImperialUnits?"IN":"MM");
      break;
    case PROTOCOL_GRBL:
      // GRBL jog format: $J=G91G1X10.0F1000
      Serial.printf("$J=G91G1%c%.4fF%d\n", axis, positive?distance:-distance, (currentSpeed==SPEED_SLOW)?500:(currentSpeed==SPEED_FAST)?2000:1000);
      break;
    case PROTOCOL_MACH:
      // Mach3/4 uses G91 incremental mode  
      Serial.printf("G91G1%c%.4fF%d\n", axis, positive?distance:-distance, (currentSpeed==SPEED_SLOW)?100:(currentSpeed==SPEED_FAST)?1000:500);
      break;
    case PROTOCOL_LINUXCNC:
      // LinuxCNC MDI format
      Serial.printf("G91 G1 %c%.4f F%d\n", axis, positive?distance:-distance, (currentSpeed==SPEED_SLOW)?100:(currentSpeed==SPEED_FAST)?1000:500);
      break;
  }
}

// Pretty bits
void drawGlassCirclePanel(int cx,int cy,int r,uint16_t tint,uint16_t bgBase){
  for(int i=0;i<10;i++){
    int rr=r-i; if(rr<=0) break;
    uint8_t a=(uint8_t)(200-(i*170/10));
    lcd.drawCircle(cx,cy,rr,blend565(tint,bgBase,a));
  }
  lcd.drawCircle(cx-5, cy-8, r-11, blend565(RGB565(255,255,255), bgBase, 90));
  lcd.fillCircle (cx-11,cy-16, r/7,  blend565(RGB565(255,255,255), bgBase, 70));
}
void drawGlassPill(int x,int y,int w,int h,uint16_t tint,uint16_t bgBase,uint16_t outline){
  if(x<10)x=10;if(y<10)y=10;if(x+w>230)x=230-w;if(y+h>230)y=230-h;
  int r=h/2;
  lcd.fillRoundRect(x+1,y+2,w,h,r,blend565(RGB565(0,0,0),bgBase,140));
  for(int i=0;i<6;i++){ int ix=x+i,iy=y+i,iw=w-2*i,ih=h-2*i; if(iw<=0||ih<=0) break;
    uint8_t a=(uint8_t)(160-abs(i-3)*28);
    lcd.fillRoundRect(ix,iy,iw,ih,ih/2,blend565(tint,bgBase,a));
  }
  lcd.fillRoundRect(x+3,y+3,w-6,h/3,(h/3)/2,blend565(RGB565(255,255,255),bgBase,80));
  lcd.drawRoundRect(x,y,w,h,r,outline);
}

void drawBackground(){
  if(useRGBWheel){
    float h=fmodf(rgbHue,360.0f), s=1.0f, v=0.35f;
    float C=v*s, X=C*(1-fabs(fmod(h/60.0f,2)-1)), m=v-C;
    float r1=0,g1=0,b1=0;
    if(h<60){r1=C;g1=X;} else if(h<120){r1=X;g1=C;} else if(h<180){g1=C;b1=X;}
    else if(h<240){g1=X;b1=C;} else if(h<300){r1=X;b1=C;} else {r1=C;b1=X;}
    uint8_t r=(uint8_t)((r1+m)*255), g=(uint8_t)((g1+m)*255), b=(uint8_t)((b1+m)*255);
    lcd.fillScreen(rgb8_to_565(r,g,b));
  } else {
    uint16_t color = (bgR|bgG|bgB)? rgb8_to_565(bgR,bgG,bgB): THEME_BG_DARK;
    lcd.fillScreen(color);
  }
}
void drawStatusStaticRings(){ UI_Begin(); lcd.drawCircle(120,120,118,THEME_RING_1); lcd.drawCircle(120,120,117,THEME_RING_2); lcd.drawCircle(120,120,116,THEME_RING_3); UI_End(); }

// Forward declarations for UI functions
void drawAxisDisplay();
void drawConnectionStatus();
void drawPositionDisplay();

// Request a full UI refresh - deferred to avoid flicker from rapid calls
void requestFullRedraw(){ needsFullRedraw = true; }

// Perform immediate full redraw (only use when absolutely necessary)
void doFullRedraw(){
  UI_Begin(); 
  drawBackground(); 
  drawStatusStaticRings(); 
  drawAxisDisplay(); 
  drawConnectionStatus(); 
  UI_End();
  needsFullRedraw = false;
}

// Distance text/val (same mapping as before)
String getDistanceText(){
  if(useImperialUnits){
    if(currentMachine==MACHINE_FIRECONTROL){
      switch(currentJogDistance){case JOG_FINE:return "0.0005\"";case JOG_SMALL:return "0.001\"";case JOG_MEDIUM:return "0.01\"";case JOG_LARGE:return "0.1\"";}
    } else {
      switch(currentJogDistance){case JOG_FINE:return "0.004\"";case JOG_SMALL:return "0.039\"";case JOG_MEDIUM:return "0.394\"";case JOG_LARGE:return "3.937\"";}
    }
  } else {
    if(currentMachine==MACHINE_FIRECONTROL){
      switch(currentJogDistance){case JOG_FINE:return "0.0125mm";case JOG_SMALL:return "0.025mm";case JOG_MEDIUM:return "0.25mm";case JOG_LARGE:return "2.5mm";}
    } else {
      switch(currentJogDistance){case JOG_FINE:return "0.1mm";case JOG_SMALL:return "1.0mm";case JOG_MEDIUM:return "10.0mm";case JOG_LARGE:return "100.0mm";}
    }
  }
  return useImperialUnits?"0.001\"":(currentMachine==MACHINE_FIRECONTROL?"0.025mm":"1.0mm");
}

// Get feed rate as percentage string
const char* getFeedRatePercent(){
  switch(currentFeedRate){
    case FEED_25: return "25%";
    case FEED_50: return "50%";
    case FEED_75: return "75%";
    case FEED_100: return "100%";
    default: return "50%";
  }
}

float getJogDistanceValue(){
  if(useImperialUnits){
    if(currentMachine==MACHINE_FIRECONTROL){switch(currentJogDistance){case JOG_FINE:return 0.0005f;case JOG_SMALL:return 0.001f;case JOG_MEDIUM:return 0.01f;case JOG_LARGE:return 0.1f;}}
    else {switch(currentJogDistance){case JOG_FINE:return 0.004f;case JOG_SMALL:return 0.039f;case JOG_MEDIUM:return 0.394f;case JOG_LARGE:return 3.937f;}}
  } else {
    if(currentMachine==MACHINE_FIRECONTROL){switch(currentJogDistance){case JOG_FINE:return 0.0125f;case JOG_SMALL:return 0.025f;case JOG_MEDIUM:return 0.25f;case JOG_LARGE:return 2.5f;}}
    else {switch(currentJogDistance){case JOG_FINE:return 0.1f;case JOG_SMALL:return 1.0f;case JOG_MEDIUM:return 10.0f;case JOG_LARGE:return 100.0f;}}
  }
  return useImperialUnits?0.001f:(currentMachine==MACHINE_FIRECONTROL?0.025f:1.0f);
}

// Determine what to show (host vs local)
bool hostWCSFresh(){ return (millis()-tHostWCS) <= HOST_POS_STALE_MS; }
bool hostMCSFresh(){ return (millis()-tHostMCS) <= HOST_POS_STALE_MS; }

// Axis card + live position readout - FLICKER-FREE with sprite
void drawAxisDisplay(){
  uint16_t accent=getMachineAccentColor();
  
  // Create sprite for center area (avoids flicker)
  const int sprW = 140, sprH = 110;
  spr.createSprite(sprW, sprH);
  spr.fillSprite(THEME_BG_DARK);
  
  // Draw glass circle effect in sprite
  int cx = sprW/2, cy = 45;
  for(int i=0;i<10;i++){
    int rr=CARD_RADIUS-i; if(rr<=0) break;
    uint8_t a=(uint8_t)(200-(i*160/10));
    spr.drawCircle(cx,cy,rr,blend565(accent,THEME_BG_DARK,a));
  }
  // Highlight spot
  spr.fillCircle(cx-8, cy-12, 4, blend565(RGB565(255,255,255), THEME_BG_DARK, 50));

  // Armed status ring
  bool armed = hostArmed && (millis()-lastHostPing) <= HOST_TIMEOUT_MS && hostMotionState == MS_IDLE;
  if(armed){
    spr.drawCircle(cx, cy, CARD_RADIUS+2, RGB565(0,255,0));
    spr.drawCircle(cx, cy, CARD_RADIUS+3, RGB565(0,200,0));
  }

  // Axis letter - big and bold
  const char* axisText; uint16_t axisColor;
  switch(currentAxis){ 
    case AXIS_Z: axisText="Z"; axisColor=RGB565(100,100,255); break;
    case AXIS_Y: axisText="Y"; axisColor=RGB565(100,255,100); break;
    default    : axisText="X"; axisColor=RGB565(255,100,100); break; 
  }

  spr.setTextDatum(middle_center);
  spr.setFont(&fonts::Font4);
  spr.setTextColor(axisColor); 
  spr.drawString(axisText, cx, cy-10);
  
  // "AXIS" label
  spr.setFont(&fonts::Font0);
  spr.setTextColor(THEME_TEXT_SUB);
  spr.drawString("AXIS", cx, cy+8);

  // Distance text
  String distanceText=getDistanceText();
  spr.setTextColor(RGB565(255,220,100));
  spr.drawString(distanceText.c_str(), cx, cy+24);

  // Speed indicator
  const char* spd = currentSpeed==SPEED_SLOW?"SLOW":currentSpeed==SPEED_FAST?"FAST":"MED";
  uint16_t spdCol = currentSpeed==SPEED_SLOW?RGB565(100,255,100):currentSpeed==SPEED_FAST?RGB565(255,100,100):RGB565(255,255,100);
  spr.setTextColor(spdCol);
  spr.drawString(spd, cx, cy+38);

  // Feed override for mill mode
  if(currentMachine==MACHINE_CUTCONTROL){
    const char* feed="50%";
    switch(currentFeedRate){case FEED_25:feed="25%";break;case FEED_50:feed="50%";break;case FEED_75:feed="75%";break;case FEED_100:feed="100%";break;}
    spr.setTextColor(RGB565(100,200,255));
    spr.drawString(feed, cx, cy+52);
  }
  
  // Push sprite to screen - centered
  spr.pushSprite(SCREEN_CX - sprW/2, 55);
  spr.deleteSprite();

  // Live positions area
  drawPositionDisplay();
}

// Separate position display function with sprite buffering
void drawPositionDisplay(){
  bool showWCS = (coordView==VIEW_WCS);
  bool fresh   = showWCS ? hostWCSFresh() : hostMCSFresh();
  float sx=0, sy=0, sz=0;
  if(fresh){
    if(showWCS){ sx=hostWCS.x; sy=hostWCS.y; sz=hostWCS.z; } else { sx=hostMCS.x; sy=hostMCS.y; sz=hostMCS.z; }
  } else {
    sx = xPosition; sy = yPosition; sz = zPosition;
  }

  // Skip redraw if positions haven't changed significantly
  if(fabs(sx - lastDrawnX) < 0.0001f && fabs(sy - lastDrawnY) < 0.0001f && 
     fabs(sz - lastDrawnZ) < 0.0001f && !needsFullRedraw) {
    return;
  }
  lastDrawnX = sx; lastDrawnY = sy; lastDrawnZ = sz;

  uint16_t accent = getMachineAccentColor();
  
  // Create sprite for position area
  const int posW = 200, posH = 60;
  spr.createSprite(posW, posH);
  spr.fillSprite(THEME_BG_DARK);
  
  // Draw subtle border
  spr.drawRoundRect(0, 0, posW, posH, 8, THEME_RING_1);
  
  char buf[16];
  int colW = 64;  // Column width for each axis
  int baseY = 12;
  
  // X column
  spr.setTextDatum(top_center);
  spr.setFont(&fonts::Font0);
  spr.setTextColor(RGB565(255,120,120));
  spr.drawString("X", colW/2, 4);
  snprintf(buf, sizeof(buf), "%.3f", sx);
  spr.setTextColor(THEME_TEXT_MAIN);
  spr.drawString(buf, colW/2, baseY + 8);
  
  // Y column
  spr.setTextColor(RGB565(120,255,120));
  spr.drawString("Y", colW + colW/2, 4);
  snprintf(buf, sizeof(buf), "%.3f", sy);
  spr.setTextColor(THEME_TEXT_MAIN);
  spr.drawString(buf, colW + colW/2, baseY + 8);
  
  // Z column
  spr.setTextColor(RGB565(120,120,255));
  spr.drawString("Z", 2*colW + colW/2, 4);
  snprintf(buf, sizeof(buf), "%.3f", sz);
  spr.setTextColor(THEME_TEXT_MAIN);
  spr.drawString(buf, 2*colW + colW/2, baseY + 8);
  
  // Bottom row: Units | WCS/MCS | Machine | Fresh indicator
  int bottomY = posH - 12;
  spr.setTextDatum(bottom_left);
  spr.setTextColor(accent);
  spr.drawString(useImperialUnits ? "IN" : "MM", 8, bottomY);
  
  spr.setTextDatum(bottom_center);
  spr.setTextColor(THEME_TEXT_SUB);
  spr.drawString(showWCS ? activeWCS.c_str() : "MCS", posW/2 - 20, bottomY);
  
  spr.setTextColor(accent);
  spr.drawString(getMachineDisplayName(), posW/2 + 30, bottomY);
  
  // Freshness indicator dot
  uint16_t dot = fresh ? RGB565(0,255,100) : RGB565(255,180,0);
  spr.fillCircle(posW - 12, bottomY - 4, 4, dot);
  
  // Push to screen
  spr.pushSprite(SCREEN_CX - posW/2, POS_AREA_Y);
  spr.deleteSprite();
}

void drawSpeedIndicator(){
  // Speed is now shown in the center axis card - this just forces a redraw
  if(lastDrawnSpeed != currentSpeed){
    lastDrawnSpeed = currentSpeed;
    drawAxisDisplay();
  }
}

void drawConnectionStatus(){
  // Rate-limit status updates to reduce flicker
  unsigned long now = millis();
  if(now - lastStatusRedraw < 500 && !needsFullRedraw && lastDrawnConnected == softwareConnected) return;
  lastStatusRedraw = now;
  lastDrawnConnected = softwareConnected;
  
  static bool pulse = false;
  pulse = !pulse;
  uint16_t accent = getMachineAccentColor();

  // Create sprite for top status bar
  const int barW = 200, barH = 28;
  spr.createSprite(barW, barH);
  spr.fillSprite(THEME_BG_DARK);
  
  // Left side: WCS indicator
  spr.setTextDatum(middle_left);
  spr.setFont(&fonts::Font0);
  spr.setTextColor(accent);
  spr.drawString(activeWCS.c_str(), 4, barH/2);
  
  // Center: connection status
  spr.setTextDatum(middle_center);
  uint16_t statusColor;
  const char* statusText;
  if(softwareConnected){ 
    statusColor = pulse ? accent : blend565(accent, THEME_BG_DARK, 180); 
    statusText = "CONNECTED"; 
  } else { 
    statusColor = pulse ? RGB565(255,255,0) : RGB565(200,100,0); 
    statusText = "USB WAIT"; 
  }
  spr.setTextColor(THEME_TEXT_MAIN);
  spr.drawString(statusText, barW/2, barH/2);
  
  // Connection indicator dot
  spr.fillCircle(barW/2 + 40, barH/2, 4, statusColor);
  
  // Right side: arm status
  spr.setTextDatum(middle_right);
  if(hostArmed && (millis()-lastHostPing) <= HOST_TIMEOUT_MS){
    spr.setTextColor(RGB565(0,255,0));
    spr.drawString("ARMED", barW - 4, barH/2);
  } else {
    spr.setTextColor(RGB565(255,100,0));
    spr.drawString("DISARM", barW - 4, barH/2);
  }
  
  // Push to screen
  spr.pushSprite(SCREEN_CX - barW/2, 4);
  spr.deleteSprite();
}

void drawBootScreen(){
  lcd.fillScreen(0x0000);
  UI_Begin();
  for(int r=115;r<=118;r++){
    uint16_t col=(r==118)?getMachineAccentColor():(r==117)?blend565(getMachineAccentColor(),THEME_BG_DARK,180):blend565(getMachineAccentColor(),THEME_BG_DARK,120);
    lcd.drawCircle(120,120,r,col);
  }
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(getMachineAccentColor(),0x0000);
  lcd.setFont(&fonts::Font4); lcd.drawString("AIONMECH",120,70);
  lcd.setTextColor(THEME_TEXT_MAIN,0x0000);
  lcd.setFont(&fonts::Font2); lcd.drawString("CNC PENDANT",120,95);
  lcd.setTextColor(THEME_TEXT_SUB,0x0000);
  lcd.setFont(&fonts::Font2); lcd.drawString(getMachineName(),120,115);

  // Shortened instructions to fit screen
  lcd.setFont(&fonts::Font0); lcd.setTextColor(0xFFE0,0x0000);
  lcd.drawString("BTN1: Axis  BTN2: Speed",120,145);
  lcd.drawString("HOLD: Distance/View",120,160);
  lcd.drawString("Encoder: Jog",120,175);
  UI_End();
  
  delay(1500);  // Show boot screen for 1.5 seconds
}

// ====== Encoder ISR ======
void IRAM_ATTR encoderISR(){
  static uint32_t last_us=0;
  uint32_t now_us=(uint32_t)esp_timer_get_time();
  if(now_us-last_us>5000){ encAedges++; last_us=now_us; } // ~5ms debounce
}

// ====== Movement helpers ======
void updatePositionTracking(float movement){
  // Only for fallback; doesn't try to be "truthier" than host
  switch(currentAxis){ case AXIS_X:xPosition+=movement; break; case AXIS_Y:yPosition+=movement; break; case AXIS_Z:zPosition+=movement; break; }
}

// ====== Buttons ======
void drawHelpScreen(){
  UI_Begin();
  lcd.fillScreen(THEME_BG_DARK);
  
  uint16_t accent = getMachineAccentColor();
  
  // Header
  lcd.setTextDatum(top_center);
  lcd.setTextColor(accent, THEME_BG_DARK);
  lcd.setFont(&fonts::Font2); 
  lcd.drawString("QUICK GUIDE", SCREEN_CX, 12);
  
  lcd.setTextDatum(top_left);
  lcd.setFont(&fonts::Font0);
  
  int x = 24, y = 38;
  int lineH = 15;
  
  // Button 1
  lcd.setTextColor(RGB565(255,120,120), THEME_BG_DARK);
  lcd.drawString("BTN1 (Left):", x, y); y += lineH;
  lcd.setTextColor(THEME_TEXT_MAIN, THEME_BG_DARK);
  lcd.drawString("  Short: Cycle Axis", x, y); y += lineH;
  lcd.drawString("  Hold:  Jog Distance", x, y); y += lineH + 4;
  
  // Button 2
  lcd.setTextColor(RGB565(120,255,120), THEME_BG_DARK);
  lcd.drawString("BTN2 (Right):", x, y); y += lineH;
  lcd.setTextColor(THEME_TEXT_MAIN, THEME_BG_DARK);
  lcd.drawString("  Short: Cycle Speed", x, y); y += lineH;
  lcd.drawString("  Hold:  WCS/MCS View", x, y); y += lineH + 4;
  
  // Encoder
  lcd.setTextColor(RGB565(120,120,255), THEME_BG_DARK);
  lcd.drawString("ENCODER:", x, y); y += lineH;
  lcd.setTextColor(THEME_TEXT_MAIN, THEME_BG_DARK);
  lcd.drawString("  Rotate to jog", x, y); y += lineH + 4;
  
  // Toggle
  lcd.setTextColor(accent, THEME_BG_DARK);
  lcd.drawString("TOGGLE: Power On/Off", x, y); y += lineH + 6;
  
  // Exit hint at bottom
  lcd.setTextDatum(bottom_center);
  lcd.setTextColor(THEME_TEXT_SUB, THEME_BG_DARK);
  lcd.drawString("Hold both BTN to exit", SCREEN_CX, 230);
  
  UI_End();
}

// ================ NEW BUTTON HANDLING SYSTEM =================

void handleButton1(){
  // Button 1: Short press = change axis, Long press = change jog distance
  bool raw = digitalRead(PIN_BUTTON_1);
  bool down = !raw;  // Active low
  static bool last = false; 
  static unsigned long dbT = 0; 
  unsigned long now = millis();
  
  if(down != last) dbT = now;
  if((now - dbT) > DEBOUNCE_DELAY){
    if(down && !button1Pressed){ 
      button1Pressed = true; 
      button1Processed = false; 
      btn1PressTime = now; 
      lastActivityTime = now;
    }
    else if(!down && button1Pressed){
      button1Pressed = false;
      unsigned long dur = now - btn1PressTime;
      if(!button1Processed){
        if(dur >= LONG_PRESS_THRESHOLD){
          // Long press: Change jog distance
          currentJogDistance = (JogDistance)((currentJogDistance + 1) % 4);
          Serial.print("DISTANCE:"); Serial.println(getJogDistanceValue(), 4);
          if(currentMachine == MACHINE_CUTCONTROL && hostArmed && (millis() - lastHostPing) <= HOST_TIMEOUT_MS && hostMotionState == MS_IDLE){
            switch(currentJogDistance){
              case JOG_FINE: Serial.println("KEY:1,1"); break;
              case JOG_SMALL: Serial.println("KEY:2,1"); break;
              case JOG_MEDIUM: Serial.println("KEY:3,1"); break;
              case JOG_LARGE: Serial.println("KEY:4,1"); break;
            }
          }
          UI_Begin(); drawAxisDisplay(); UI_End();  // Redraw to show new distance
        } else {
          // Short press: Change axis
          currentAxis = (AxisType)((currentAxis + 1) % 3);
          Serial.print("AXIS:"); Serial.println(currentAxis == AXIS_Z ? "Z" : currentAxis == AXIS_Y ? "Y" : "X");
          UI_Begin(); drawAxisDisplay(); UI_End();
        }
        button1Processed = true;
      }
    }
    last = down;
  }
}

void handleButton2(){
  // Button 2: Short press = change speed, Long press = change coordinate view
  bool raw = digitalRead(PIN_BUTTON_2);
  bool down = !raw;  // Active low
  static bool last = false; 
  static unsigned long dbT = 0; 
  unsigned long now = millis();
  
  if(down != last) dbT = now;
  if((now - dbT) > DEBOUNCE_DELAY){
    if(down && !button2Pressed){ 
      button2Pressed = true; 
      button2Processed = false; 
      btn2PressTime = now; 
      lastActivityTime = now;
    }
    else if(!down && button2Pressed){
      button2Pressed = false;
      unsigned long dur = now - btn2PressTime;
      if(!button2Processed){
        if(dur >= LONG_PRESS_THRESHOLD){
          // Long press: Change coordinate view (WCS/MCS)
          if(!viewLockedByHost){
            coordView = (coordView == VIEW_WCS) ? VIEW_MCS : VIEW_WCS;
            Serial.print("COORD:VIEW,"); Serial.println(coordView == VIEW_WCS ? "WCS" : "MCS");
            requestFullRedraw();
          }
        } else {
          // Short press: Change speed (or feedrate for mill)
          if(currentMachine == MACHINE_CUTCONTROL){
            currentFeedRate = (FeedRate)((currentFeedRate + 1) % 4);
            Serial.print("FEEDRATE:"); Serial.println(getFeedRatePercent());
          } else {
            currentSpeed = (SpeedType)((currentSpeed + 1) % 3);
            Serial.print("SPEED:"); Serial.println(currentSpeed == SPEED_SLOW ? "SLOW" : currentSpeed == SPEED_FAST ? "FAST" : "MEDIUM");
          }
          UI_Begin(); drawAxisDisplay(); UI_End();  // Speed shown in center card
        }
        button2Processed = true;
      }
    }
    last = down;
  }
}

// Check if both buttons held simultaneously for help screen
void handleBothButtons(){
  bool btn1Raw = digitalRead(PIN_BUTTON_1);
  bool btn2Raw = digitalRead(PIN_BUTTON_2);
  bool btn1Down = !btn1Raw;  // Active low
  bool btn2Down = !btn2Raw;
  
  unsigned long now = millis();
  
  if(btn1Down && btn2Down){
    if(!bothButtonsHeld){
      bothButtonsHeld = true;
      bothButtonsTime = now;
    } else if(now - bothButtonsTime >= HELP_HOLD_TIME){
      // Both buttons held long enough
      if(currentViewMode == VIEW_NORMAL){
        currentViewMode = VIEW_HELP;
        UI_Begin(); drawHelpScreen(); UI_End();
        button1Processed = true;  // Consume button events
        button2Processed = true;
      } else {
        currentViewMode = VIEW_NORMAL;
        requestFullRedraw();
        button1Processed = true;
        button2Processed = true;
      }
      bothButtonsTime = now + 10000;  // Prevent rapid toggle
    }
  } else {
    bothButtonsHeld = false;
  }
}

void handleToggleSwitch(){
  // Toggle switch: Power/Sleep management
  bool raw = digitalRead(PIN_TOGGLE_SW);
  bool current = !raw;  // Active low (true = ON, false = OFF)
  
  if(current != lastToggleState){
    lastToggleState = current;
    lastActivityTime = millis();
    
    if(current){
      // Switch turned ON - wake up
      if(currentPowerState != POWER_ACTIVE){
        currentPowerState = POWER_ACTIVE;
#ifndef BATTERY_MODE
        sleepCountdown = 0;
#endif
        Serial.println("POWER:ACTIVE");
        // Re-initialize display
        doFullRedraw();
      }
    } else {
      // Switch turned OFF - start sleep countdown
#ifndef BATTERY_MODE
      if(currentPowerState == POWER_ACTIVE){
        currentPowerState = POWER_COUNTDOWN;
        sleepCountdownStart = millis();
        sleepCountdown = 5;
        Serial.println("POWER:COUNTDOWN");
      }
#else
      if(currentPowerState == POWER_ACTIVE){
        currentPowerState = POWER_SLEEP;
        Serial.println("POWER:SLEEP");
        lcd.fillScreen(0x0000);
      }
#endif
    }
  }
}

void handlePowerManagement(){
  unsigned long now = millis();
  
#ifndef BATTERY_MODE
  if(currentPowerState == POWER_COUNTDOWN){
    unsigned long elapsed = now - sleepCountdownStart;
    int newCountdown = 5 - (elapsed / 1000);
    
    if(newCountdown != sleepCountdown && newCountdown >= 0){
      sleepCountdown = newCountdown;
      // Display countdown using sprite for flicker-free
      spr.createSprite(120, 80);
      spr.fillSprite(0x0000);
      spr.setTextDatum(middle_center);
      spr.setFont(&fonts::Font2);
      spr.setTextColor(THEME_TEXT_MAIN);
      spr.drawString("SLEEP IN", 60, 20);
      spr.setFont(&fonts::Font4);
      spr.setTextColor(RGB565(255,200,0));
      spr.drawString(String(sleepCountdown).c_str(), 60, 50);
      
      UI_Begin();
      lcd.fillScreen(0x0000);
      spr.pushSprite(SCREEN_CX - 60, SCREEN_CY - 40);
      UI_End();
      spr.deleteSprite();
    }
    
    if(elapsed >= SLEEP_COUNTDOWN_TIME){
      currentPowerState = POWER_SLEEP;
      Serial.println("POWER:SLEEP");
      lcd.fillScreen(0x0000);
    }
  }
#endif
}

void handleEstop(){
  // E-stop button monitoring with proper debouncing
  bool raw = digitalRead(PIN_ESTOP);
  bool pressed = !raw;  // Active low (NC contact opens when pressed)
  static bool last = false;
  static unsigned long dbT = 0;
  unsigned long now = millis();
  
  if(pressed != last) dbT = now;
  if((now - dbT) > ESTOP_DEBOUNCE){
    if(pressed != estopPressed){
      estopPressed = pressed;
      estopPressTime = now;
      lastActivityTime = now;
      
      if(pressed){
        // E-stop activated
        currentEstopState = ESTOP_PRESSED;
        sendUniversalEstopCommand(true);
        Serial.println("ESTOP:EMERGENCY_STOP_ACTIVATED");
        
        // Visual indication - red screen with E-STOP text
        UI_Begin();
        lcd.fillScreen(RGB565(180, 0, 0));
        
        // Pulsing rings
        for(int r = 60; r < 115; r += 15){
          lcd.drawCircle(SCREEN_CX, SCREEN_CY, r, RGB565(255, 50, 50));
        }
        
        lcd.setTextDatum(middle_center);
        lcd.setFont(&fonts::Font4);
        lcd.setTextColor(RGB565(255,255,255), RGB565(180,0,0));
        lcd.drawString("E-STOP", SCREEN_CX, SCREEN_CY - 15);
        lcd.setFont(&fonts::Font2);
        lcd.setTextColor(RGB565(255,200,200), RGB565(180,0,0));
        lcd.drawString("EMERGENCY STOP", SCREEN_CX, SCREEN_CY + 20);
        lcd.setFont(&fonts::Font0);
        lcd.setTextColor(RGB565(255,255,100), RGB565(180,0,0));
        lcd.drawString("Release to resume", SCREEN_CX, SCREEN_CY + 50);
        UI_End();
      } else {
        // E-stop released
        currentEstopState = ESTOP_RELEASED;
        sendUniversalEstopCommand(false);
        Serial.println("ESTOP:EMERGENCY_STOP_RELEASED");
        
        // Restore normal display
        doFullRedraw();
      }
    }
    last = pressed;
  }
}

// ====== Key send (Serial or BLE) ======
#ifdef BLUETOOTH_MODE
// … (same as before) …
#endif
void sendKeyCommand(const char* command){
#ifdef BLUETOOTH_MODE
  // If BLE is enabled, we could map here; otherwise fall back to Serial:
  Serial.println(command);
#else
  Serial.println(command);
#endif
}

// ====== Encoder handler ======
void handleEncoder(){
  long pulses=encAedges; if(pulses==0) return; encAedges=0;
  int dir = digitalRead(PIN_ENCODER_B) ? +1 : -1;
  encoderPosition += dir * pulses;
  // clamp just in case
  if(encoderPosition>1000000L) encoderPosition=1000000L;
  if(encoderPosition<-1000000L) encoderPosition=-1000000L;

  long currentPos=encoderPosition;
  int rawMovement=currentPos-lastEncoderPos;
  if(rawMovement==0) return;
  lastEncoderPos=currentPos;

  scaledEncoderPosition += rawMovement;
  int scaledMovement = scaledEncoderPosition / encoderScale;
  if(scaledMovement==0) return;
  scaledEncoderPosition = scaledEncoderPosition % encoderScale;

  // SAFETY GATES: armed + heartbeat + machine idle
  if(!(hostArmed && (millis()-lastHostPing)<=HOST_TIMEOUT_MS && hostMotionState==MS_IDLE)){
    scaledEncoderPosition=0; return;
  }

  bool positive = scaledMovement>0;
  float jogDistance = getJogDistanceValue();
  float actual = (positive?jogDistance:-jogDistance) * abs(scaledMovement);

  updatePositionTracking(actual);

  char axisChar = (currentAxis==AXIS_X)?'X': (currentAxis==AXIS_Y)?'Y':'Z';
  
  // Send universal jog command based on current protocol
  sendUniversalJogCommand(axisChar, positive, fabs(actual));

  // Legacy keyboard commands for Windows app compatibility
  if(currentProtocol == PROTOCOL_LANGMUIR){
    int hold= (currentJogDistance==JOG_FINE)?12:(currentJogDistance==JOG_SMALL)?18:(currentJogDistance==JOG_MEDIUM)?28:40;
    switch(currentAxis){
      case AXIS_Z: sendKeyCommand(positive?"KEY:PGUP,1":"KEY:PGDN,1"); delay(hold); sendKeyCommand(positive?"KEY:PGUP,0":"KEY:PGDN,0"); break;
      case AXIS_Y: sendKeyCommand(positive?"KEY:UP,1":"KEY:DOWN,1");   delay(hold); sendKeyCommand(positive?"KEY:UP,0":"KEY:DOWN,0");   break;
      case AXIS_X: sendKeyCommand(positive?"KEY:RIGHT,1":"KEY:LEFT,1");delay(hold); sendKeyCommand(positive?"KEY:RIGHT,0":"KEY:LEFT,0");break;
    }
  }

  // Position display update is handled by the main loop with rate limiting
  // Only mark that we need a position redraw
  needsAxisRedraw = true;
}

// ====== Serial command processor (host link) ======
void processSerialCommands(){
  while(Serial.available()){
    String c=Serial.readStringUntil('\n'); c.trim(); if(!c.length()) continue;

    // Auto-detect machine type based on incoming commands
    if(c.startsWith("ok") || c.startsWith("error:") || c.startsWith("ALARM:") || c.startsWith("$")){
      // GRBL-style responses - could be UGS, Carbide Motion, bCNC, etc.
      if(currentMachine == MACHINE_FIRECONTROL || currentMachine == MACHINE_CUTCONTROL || currentMachine == MACHINE_MANUAL){
        currentMachine = MACHINE_UGS; currentProtocol = PROTOCOL_GRBL;
        Serial.println("AUTO-DETECTED:UGS/GRBL");
        requestFullRedraw();
      }
    } else if(c.startsWith("Mach3") || c.startsWith("Mach4") || c.indexOf("Artsoft")>=0){
      // Mach series detection
      if(c.indexOf("Mach4")>=0){ currentMachine = MACHINE_MACH4; } else { currentMachine = MACHINE_MACH3; }
      currentProtocol = PROTOCOL_MACH;
      Serial.printf("AUTO-DETECTED:%s\n", getMachineName());
      requestFullRedraw();
    } else if(c.startsWith("UCCNC") || c.indexOf("cncdrive")>=0){
      currentMachine = MACHINE_UCCNC; currentProtocol = PROTOCOL_MACH;
      Serial.println("AUTO-DETECTED:UCCNC");
      requestFullRedraw();
    } else if(c.startsWith("LINUXCNC") || c.indexOf("emc")>=0 || c.indexOf("axis")>=0){
      currentMachine = MACHINE_LINUXCNC; currentProtocol = PROTOCOL_LINUXCNC;
      Serial.println("AUTO-DETECTED:LINUXCNC");
      requestFullRedraw();
    } else if(c.indexOf("Carbide")>=0 || c.indexOf("Shapeoko")>=0 || c.indexOf("Nomad")>=0){
      currentMachine = MACHINE_CARBIDE; currentProtocol = PROTOCOL_GRBL;
      Serial.println("AUTO-DETECTED:CARBIDE");
      requestFullRedraw();
    }

    if(c.startsWith("LCD:SOLID,")){
      int a=c.indexOf(',',10), b=c.indexOf(',',a+1);
      if(a>0&&b>0){ bgR=c.substring(10,a).toInt(); bgG=c.substring(a+1,b).toInt(); bgB=c.substring(b+1).toInt(); useRGBWheel=false;
        requestFullRedraw(); }

    } else if(c.startsWith("LCD:WHEEL,")){
      rgbHue=c.substring(10).toFloat(); useRGBWheel=true;
      requestFullRedraw();

    } else if(c.startsWith("MACHINE:")){
      String mt=c.substring(8); MachineType nm=currentMachine;
      if(mt=="FIRECONTROL") nm=MACHINE_FIRECONTROL;
      else if(mt=="CUTCONTROL") nm=MACHINE_CUTCONTROL;
      else if(mt=="MACH3") nm=MACHINE_MACH3;
      else if(mt=="MACH4") nm=MACHINE_MACH4;
      else if(mt=="LINUXCNC") nm=MACHINE_LINUXCNC;
      else if(mt=="UCCNC") nm=MACHINE_UCCNC;
      else if(mt=="CARBIDE") nm=MACHINE_CARBIDE;
      else if(mt=="UGS") nm=MACHINE_UGS;
      else if(mt=="OPENBUILDS") nm=MACHINE_UGS;
      else if(mt=="CNCJS") nm=MACHINE_UGS;
      else if(mt=="MANUAL") nm=MACHINE_MANUAL;
      if(nm!=currentMachine){ currentMachine=nm; requestFullRedraw(); Serial.print("MACHINE:"); Serial.println(getMachineName()); }

    } else if(c.startsWith("UNITS:")){
      String u=c.substring(6); bool ni=(u=="INCHES"||u=="IN");
      if(ni!=useImperialUnits){ useImperialUnits=ni; needsAxisRedraw=true; Serial.print("UNITS:"); Serial.println(useImperialUnits?"INCHES":"MM"); }

    } else if(c.startsWith("SOFTWARE:")){
      bool st = c.substring(9)=="CONNECTED";
      if(st!=softwareConnected){ softwareConnected=st; needsStatusRedraw=true; Serial.print("SOFTWARE:"); Serial.println(softwareConnected?"CONNECTED":"DISCONNECTED"); }

    } else if(c=="HOST:HELLO"){
      Serial.printf("FW:%s\n", FW_VERSION);
      Serial.println("PENDANT:HELLO");

    } else if(c=="ARM:ENABLE"){
      hostArmed=true; lastHostPing=millis(); Serial.println("ARM:ENABLED");

    } else if(c=="ARM:DISABLE"){
      hostArmed=false; Serial.println("ARM:DISABLED");

    } else if(c=="HOST:PING"){
      lastHostPing=millis(); Serial.println("HOST:PONG");

    } else if(c.startsWith("MOTION:STATE,")){
      String s=c.substring(13);
      MotionState nm=MS_IDLE; if(s=="RUN") nm=MS_RUN; else if(s=="HOLD") nm=MS_HOLD;
      if(nm!=hostMotionState){ hostMotionState=nm; Serial.printf("MOTION:%s\n", s.c_str()); }

    } else if(c.startsWith("COORD:ACTIVE,")){
      activeWCS = c.substring(13); activeWCS.trim(); if(activeWCS.length()==0) activeWCS="G54";
      needsStatusRedraw = true;

    } else if(c.startsWith("COORD:VIEW,")){
      String v=c.substring(11);
      if(v=="WCS"){ coordView=VIEW_WCS; viewLockedByHost=true; }
      else if(v=="MCS"){ coordView=VIEW_MCS; viewLockedByHost=true; }
      needsAxisRedraw = true; needsStatusRedraw = true;

    } else if(c.startsWith("OFFSET:")){
      // Format: OFFSET:G54,X, -100.000 ,Y, 0.000 ,Z, 0.000  (commas)
      int gEnd=c.indexOf(',',7); if(gEnd>0){
        String g=c.substring(7,gEnd); const XYZ* cur=getOffsetPtr(g); XYZ* tgt=(XYZ*)cur; if(tgt){
          // parse values
          String rest=c.substring(gEnd+1); rest.replace(" ","");
          // simple parse
          int xI=rest.indexOf("X,"); int yI=rest.indexOf(",Y,"); int zI=rest.indexOf(",Z,");
          if(xI>=0 && yI>0 && zI>0){
            float vx=rest.substring(xI+2, yI).toFloat();
            float vy=rest.substring(yI+3, zI).toFloat();
            float vz=rest.substring(zI+3).toFloat();
            tgt->x=vx; tgt->y=vy; tgt->z=vz;
          }
        }
      }

    } else if(c.startsWith("POS:WCS,")){
      // POS:WCS,X,123.456,Y,7.890,Z,-1.234
      String s=c.substring(8); s.replace(" ","");
      int xI=s.indexOf("X,"); int yI=s.indexOf(",Y,"); int zI=s.indexOf(",Z,");
      if(xI>=0 && yI>0 && zI>0){
        hostWCS.x = s.substring(xI+2, yI).toFloat();
        hostWCS.y = s.substring(yI+3, zI).toFloat();
        hostWCS.z = s.substring(zI+3).toFloat();
        tHostWCS = millis();
        needsAxisRedraw = true;  // Deferred update
      }

    } else if(c.startsWith("POS:MCS,")){
      // POS:MCS,X,223.456,Y,107.890,Z,-11.234
      String s=c.substring(8); s.replace(" ","");
      int xI=s.indexOf("X,"); int yI=s.indexOf(",Y,"); int zI=s.indexOf(",Z,");
      if(xI>=0 && yI>0 && zI>0){
        hostMCS.x = s.substring(xI+2, yI).toFloat();
        hostMCS.y = s.substring(yI+3, zI).toFloat();
        hostMCS.z = s.substring(zI+3).toFloat();
        tHostMCS = millis();
        needsAxisRedraw = true;  // Deferred update
      }

    } else if(c.startsWith("POS:RESET")){
      xPosition=yPosition=zPosition=0; needsAxisRedraw = true;

    } else if(c=="POS:GET"){
      Serial.printf("POS:X,%.4f\n", xPosition);
      Serial.printf("POS:Y,%.4f\n", yPosition);
      Serial.printf("POS:Z,%.4f\n", zPosition);

    } else if(c.startsWith("POS:SET,")){
      int a=c.indexOf(',',8), b=c.indexOf(',',a+1);
      if(a>0 && b>0){
        char ax=c.charAt(a+1);
        float val=c.substring(b+1).toFloat();
        if(ax=='X'){ xPosition=val; }
        else if(ax=='Y'){ yPosition=val; }
        else if(ax=='Z'){ zPosition=val; }
        needsAxisRedraw = true;
      }

    } else if(c.startsWith("ENCODER:SCALE,")){
      int sc=c.substring(14).toInt(); if(sc>=1&&sc<=10){ encoderScale=sc; scaledEncoderPosition=0; Serial.printf("ENCODER_SCALE_SET:%d\n",sc); }

    } else if(c=="MACHINE:ZERO"){
      xPosition=yPosition=zPosition=0; encoderPosition=0; scaledEncoderPosition=0; lastEncoderPos=0; needsAxisRedraw = true;

    } else if(c=="ENCODER:GET"){
      Serial.printf("ENCODER:SCALE,%d\n",encoderScale);
      Serial.printf("ENCODER:POS,%ld\n",encoderPosition);
      Serial.printf("ENCODER:SCALED,%ld\n",scaledEncoderPosition);
      Serial.printf("HOST:WCS_FRESH,%s\n", hostWCSFresh()?"TRUE":"FALSE");
      Serial.printf("HOST:MCS_FRESH,%s\n", hostMCSFresh()?"TRUE":"FALSE");

    } else if(c.startsWith("LCD:BRIGHTNESS,")){
      int b=c.substring(15).toInt(); b=constrain(b,0,255); Serial.printf("BRIGHTNESS:%d\n",b); // no hardware BL by default
    }
  }
}

// ====== Setup/Loop ======
void setup(){
  Serial.begin(115200); Serial.println();
  pinMode(PIN_ENCODER_A,INPUT_PULLUP); pinMode(PIN_ENCODER_B,INPUT_PULLUP); 
  pinMode(PIN_BUTTON_1,INPUT_PULLUP); pinMode(PIN_BUTTON_2,INPUT_PULLUP); pinMode(PIN_TOGGLE_SW,INPUT_PULLUP);
  pinMode(PIN_ESTOP,INPUT_PULLUP);  // E-stop NC contact (normally closed)
  pinMode(PIN_LCD_DC,OUTPUT); digitalWrite(PIN_LCD_DC,HIGH); // DC strap high before init
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), encoderISR, RISING);

  lcd.init(); lcd.setRotation(0); lcd.setColorDepth(16);

  drawBootScreen(); 
  doFullRedraw();  // Initial full display

  Serial.printf("FW:%s\n", FW_VERSION);
  Serial.print("MACHINE:"); Serial.println(getMachineName());
  Serial.println("AIONMECH Universal CNC Pendant starting...");
  Serial.println("PENDANT:READY");
  Serial.println("FIRMWARE:UNIVERSAL_FULL_V2.0");
}

void loop(){
  handleEstop();         // PRIORITY: E-stop monitoring first
  handleBothButtons();   // Check for help screen toggle (both buttons held)
  handleButton1();
  handleButton2(); 
  handleToggleSwitch();
  handlePowerManagement();
  
  if(currentPowerState == POWER_ACTIVE && currentEstopState == ESTOP_RELEASED){
    handleEncoder();
    processSerialCommands();

    // Skip normal UI updates if in help view
    if(currentViewMode == VIEW_HELP) {
      delay(10);
      return;
    }

    unsigned long now = millis();
    
    // Full redraw if flagged (theme change, machine change, etc.)
    // Process this FIRST so other flags get cleared properly
    if(needsFullRedraw){
      doFullRedraw();
      needsAxisRedraw = false;
      needsStatusRedraw = false;
    }
    
    // Rate-limited UI updates to prevent flicker
    // Position display: update at most every 100ms
    if(needsAxisRedraw && (now - lastPosRedraw > 100)){
      UI_Begin(); 
      drawPositionDisplay();  // Only update position numbers
      UI_End();
      lastPosRedraw = now;
      needsAxisRedraw = false;
    }
    
    // Status bar: update if flagged or every 1 second for connection pulse
    static unsigned long lastUI = 0;
    if(needsStatusRedraw || (now - lastUI > 1000)){ 
      UI_Begin(); 
      drawConnectionStatus(); 
      UI_End(); 
      lastUI = now;
      needsStatusRedraw = false;
    }
  }

  delay(2);
}