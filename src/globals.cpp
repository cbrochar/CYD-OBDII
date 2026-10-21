#include "config.h"

// Hardware Instance Definitions
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite canvas = TFT_eSprite(&tft);
BluetoothSerial SerialBT;
ELM327 myELM;
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);

// State Flags
bool isConnected = false;
unsigned long lastBusQuery = 0;
unsigned long lastTouchDebounce = 0;
unsigned long lastBtRetry = 0;
int currentPage = 0;

// Metric Data Handlers
String valTCase = "N/A";
String valBoost = "0.0 PSI";
String valFICM = "48.0V";
String valECT = "0 F";
String valTFT = "0 F";
String valVolts = "12.0V";
bool hasDTCWarning = false;
