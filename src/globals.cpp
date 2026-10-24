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
String valWheelLF = "---";
String valWheelRF = "---";
String valWheelRear = "---";
bool hasDTCWarning = false;
String dtcCodes[MAX_DTCS];
int dtcTotal = -1;
int absDtcTotal = -1;

void loadDemoValues() {
    valTCase  = "4H";
    valBoost  = "12.5";
    valFICM   = "48.0";
    valECT    = "195";
    valTFT    = "162";
    valVolts  = "13.8";
    valRPM    = "1850";
    valSpeed  = "62";
    valWheelLF = valWheelRF = valWheelRear = "62";
    absDtcTotal = 1;   // shows the ABS warning card so it can be checked on the bench
    hasDTCWarning = false;
    dtcCodes[0] = "P0470";
    dtcCodes[1] = "P2291";
    dtcTotal  = 2;
}

void clearLiveValues() {
    valTCase = valBoost = valFICM = valECT = "---";
    valTFT = valVolts = valRPM = valSpeed = "---";
    valWheelLF = valWheelRF = valWheelRear = "---";
    hasDTCWarning = false;
    dtcTotal = absDtcTotal = -1;
}