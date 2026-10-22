#include "config.h"

// Hardware Instance Definitions
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite canvas = TFT_eSprite(&tft);
BluetoothSerial SerialBT;
ELM327 myELM;
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);

// State Flags
volatile bool isConnected = false;
volatile bool btAttempting = false;
volatile bool btStackFailed = false;
unsigned long lastBusQuery = 0;
unsigned long lastTouchDebounce = 0;
unsigned long lastBtRetry = 0;
int currentPage = 0;

// Metric Data Handlers
String valTCase = "---";
String valBoost = "---";
String valFICM = "---";
String valECT = "---";
String valTFT = "---";
String valVolts = "---";
String valRPM = "---";
String valSpeed = "---";
bool hasDTCWarning = false;

void loadDemoValues() {
    valTCase  = "4H";
    valBoost  = "12.5 PSI";
    valFICM   = "48.0V";
    valECT    = "195 F";
    valTFT    = "162 F";
    valVolts  = "13.8V";
    valRPM    = "1850";
    valSpeed  = "62 MPH";
    hasDTCWarning = false;
}

void clearLiveValues() {
    valTCase = valBoost = valFICM = valECT = "---";
    valTFT = valVolts = valRPM = valSpeed = "---";
    hasDTCWarning = false;
}