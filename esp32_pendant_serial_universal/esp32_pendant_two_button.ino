/*
  UNIVERSAL AIONMECH CNC PENDANT – ESP32 WROOM TWO BUTTON VERSION
  Display: GC9A1 240x240, LovyanGFX
  Serial: 115200 (newline '\n')
  
  TWO BUTTON CONTROLS:
  Button 1 (short press): Change Axis (X -> Y -> Z)
  Button 1 (long press 2s): Change Jog Distance
  Button 2 (short press): Change Speed/Feed Rate
  Button 2 (long press 2s): Toggle WCS/MCS View
  Toggle Switch: Power/Sleep management
  
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
#define PIN_BUTTON_1    25  // Axis/Distance
#define PIN_BUTTON_2    4   // Speed/View - CHANGED from 26 to avoid LCD CS conflict
#define PIN_TOGGLE_SW   27  // Power/Sleep - CHANGED from 15 to avoid boot issues
#define PIN_ESTOP       14  // Emergency Stop (19mm mushroom button, NC contact)
#define PIN_LCD_SCLK    18  // FIXED: Was 19
#define PIN_LCD_MOSI    23  // FIXED: Was 18
#define PIN_LCD_CS      15  // FIXED: Was 26 (conflicted with button)
#define PIN_LCD_DC       2
#define PIN_LCD_RST     21  // FIXED: Was 12

// ================ Enums =================
enum MachineType { MACHINE_FIRECONTROL=0, MACHINE_CUTCONTROL=1, MACHINE_MANUAL=2, MACHINE_MACH3=3, MACHINE_MACH4=4, MACHINE_LINUXCNC=5, MACHINE_UCCNC=6, MACHINE_CARBIDE=7, MACHINE_UGS=8 };
enum AxisType    { AXIS_Z=0, AXIS_Y=1, AXIS_X=2 };
enum SpeedType   { SPEED_SLOW=0, SPEED_MEDIUM=1, SPEED_FAST=2 };
enum JogDistance { JOG_FINE=0, JOG_SMALL=1, JOG_MEDIUM=2, JOG_LARGE=3 };
enum FeedRate    { FEED_25=0, FEED_50=1, FEED_75=2, FEED_100=3 };
enum CoordView   { VIEW_WCS=0, VIEW_MCS=1 };
enum MotionState { MS_IDLE=0, MS_RUN=1, MS_HOLD=2 };
enum CommProtocol { PROTOCOL_LANGMUIR=0, PROTOCOL_GRBL=1, PROTOCOL_MACH=2, PROTOCOL_LINUXCNC=3 };
enum PowerState  { POWER_ACTIVE=0, POWER_COUNTDOWN=1, POWER_SLEEP=2 };
enum EstopState  { ESTOP_RELEASED=0, ESTOP_PRESSED=1, ESTOP_FAULT=2 };

// ================ Globals ================
#define FW_VERSION "ULP-ESP32 v2.0.0-TWO-BUTTON"

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
const unsigned long HOST_TIMEOUT_MS=3500;

// Motion gate from host
MotionState hostMotionState = MS_IDLE;

// Local fallback position accumulators
float xPosition=0, yPosition=0, zPosition=0;

// Host live positions + timestamps (WCS and MCS)
struct XYZ { float x=0, y=0, z=0; };
XYZ hostWCS, hostMCS;
unsigned long tHostWCS=0, tHostMCS=0;
const unsigned long HOST_POS_STALE_MS = 1200;

// Active work offset
XYZ offG54, offG55, offG56, offG57, offG58, offG59;
const XYZ* getOffsetPtr(const String& g){
  if(g=="G54") return &offG54; if(g=="G55") return &offG55; if(g=="G56") return &offG56;
  if(g=="G57") return &offG57; if(g=="G58") return &offG58; if(g=="G59") return &offG59; return nullptr;
}

// WCS selection + view mode
String activeWCS = "G54";
CoordView coordView = VIEW_WCS;
bool viewLockedByHost = false;

// TWO BUTTON HANDLING
bool button1Pressed = false, button1Processed = false;
bool button2Pressed = false, button2Processed = false;
unsigned long btn1PressTime = 0, btn2PressTime = 0;
const unsigned long LONG_PRESS_THRESHOLD = 2000;  // 2 seconds for long press
const unsigned long DEBOUNCE_DELAY = 50;

// Toggle switch and power management
bool toggleSwitchState = true, lastToggleState = true;
PowerState currentPowerState = POWER_ACTIVE;
unsigned long lastActivityTime = 0;
unsigned long sleepCountdownStart = 0;
int sleepCountdown = 0;
const unsigned long SLEEP_COUNTDOWN_TIME = 5000;

// E-stop safety system
EstopState currentEstopState = ESTOP_RELEASED;
bool estopPressed = false, lastEstopState = false;
unsigned long estopPressTime = 0;
const unsigned long ESTOP_DEBOUNCE = 100;
bool systemLocked = false;

uint8_t bgR=0,bgG=0,bgB=0;
bool useRGBWheel=false;
float rgbHue=0.0f;

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

// Batch + clip
inline void UI_Begin(){ lcd.startWrite(); }
inline void UI_End(){ lcd.endWrite(); }
inline void SetClip(int x,int y,int w,int h){ lcd.setClipRect(x,y,w,h); }
inline void ClearClip(){ lcd.clearClipRect(); }

// Dirty rects
const int AX_CARD_X=40, AX_CARD_Y=60, AX_CARD_W=160, AX_CARD_H=120;
const int SPEED_X=85, SPEED_Y=182, SPEED_W=70, SPEED_H=32;
const int STATUS_X=130, STATUS_Y=14, STATUS_W=94, STATUS_H=30;
const int UNITS_X=12, UNITS_Y=12, UNITS_W=84, UNITS_H=40;

// Utility functions
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
    case MACHINE_MACH3      : return RGB565(255,128,0);
    case MACHINE_MACH4      : return RGB565(0,160,255);
    case MACHINE_LINUXCNC   : return RGB565(255,80,80);
    case MACHINE_UCCNC      : return RGB565(80,255,80);
    case MACHINE_CARBIDE    : return RGB565(255,200,0);
    case MACHINE_UGS        : return RGB565(160,80,255);
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

void sendUniversalEstopCommand(bool estopActive){
  if(estopActive){
    systemLocked = true;
    switch(currentProtocol){
      case PROTOCOL_LANGMUIR:
        Serial.println("ESTOP:ACTIVE");
        Serial.println("PAUSE");
        break;
      case PROTOCOL_GRBL:
        Serial.write(0x85);
        Serial.println("!");
        Serial.println("$X");
        break;
      case PROTOCOL_MACH:
        Serial.println("G80");
        Serial.println("M0");
        break;
      case PROTOCOL_LINUXCNC:
        Serial.println("M1");
        Serial.println("G80");
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
        Serial.println("$X");
        Serial.println("~");
        break;
      case PROTOCOL_MACH:
        break;
      case PROTOCOL_LINUXCNC:
        break;
    }
    Serial.println("ESTOP:RELEASED");
  }
}

void sendUniversalJogCommand(char axis, bool positive, float distance){
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
      Serial.printf("$J=G91G1%c%.4fF%d\n", axis, positive?distance:-distance, (currentSpeed==SPEED_SLOW)?500:(currentSpeed==SPEED_FAST)?2000:1000);
      break;
    case PROTOCOL_MACH:
      Serial.printf("G91G1%c%.4fF%d\n", axis, positive?distance:-distance, (currentSpeed==SPEED_SLOW)?100:(currentSpeed==SPEED_FAST)?1000:500);
      break;
    case PROTOCOL_LINUXCNC:
      Serial.printf("G91 G1 %c%.4f F%d\n", axis, positive?distance:-distance, (currentSpeed==SPEED_SLOW)?100:(currentSpeed==SPEED_FAST)?1000:500);
      break;
  }
}

// Drawing functions
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

void drawStatusStaticRings(){ 
  UI_Begin(); 
  lcd.drawCircle(120,120,118,THEME_RING_1); 
  lcd.drawCircle(120,120,117,THEME_RING_2); 
  lcd.drawCircle(120,120,116,THEME_RING_3); 
  UI_End(); 
}

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

bool hostWCSFresh(){ return (millis()-tHostWCS) <= HOST_POS_STALE_MS; }
bool hostMCSFresh(){ return (millis()-tHostMCS) <= HOST_POS_STALE_MS; }

void drawAxisDisplay(){
  uint16_t accent=getMachineAccentColor();
  SetClip(AX_CARD_X,AX_CARD_Y,AX_CARD_W,AX_CARD_H);
  lcd.fillRect(AX_CARD_X,AX_CARD_Y,AX_CARD_W,AX_CARD_H, THEME_BG_DARK);

  lcd.fillCircle(120,120,60,THEME_BG_DARK);
  drawGlassCirclePanel(120,120,56,blend565(accent,THEME_CARD,100),THEME_BG_DARK);

  const char* axisText; const char* axisDesc; uint16_t axisColor;
  switch(currentAxis){ 
    case AXIS_Z: axisText="Z-AXIS"; axisDesc="PG UP/DN"; axisColor=0x001F; break;
    case AXIS_Y: axisText="Y-AXIS"; axisDesc="UP/DOWN";  axisColor=0x07E0; break;
    default    : axisText="X-AXIS"; axisDesc="LEFT/RIGHT"; axisColor=0xF800; break; 
  }

  lcd.setTextDatum(middle_center);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(0x0000,THEME_BG_DARK); lcd.drawString(axisText,120,92);
  lcd.setTextColor(axisColor,THEME_BG_DARK); lcd.drawString(axisText,120,90);

  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(0x0000,THEME_BG_DARK); lcd.drawString(axisDesc,120,112);
  lcd.setTextColor(THEME_TEXT_MAIN,THEME_BG_DARK); lcd.drawString(axisDesc,120,110);

  String distanceText=getDistanceText();
  lcd.setTextColor(0x0000,THEME_BG_DARK); lcd.drawString(distanceText.c_str(),120,132);
  lcd.setTextColor(blend565(accent, RGB565(255,255,0),150),THEME_BG_DARK); lcd.drawString(distanceText.c_str(),120,130);

  if(currentMachine==MACHINE_CUTCONTROL){
    const char* feed="50%";
    switch(currentFeedRate){case FEED_25:feed="25%";break;case FEED_50:feed="50%";break;case FEED_75:feed="75%";break;case FEED_100:feed="100%";break;}
    lcd.setTextColor(0x0000,THEME_BG_DARK); lcd.drawString(feed,120,148);
    lcd.setTextColor(blend565(accent,RGB565(0,255,255),150),THEME_BG_DARK); lcd.drawString(feed,120,146);
  }

  bool showWCS = (coordView==VIEW_WCS);
  bool fresh   = showWCS ? hostWCSFresh() : hostMCSFresh();
  float sx=0, sy=0, sz=0;
  if(fresh){
    if(showWCS){ sx=hostWCS.x; sy=hostWCS.y; sz=hostWCS.z; } else { sx=hostMCS.x; sy=hostMCS.y; sz=hostMCS.z; }
  } else {
    sx = xPosition; sy = yPosition; sz = zPosition;
  }

  char buf[48];
  lcd.setFont(&fonts::Font0);
  lcd.setTextColor(THEME_TEXT_MAIN, THEME_BG_DARK);
  snprintf(buf,sizeof(buf),"X:% .3f", sx); lcd.drawString(buf,120,156);
  snprintf(buf,sizeof(buf),"Y:% .3f", sy); lcd.drawString(buf,120,166);
  snprintf(buf,sizeof(buf),"Z:% .3f", sz); lcd.drawString(buf,120,176);

  uint16_t dot = fresh ? RGB565(0,220,120) : RGB565(255,160,0);
  lcd.fillCircle(196,176,3,dot);

  String mtxt=getMachineDisplayName();
  drawGlassPill(70, 182, 100, 18, accent, THEME_BG_DARK, accent);
  lcd.setTextColor(accent, THEME_BG_DARK); lcd.setTextDatum(middle_center); lcd.drawString(mtxt.c_str(),120,191);

  ClearClip();
}

void drawSpeedIndicator(){
  uint16_t accent=getMachineAccentColor();
  SetClip(SPEED_X,SPEED_Y,SPEED_W,SPEED_H);
  lcd.fillRect(SPEED_X,SPEED_Y,SPEED_W,SPEED_H, THEME_BG_DARK);
  drawGlassPill(SPEED_X,SPEED_Y,SPEED_W,SPEED_H, THEME_RING_2, THEME_BG_DARK, accent);
  const char* txt="MED"; uint16_t color=0xFFE0;
  if(currentSpeed==SPEED_SLOW){txt="SLOW"; color=0x07E0;}
  else if(currentSpeed==SPEED_FAST){txt="FAST"; color=0xF800;}
  lcd.setTextDatum(middle_center); lcd.setFont(&fonts::Font2);
  lcd.setTextColor(0x0000,THEME_BG_DARK); lcd.drawString(txt,120,194);
  lcd.setTextColor(color,THEME_BG_DARK);  lcd.drawString(txt,120,192);
  ClearClip();
}

void drawConnectionStatus(){
  static unsigned long lastPulse=0; static bool pulse=false;
  if(millis()-lastPulse>900){ pulse=!pulse; lastPulse=millis(); }
  uint16_t accent=getMachineAccentColor();

  uint16_t statusColor; const char* statusText;
  if(softwareConnected){ statusColor=pulse?accent:blend565(accent,THEME_BG_DARK,180); statusText=getMachineDisplayName(); }
  else { statusColor=pulse?RGB565(255,255,0):RGB565(200,100,0); statusText="USB"; }

  SetClip(STATUS_X,STATUS_Y,STATUS_W,STATUS_H);
  lcd.fillRect(STATUS_X,STATUS_Y,STATUS_W,STATUS_H, THEME_BG_DARK);
  drawGlassPill(140, 18, 78, 24, THEME_RING_2, THEME_BG_DARK, accent);
  lcd.fillCircle(196, 30, 6, statusColor);
  lcd.drawCircle(196, 30, 7, THEME_RING_3);
  lcd.setTextDatum(middle_right); lcd.setFont(&fonts::Font2);
  lcd.setTextColor(0x0000, THEME_BG_DARK); lcd.drawString(statusText,186,30);
  lcd.setTextColor(THEME_TEXT_MAIN, THEME_BG_DARK); lcd.drawString(statusText,184,30);
  ClearClip();

  SetClip(UNITS_X,UNITS_Y,UNITS_W,UNITS_H);
  lcd.fillRect(UNITS_X,UNITS_Y,UNITS_W,UNITS_H, THEME_BG_DARK);
  String units = useImperialUnits ? "IN" : "MM";
  uint16_t unitsColor = useImperialUnits ? RGB565(255,0,150) : accent;
  lcd.setTextDatum(middle_left); lcd.setFont(&fonts::Font4);
  lcd.setTextColor(0x0000,THEME_BG_DARK); lcd.drawString(units.c_str(),30,30);
  lcd.setTextColor(unitsColor,THEME_BG_DARK); lcd.drawString(units.c_str(),28,30);

  char badge[16];
  snprintf(badge,sizeof(badge), "%s %c", activeWCS.c_str(), (coordView==VIEW_WCS?'W':'M'));
  drawGlassPill(72, 18, 64, 22, THEME_RING_2, THEME_BG_DARK, accent);
  lcd.setTextDatum(middle_center); lcd.setFont(&fonts::Font0);
  lcd.setTextColor(THEME_TEXT_MAIN, THEME_BG_DARK); lcd.drawString(badge, 104, 29);
  ClearClip();
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
  lcd.setFont(&fonts::Font8); lcd.drawString("AIONMECH",120,76);
  lcd.setTextColor(THEME_TEXT_MAIN,0x0000);
  lcd.setFont(&fonts::Font4); lcd.drawString("PENDANT",120,100);
  lcd.setTextColor(getMachineAccentColor(),0x0000);
  lcd.setFont(&fonts::Font2); lcd.drawString("TWO BUTTON",120,120);
  lcd.setTextColor(THEME_TEXT_SUB,0x0000);
  lcd.setFont(&fonts::Font2); lcd.drawString(getMachineName(),120,140);

  lcd.setFont(&fonts::Font0); lcd.setTextColor(0xFFE0,0x0000);
  lcd.drawString("ENC: Jog | BTN1: Axis/Dist BTN2: Speed/View",120,170);
  lcd.drawString("Hold 2s: Long functions",120,184);
  UI_End();
}

void drawHelpScreen(){
  UI_Begin();
  lcd.setClipRect(10,10,220,220);
  lcd.fillRect(10,10,220,220, THEME_BG_DARK);

  lcd.setTextDatum(middle_center);
  lcd.setTextColor(getMachineAccentColor(), THEME_BG_DARK);
  lcd.setFont(&fonts::Font4); lcd.drawString("TWO BUTTON",120,28);

  lcd.setTextDatum(middle_left);
  lcd.setFont(&fonts::Font0);
  lcd.setTextColor(getMachineAccentColor(), THEME_BG_DARK);
  lcd.drawString("ENCODER:",18,54);
  lcd.setTextColor(THEME_TEXT_MAIN, THEME_BG_DARK);
  lcd.drawString("Turn = Jog selected axis",18,68);

  lcd.setTextColor(getMachineAccentColor(), THEME_BG_DARK);
  lcd.drawString("BUTTON 1:",18,90);
  lcd.setTextColor(THEME_TEXT_MAIN, THEME_BG_DARK);
  lcd.drawString("Short = Change Axis",18,104);
  lcd.drawString("Long = Change Distance",18,118);

  lcd.setTextColor(getMachineAccentColor(), THEME_BG_DARK);
  lcd.drawString("BUTTON 2:",18,140);
  lcd.setTextColor(THEME_TEXT_MAIN, THEME_BG_DARK);
  lcd.drawString("Short = Speed/Feed",18,154);
  lcd.drawString("Long = Toggle WCS/MCS",18,168);

  lcd.setTextColor(getMachineAccentColor(), THEME_BG_DARK);
  lcd.drawString("TOGGLE SW:",18,188);
  lcd.setTextColor(THEME_TEXT_MAIN, THEME_BG_DARK);
  lcd.drawString("Power/Sleep control",18,202);

  lcd.clearClipRect();
  UI_End();

  delay(1500);

  UI_Begin(); drawAxisDisplay(); drawSpeedIndicator(); drawConnectionStatus(); UI_End();
}

// ====== Encoder ISR ======
void IRAM_ATTR encoderISR(){
  static uint32_t last_us=0;
  uint32_t now_us=(uint32_t)esp_timer_get_time();
  if(now_us-last_us>5000){ encAedges++; last_us=now_us; }
}

void updatePositionTracking(float movement){
  switch(currentAxis){ 
    case AXIS_X:xPosition+=movement; break; 
    case AXIS_Y:yPosition+=movement; break; 
    case AXIS_Z:zPosition+=movement; break; 
  }
}

// ====== TWO BUTTON HANDLERS ======
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
          UI_Begin(); drawAxisDisplay(); UI_End();
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
            UI_Begin(); drawBackground(); drawStatusStaticRings(); drawAxisDisplay(); drawSpeedIndicator(); drawConnectionStatus(); UI_End();
          }
        } else {
          // Short press: Change speed/feedrate
          if(currentMachine == MACHINE_CUTCONTROL){
            currentFeedRate = (FeedRate)((currentFeedRate + 1) % 4);
            const char* ftxt = "50%";
            switch(currentFeedRate){
              case FEED_25: ftxt = "25%"; if(hostArmed && (millis() - lastHostPing) <= HOST_TIMEOUT_MS && hostMotionState == MS_IDLE) Serial.println("KEY:F1,1"); break;
              case FEED_50: ftxt = "50%"; if(hostArmed && (millis() - lastHostPing) <= HOST_TIMEOUT_MS && hostMotionState == MS_IDLE) Serial.println("KEY:F2,1"); break;
              case FEED_75: ftxt = "75%"; if(hostArmed && (millis() - lastHostPing) <= HOST_TIMEOUT_MS && hostMotionState == MS_IDLE) Serial.println("KEY:F3,1"); break;
              case FEED_100: ftxt = "100%"; if(hostArmed && (millis() - lastHostPing) <= HOST_TIMEOUT_MS && hostMotionState == MS_IDLE) Serial.println("KEY:F4,1"); break;
            }
            Serial.print("FEEDRATE:"); Serial.println(ftxt);
            UI_Begin(); drawAxisDisplay(); UI_End();
          } else {
            currentSpeed = (SpeedType)((currentSpeed + 1) % 3);
            Serial.print("SPEED:"); Serial.println(currentSpeed == SPEED_SLOW ? "SLOW" : currentSpeed == SPEED_MEDIUM ? "MEDIUM" : "FAST");
            UI_Begin(); drawSpeedIndicator(); UI_End();
          }
        }
        button2Processed = true;
      }
    }
    last = down;
  }
}

void handleToggleSwitch(){
  bool raw = digitalRead(PIN_TOGGLE_SW);
  bool current = !raw;
  
  if(current != lastToggleState){
    lastToggleState = current;
    lastActivityTime = millis();
    
    if(current){
      if(currentPowerState != POWER_ACTIVE){
        currentPowerState = POWER_ACTIVE;
        sleepCountdown = 0;
        Serial.println("POWER:ACTIVE");
        UI_Begin(); drawBackground(); drawStatusStaticRings(); drawAxisDisplay(); drawSpeedIndicator(); drawConnectionStatus(); UI_End();
      }
    } else {
      if(currentPowerState == POWER_ACTIVE){
        currentPowerState = POWER_COUNTDOWN;
        sleepCountdownStart = millis();
        sleepCountdown = 5;
        Serial.println("POWER:COUNTDOWN");
      }
    }
  }
}

void handlePowerManagement(){
  unsigned long now = millis();
  
  if(currentPowerState == POWER_COUNTDOWN){
    unsigned long elapsed = now - sleepCountdownStart;
    int newCountdown = 5 - (elapsed / 1000);
    
    if(newCountdown != sleepCountdown && newCountdown >= 0){
      sleepCountdown = newCountdown;
      UI_Begin();
      lcd.fillScreen(RGB565(0,0,0));
      lcd.setTextColor(RGB565(255,255,255));
      lcd.setTextDatum(middle_center);
      lcd.setFont(&fonts::Font4);
      lcd.drawString("SLEEP IN", 120, 80);
      lcd.setFont(&fonts::Font8);
      lcd.drawString(String(sleepCountdown), 120, 120);
      UI_End();
    }
    
    if(elapsed >= SLEEP_COUNTDOWN_TIME){
      currentPowerState = POWER_SLEEP;
      Serial.println("POWER:SLEEP");
      lcd.fillScreen(RGB565(0,0,0));
    }
  }
}

void handleEstop(){
  bool raw = digitalRead(PIN_ESTOP);
  bool pressed = !raw;
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
        currentEstopState = ESTOP_PRESSED;
        sendUniversalEstopCommand(true);
        Serial.println("ESTOP:EMERGENCY_STOP_ACTIVATED");
        
        UI_Begin();
        lcd.fillScreen(RGB565(255,0,0));
        lcd.setTextColor(RGB565(255,255,255));
        lcd.setTextDatum(middle_center);
        lcd.setFont(&fonts::Font4);
        lcd.drawString("E-STOP", 120, 80);
        lcd.setFont(&fonts::Font2);
        lcd.drawString("EMERGENCY", 120, 120);
        lcd.drawString("STOP ACTIVE", 120, 140);
        UI_End();
      } else {
        currentEstopState = ESTOP_RELEASED;
        sendUniversalEstopCommand(false);
        Serial.println("ESTOP:EMERGENCY_STOP_RELEASED");
        
        UI_Begin(); 
        drawBackground(); 
        drawStatusStaticRings(); 
        drawAxisDisplay(); 
        drawSpeedIndicator(); 
        drawConnectionStatus(); 
        UI_End();
      }
    }
    last = pressed;
  }
}

void sendKeyCommand(const char* command){
  Serial.println(command);
}

void handleEncoder(){
  long pulses = encAedges; 
  if(pulses == 0) return; 
  encAedges = 0;
  
  int dir = digitalRead(PIN_ENCODER_B) ? +1 : -1;
  encoderPosition += dir * pulses;
  
  if(encoderPosition > 1000000L) encoderPosition = 1000000L;
  if(encoderPosition < -1000000L) encoderPosition = -1000000L;

  long currentPos = encoderPosition;
  int rawMovement = currentPos - lastEncoderPos;
  if(rawMovement == 0) return;
  lastEncoderPos = currentPos;

  scaledEncoderPosition += rawMovement;
  int scaledMovement = scaledEncoderPosition / encoderScale;
  if(scaledMovement == 0) return;
  scaledEncoderPosition = scaledEncoderPosition % encoderScale;

  if(!(hostArmed && (millis() - lastHostPing) <= HOST_TIMEOUT_MS && hostMotionState == MS_IDLE)){
    scaledEncoderPosition = 0; 
    return;
  }

  bool positive = scaledMovement > 0;
  float jogDistance = getJogDistanceValue();
  float actual = (positive ? jogDistance : -jogDistance) * abs(scaledMovement);

  updatePositionTracking(actual);

  char axisChar = (currentAxis == AXIS_X) ? 'X' : (currentAxis == AXIS_Y) ? 'Y' : 'Z';
  
  sendUniversalJogCommand(axisChar, positive, fabs(actual));

  if(currentProtocol == PROTOCOL_LANGMUIR){
    int hold = (currentJogDistance == JOG_FINE) ? 12 : (currentJogDistance == JOG_SMALL) ? 18 : (currentJogDistance == JOG_MEDIUM) ? 28 : 40;
    switch(currentAxis){
      case AXIS_Z: sendKeyCommand(positive ? "KEY:PGUP,1" : "KEY:PGDN,1"); delay(hold); sendKeyCommand(positive ? "KEY:PGUP,0" : "KEY:PGDN,0"); break;
      case AXIS_Y: sendKeyCommand(positive ? "KEY:UP,1" : "KEY:DOWN,1");   delay(hold); sendKeyCommand(positive ? "KEY:UP,0" : "KEY:DOWN,0");   break;
      case AXIS_X: sendKeyCommand(positive ? "KEY:RIGHT,1" : "KEY:LEFT,1"); delay(hold); sendKeyCommand(positive ? "KEY:RIGHT,0" : "KEY:LEFT,0"); break;
    }
  }

  UI_Begin(); drawAxisDisplay(); drawSpeedIndicator(); UI_End();
}

// Serial command processor (same as original)
void processSerialCommands(){
  while(Serial.available()){
    String c=Serial.readStringUntil('\n'); c.trim(); if(!c.length()) continue;

    if(c.startsWith("ok") || c.startsWith("error:") || c.startsWith("ALARM:") || c.startsWith("$")){
      if(currentMachine == MACHINE_FIRECONTROL || currentMachine == MACHINE_CUTCONTROL || currentMachine == MACHINE_MANUAL){
        currentMachine = MACHINE_UGS; currentProtocol = PROTOCOL_GRBL;
        Serial.println("AUTO-DETECTED:UGS/GRBL");
        UI_Begin(); drawBackground(); drawStatusStaticRings(); drawAxisDisplay(); drawSpeedIndicator(); drawConnectionStatus(); UI_End();
      }
    }

    if(c.startsWith("LCD:SOLID,")){
      int a=c.indexOf(',',10), b=c.indexOf(',',a+1);
      if(a>0&&b>0){ bgR=c.substring(10,a).toInt(); bgG=c.substring(a+1,b).toInt(); bgB=c.substring(b+1).toInt(); useRGBWheel=false;
        UI_Begin(); drawBackground(); drawStatusStaticRings(); drawAxisDisplay(); drawSpeedIndicator(); drawConnectionStatus(); UI_End(); }

    } else if(c.startsWith("LCD:WHEEL,")){
      rgbHue=c.substring(10).toFloat(); useRGBWheel=true;
      UI_Begin(); drawBackground(); drawStatusStaticRings(); drawAxisDisplay(); drawSpeedIndicator(); drawConnectionStatus(); UI_End();

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
      else if(mt=="OPENBUILDS") nm=MACHINE_OPENBUILDS;
      else if(mt=="CNCJS") nm=MACHINE_CNCJS;
      else if(mt=="MANUAL") nm=MACHINE_MANUAL;
      if(nm!=currentMachine){ currentMachine=nm; UI_Begin(); drawBackground(); drawStatusStaticRings(); drawAxisDisplay(); drawSpeedIndicator(); drawConnectionStatus(); UI_End(); Serial.print("MACHINE:"); Serial.println(getMachineName()); }

    } else if(c.startsWith("UNITS:")){
      String u=c.substring(6); bool ni=(u=="INCHES"||u=="IN");
      if(ni!=useImperialUnits){ useImperialUnits=ni; UI_Begin(); drawAxisDisplay(); drawConnectionStatus(); UI_End(); Serial.print("UNITS:"); Serial.println(useImperialUnits?"INCHES":"MM"); }

    } else if(c.startsWith("SOFTWARE:")){
      bool st = c.substring(9)=="CONNECTED";
      if(st!=softwareConnected){ softwareConnected=st; UI_Begin(); drawConnectionStatus(); UI_End(); Serial.print("SOFTWARE:"); Serial.println(softwareConnected?"CONNECTED":"DISCONNECTED"); }

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
      UI_Begin(); drawConnectionStatus(); UI_End();

    } else if(c.startsWith("COORD:VIEW,")){
      String v=c.substring(11);
      if(v=="WCS"){ coordView=VIEW_WCS; viewLockedByHost=true; }
      else if(v=="MCS"){ coordView=VIEW_MCS; viewLockedByHost=true; }
      UI_Begin(); drawAxisDisplay(); drawConnectionStatus(); UI_End();

    } else if(c.startsWith("POS:WCS,")){
      String s=c.substring(8); s.replace(" ","");
      int xI=s.indexOf("X,"); int yI=s.indexOf(",Y,"); int zI=s.indexOf(",Z,");
      if(xI>=0 && yI>0 && zI>0){
        hostWCS.x = s.substring(xI+2, yI).toFloat();
        hostWCS.y = s.substring(yI+3, zI).toFloat();
        hostWCS.z = s.substring(zI+3).toFloat();
        tHostWCS = millis();
        UI_Begin(); drawAxisDisplay(); UI_End();
      }

    } else if(c.startsWith("POS:MCS,")){
      String s=c.substring(8); s.replace(" ","");
      int xI=s.indexOf("X,"); int yI=s.indexOf(",Y,"); int zI=s.indexOf(",Z,");
      if(xI>=0 && yI>0 && zI>0){
        hostMCS.x = s.substring(xI+2, yI).toFloat();
        hostMCS.y = s.substring(yI+3, zI).toFloat();
        hostMCS.z = s.substring(zI+3).toFloat();
        tHostMCS = millis();
        UI_Begin(); drawAxisDisplay(); UI_End();
      }

    } else if(c.startsWith("POS:RESET")){
      xPosition=yPosition=zPosition=0; UI_Begin(); drawAxisDisplay(); UI_End();

    } else if(c.startsWith("ENCODER:SCALE,")){
      int sc=c.substring(14).toInt(); if(sc>=1&&sc<=10){ encoderScale=sc; scaledEncoderPosition=0; Serial.printf("ENCODER_SCALE_SET:%d\n",sc); }

    } else if(c=="MACHINE:ZERO"){
      xPosition=yPosition=zPosition=0; encoderPosition=0; scaledEncoderPosition=0; lastEncoderPos=0; UI_Begin(); drawAxisDisplay(); UI_End();
    }
  }
}

void setup(){
  Serial.begin(115200); Serial.println();
  pinMode(PIN_ENCODER_A,INPUT_PULLUP); pinMode(PIN_ENCODER_B,INPUT_PULLUP); 
  pinMode(PIN_BUTTON_1,INPUT_PULLUP); pinMode(PIN_BUTTON_2,INPUT_PULLUP); pinMode(PIN_TOGGLE_SW,INPUT_PULLUP);
  pinMode(PIN_ESTOP,INPUT_PULLUP);
  pinMode(PIN_LCD_DC,OUTPUT); digitalWrite(PIN_LCD_DC,HIGH);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), encoderISR, RISING);

  lcd.init(); lcd.setRotation(0); lcd.setColorDepth(16);

  drawBootScreen(); 
  UI_Begin(); drawBackground(); drawStatusStaticRings(); drawAxisDisplay(); drawSpeedIndicator(); drawConnectionStatus(); UI_End();

  Serial.printf("FW:%s\n", FW_VERSION);
  Serial.print("MACHINE:"); Serial.println(getMachineName());
  Serial.println("AIONMECH Two Button CNC Pendant starting...");
  Serial.println("PENDANT:READY");
  Serial.println("FIRMWARE:TWO_BUTTON_V2.0");
}

void loop(){
  handleEstop();
  handleButton1();
  handleButton2(); 
  handleToggleSwitch();
  handlePowerManagement();
  
  if(currentPowerState == POWER_ACTIVE && currentEstopState == ESTOP_RELEASED){
    handleEncoder();
    processSerialCommands();

    static unsigned long lastUI=0;
    if(millis()-lastUI>1000){ UI_Begin(); drawConnectionStatus(); UI_End(); lastUI=millis(); }
  }

  delay(2);
}