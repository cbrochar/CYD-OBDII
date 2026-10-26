#ifndef DTC_DESCRIPTIONS_H
#define DTC_DESCRIPTIONS_H

#include <Arduino.h>

// ---- Trouble code descriptions -------------------------------------------------------
// Short meanings for codes common on the 6.0L Powerstroke and for generic OBD-II codes.
// P0xxx / P2xxx / Cxxxx / Uxxxx titles are the standard SAE J2012 meanings; P1xxx codes
// are Ford-specific. A code not listed here shows "No description stored".
// To add one, append a row: {"P0123", "What it means"}.

struct DtcDescription {
    const char* code;
    const char* text;
};

static const DtcDescription DTC_TABLE[] = {
    // Air, boost, sensors
    {"P0045", "Turbo boost control solenoid circuit (VGT)"},
    {"P0069", "MAP / barometric pressure correlation"},
    {"P0101", "Mass air flow (MAF) sensor circuit range/performance"},
    {"P0102", "Mass air flow (MAF) sensor circuit low"},
    {"P0103", "Mass air flow (MAF) sensor circuit high"},
    {"P0106", "Manifold absolute pressure (MAP) sensor range/performance"},
    {"P0107", "Manifold absolute pressure (MAP) sensor circuit low"},
    {"P0108", "Manifold absolute pressure (MAP) sensor circuit high"},
    {"P0112", "Intake air temperature sensor circuit low"},
    {"P0113", "Intake air temperature sensor circuit high"},
    {"P0117", "Engine coolant temperature sensor circuit low"},
    {"P0118", "Engine coolant temperature sensor circuit high"},
    {"P0128", "Coolant temperature below thermostat regulating temperature (thermostat stuck open?)"},
    {"P0196", "Engine oil temperature sensor range/performance"},
    {"P0197", "Engine oil temperature sensor circuit low"},
    {"P0198", "Engine oil temperature sensor circuit high"},
    {"P0234", "Turbocharger overboost condition"},
    {"P0236", "Turbo boost sensor A circuit range/performance"},
    {"P0237", "Turbo boost sensor A circuit low"},
    {"P0238", "Turbo boost sensor A circuit high"},
    {"P0299", "Turbocharger underboost"},
    {"P2228", "Barometric pressure sensor circuit low"},
    {"P2229", "Barometric pressure sensor circuit high"},
    {"P2262", "Turbo boost pressure not detected (mechanical)"},
    {"P2263", "Turbo boost system performance"},
    {"P1247", "Turbo boost pressure low (Ford)"},

    // Engine timing, speed sensors
    {"P0335", "Crankshaft position sensor A circuit"},
    {"P0336", "Crankshaft position sensor A range/performance"},
    {"P0340", "Camshaft position sensor A circuit"},
    {"P0341", "Camshaft position sensor A range/performance"},
    {"P0500", "Vehicle speed sensor A"},

    // Glow plugs
    {"P0380", "Glow plug / heater circuit A"},
    {"P0381", "Glow plug / heater indicator (wait-to-start lamp) circuit"},
    {"P0670", "Glow plug control module circuit"},
    {"P0683", "Glow plug control module to PCM communication circuit"},

    // EGR, exhaust back pressure
    {"P0401", "EGR flow insufficient (EGR valve or cooler clogged?)"},
    {"P0402", "EGR flow excessive"},
    {"P0403", "EGR control circuit"},
    {"P0404", "EGR control circuit range/performance"},
    {"P0405", "EGR position sensor A circuit low"},
    {"P0406", "EGR position sensor A circuit high"},
    {"P0470", "Exhaust back pressure (EBP) sensor circuit"},
    {"P0471", "Exhaust back pressure (EBP) sensor range/performance"},
    {"P0472", "Exhaust back pressure (EBP) sensor circuit low"},
    {"P0473", "Exhaust back pressure (EBP) sensor circuit high"},
    {"P0478", "Exhaust pressure control valve high input"},

    // Cooling fan
    {"P0480", "Cooling fan 1 control circuit"},
    {"P0526", "Fan speed sensor circuit"},
    {"P0528", "Fan speed sensor circuit, no signal"},

    // Electrical, modules
    {"P0562", "System voltage low"},
    {"P0563", "System voltage high"},
    {"P0603", "Control module keep-alive memory (KAM) error"},
    {"P0605", "Control module read-only memory (ROM) error"},
    {"P0606", "Control module (PCM) processor fault"},
    {"P0611", "Fuel injector control module (FICM) performance"},
    {"P0645", "A/C clutch relay control circuit"},

    // Fuel injection (6.0L HEUI: ICP = injection control pressure)
    {"P1211", "Injection control pressure (ICP) higher/lower than desired (Ford)"},
    {"P1291", "Injector high side short to ground or battery, bank 1 (Ford)"},
    {"P1292", "Injector high side short to ground or battery, bank 2 (Ford)"},
    {"P2269", "Water in fuel condition"},
    {"P2284", "Injection control pressure (ICP) sensor range/performance"},
    {"P2285", "Injection control pressure (ICP) sensor circuit low"},
    {"P2286", "Injection control pressure (ICP) sensor circuit high"},
    {"P2290", "Injection control pressure (ICP) too low"},
    {"P2291", "Injection control pressure (ICP) too low while cranking"},

    // Transmission
    {"P0700", "Transmission control system fault (see transmission codes)"},
    {"P0711", "Transmission fluid temperature sensor range/performance"},
    {"P0712", "Transmission fluid temperature sensor circuit low"},
    {"P0713", "Transmission fluid temperature sensor circuit high"},
    {"P0715", "Turbine / input shaft speed sensor circuit"},
    {"P0720", "Output shaft speed sensor circuit"},
    {"P0741", "Torque converter clutch performance or stuck off"},
    {"P0750", "Shift solenoid A"},
    {"P0755", "Shift solenoid B"},
    {"P0760", "Shift solenoid C"},
    {"P0765", "Shift solenoid D"},

    // Ford general
    {"P1000", "OBD monitors not finished yet (normal after a battery disconnect or code clear) (Ford)"},

    // Chassis (ABS) and network
    {"C0035", "Left front wheel speed sensor circuit"},
    {"C0040", "Right front wheel speed sensor circuit"},
    {"C0045", "Left rear wheel speed sensor circuit"},
    {"C0050", "Right rear wheel speed sensor circuit"},
    {"C1145", "Right front wheel speed sensor circuit failure (Ford ABS)"},
    {"C1155", "Left front wheel speed sensor circuit failure (Ford ABS)"},
    {"C1230", "Rear axle speed sensor circuit failure (Ford ABS)"},
    {"U0100", "Lost communication with the engine computer (PCM)"},
    {"U0101", "Lost communication with the transmission computer (TCM)"},
    {"U0121", "Lost communication with the ABS module"},
    {"U0155", "Lost communication with the instrument cluster"},
};
static const int DTC_TABLE_COUNT = sizeof(DTC_TABLE) / sizeof(DTC_TABLE[0]);

// Returns the description of a code like "P0470", or "" if none is stored
inline String describeDTC(const String& code) {
    for (int i = 0; i < DTC_TABLE_COUNT; i++) {
        if (code == DTC_TABLE[i].code) return DTC_TABLE[i].text;
    }

    // Per-cylinder ranges (the same pattern on every engine)
    if (code.length() == 5 && code[0] == 'P') {
        int n = strtol(code.c_str() + 1, NULL, 16);
        if (n >= 0x0261 && n <= 0x0284) {   // 3 codes per cylinder: low, high, contribution/balance
            int cyl = (n - 0x0261) / 3 + 1;
            int kind = (n - 0x0261) % 3;
            static const char* KINDS[] = {"circuit low", "circuit high", "contribution/balance"};
            return "Cylinder " + String(cyl) + " injector " + KINDS[kind];
        }
        if (n >= 0x0671 && n <= 0x0678) return "Cylinder " + String(n - 0x0670) + " glow plug circuit";
        if (n >= 0x0301 && n <= 0x0308) return "Cylinder " + String(n - 0x0300) + " misfire detected";
        if (n == 0x0300) return "Random / multiple cylinder misfire detected";
    }
    return "";
}

// "Generic" (same meaning on every vehicle) or "Ford-specific" for codes not in the table
inline const char* dtcKind(const String& code) {
    if (code.length() < 2) return "";
    char d = code[1];
    if (code[0] == 'P') return (d == '0' || d == '2') ? "Generic powertrain code" : "Ford-specific powertrain code";
    if (code[0] == 'C') return d == '0' ? "Generic chassis code" : "Ford-specific chassis code";
    if (code[0] == 'B') return d == '0' ? "Generic body code" : "Ford-specific body code";
    if (code[0] == 'U') return d == '0' ? "Generic network code" : "Ford-specific network code";
    return "";
}

#endif
