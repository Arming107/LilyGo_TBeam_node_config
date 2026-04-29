/*
 * PROJECT: LoRaWAN 433 MHz implementation on LilyGo T-Beam v1.2 node
 * AUTHOR: Armin Gembal
 * DATE: April 2026
 * DESCRIPTION:
 * Power-saving alarm device built on the LilyGo T-Beam v1.2 node,
 * that wakes up from deep sleep only when motion, vibration or sound is detected.
 * NOTE: 
 * Requires implementation of the refactored lorabase_eu868.h file into the lmic repository.
 * Keys in this file are placeholders. Replace them with real TTSS keys.
 */

// 1. LIBRARIES AND OBJECT INITIALIZATION
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Wire.h>
#include "XPowersLib.h"
#include <esp_wifi.h>
#include <esp_bt.h>
#include "driver/rtc_io.h"

// Creating an object for the Power Management Unit (PMU) AXP2101, which is on the T-Beam board.
XPowersAXP2101 pmu; 

// 2. LORAWAN KEYS FOR OTAA CONNECTION (replace with your own keys)
// APPEUI: Application identifier. In LSB (Least Significant Byte) format.
// These keys are sent during the network join request (Join Request).
static const u1_t PROGMEM APPEUI[8]={ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8);}

// DEVEUI: Unique device identifier (Device EUI). Also in LSB format.
static const u1_t PROGMEM DEVEUI[8]={ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8);}

// APPKEY: Application key (password). In MSB (Most Significant Byte) format.
// Used to encrypt communication between the device and the server.
static const u1_t PROGMEM APPKEY[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
void os_getDevKey (u1_t* buf) { memcpy_P(buf, APPKEY, 16);}

// 3. PIN DEFINITIONS AND GLOBAL VARIABLES
// These pins are used to connect the sensors.
#define PIN_MOTION     GPIO_NUM_14  // Motion sensor
#define PIN_VIBRATION  GPIO_NUM_25  // Vibration sensor
#define PIN_MICROPHONE GPIO_NUM_13  // Sound sensor
#define PIN_LED 4                   // Pin for the onboard indicator LED

// Stores information about what woke the device (1=motion, 2=vibration, 3=sound).
uint8_t payload = 0;             

// Structure that tells the LMIC library how to schedule tasks (e.g. sending data).
static osjob_t sendJob;

// Pin mapping for the LoRa chip on this specific board.
// LMIC needs to know which ESP32 pins to use for the LoRa module.
const lmic_pinmap lmic_pins = {
    .nss = 18,               // Chip Select pin for SPI
    .rxtx = LMIC_UNUSED_PIN, // Not connected (RX/TX switching is handled internally by the module)
    .rst = 23,               // Reset pin for the LoRa module
    .dio = {26, 33, 32},     // Hardware interrupt pins from the LoRa module (Data In/Out)
};

// 4. FUNCTION FOR ENTERING DEEP SLEEP MODE
// This function minimizes battery consumption to an absolute minimum and waits for an interrupt from the sensors.
void sleepBoard() {
    Serial.println(F("\n[SLEEP] Starting the board sleep process."));
    
    // Disable WiFi and Bluetooth in case they are running.
    esp_wifi_stop();
    esp_bt_controller_disable();
    
    // Disable active PMU measurement functions.
    pmu.disableBattVoltageMeasure();
    pmu.disableSystemVoltageMeasure();
    pmu.disableBattDetection();
    
    // Disable voltage rails that are not needed right now.
    pmu.disableALDO1();
    pmu.disableALDO2();
    pmu.disableALDO3();
    pmu.disableALDO4();
    pmu.disableBLDO1();
    pmu.disableBLDO2();
    pmu.disableDC2(); 
    pmu.disableDC3(); 
    pmu.disableDC4();
    pmu.disableDC5();
    
    // Configure pins during sleep using the RTC (Real Time Clock) subsystem.
    // Enabling pull-down (pulls voltage to zero) and disabling pull-up (pulls voltage to 3.3V).
    // This ensures that pins do not spontaneously generate false alarms.
    rtc_gpio_pulldown_en(PIN_MOTION);
    rtc_gpio_pullup_dis(PIN_MOTION);
    rtc_gpio_pulldown_en(PIN_VIBRATION);
    rtc_gpio_pullup_dis(PIN_VIBRATION);
    rtc_gpio_pulldown_en(PIN_MICROPHONE);
    rtc_gpio_pullup_dis(PIN_MICROPHONE);

    Serial.println(F("[SLEEP] Waiting 2 seconds for power to stabilize."));
    delay(2000); // Allow time for electronics to settle after disabling voltage rails.

    // Set sensor pins as inputs to read their state.
    pinMode(PIN_MOTION, INPUT);
    pinMode(PIN_VIBRATION, INPUT);
    pinMode(PIN_MICROPHONE, INPUT);

    Serial.print(F("[SLEEP] Checking sensors. Waiting for LOW state on all pins."));
    
    // Protection against getting stuck in a wake-up loop.
    // If a sensor continuously reports HIGH (e.g. someone is holding the motion sensor active),
    // the board would wake up immediately after going to sleep. We wait here until all sensors
    // are idle (LOW).
    while(digitalRead(PIN_MOTION) == HIGH || 
          digitalRead(PIN_VIBRATION) == HIGH || 
          digitalRead(PIN_MICROPHONE) == HIGH) {
        
        delay(300); // Wait 300 ms and check again.
        Serial.print(F("."));
    }
    Serial.println(F("\n[SLEEP] DONE (Power stable, sensors quiet)."));

    Serial.println(F("[SLEEP] Configuring wake-up for pins 13, 14, 25."));
    
    // Creating a bitmask. Telling the processor exactly which pins to monitor.
    uint64_t mask = (1ULL << PIN_MOTION) | (1ULL << PIN_VIBRATION) | (1ULL << PIN_MICROPHONE);
    
    // Configure wake-up to trigger when any of the masked pins goes HIGH.
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_HIGH);
    
    Serial.println(F("[SLEEP] Sleep setup complete."));
    Serial.flush(); // Ensures all serial output is transmitted before the chip powers down.
    
    digitalWrite(PIN_LED, HIGH); // Turn on LED to indicate the board is going to sleep.
    pinMode(PIN_LED, OUTPUT);    // Set LED pin as output so it turns off during sleep.
    
    // Powers down the main processor and enters deep sleep mode.
    esp_deep_sleep_start();
}

// 5. LORA NETWORK EVENT HANDLER
// This function is called by the LMIC library depending on the current communication phase.
void onEvent (ev_t ev) {
    switch (ev) {
        case EV_JOINING:
            // The board is currently attempting to join the network.
            Serial.println(F("\n[LORA] Join in progress."));
            break;
            
        case EV_JOINED:
            // The board successfully joined. Network session keys received.
            Serial.println(F("\n[LORA] Successfully joined."));
            // Disable Link Check mode. LMIC would otherwise verify connection quality.
            LMIC_setLinkCheckMode(0); 
            break;
            
        case EV_TXCOMPLETE:
            // Data was successfully transmitted.
            Serial.println(F("\n[LORA] Data sent to TTN."));
            // Board goes to sleep to conserve battery.
            sleepBoard(); 
            break;
            
        default:
            // Other system events (e.g. EV_RXCOMPLETE) are ignored for simplicity.
            break;
    }
}

// 6. DATA TRANSMISSION FUNCTION
// This function prepares data and queues it for transmission to the network.
void sendData(osjob_t* j) {
    // Check whether another transmission is already in progress.
    if (!(LMIC.opmode & OP_TXRXPEND)) {
        Serial.print(F("[LORA] Queuing payload: "));
        Serial.println(payload);
        
        // Queue the data for transmission.
        // Parameters: port, data to send, data length, confirmation (0 = no ACK requested).
        LMIC_setTxData2(1, &payload, 1, 0);
    } else {
        Serial.println(F("[LORA] Error: OP_TXRXPEND (Transmitter is busy)."));
    }
}

// 7. MAIN SETUP ON BOOT / WAKE-UP
// Setup() runs on every power-on, reset, and also after waking from deep sleep.
void setup() {
    Serial.begin(115200); // Serial baud rate for console output.
    analogWrite(PIN_LED, 254); // Set LED to minimum brightness.

    delay(1000); // Wait one second for the system and serial port on the PC to stabilize.
    
    Serial.println(F("\n\n======================================="));
    Serial.println(F("=========== BOARD HAS WOKEN UP ========"));
    Serial.println(F("======================================="));

    // Initialize I2C bus for communication with the PMU.
    Wire.begin(21, 22);
    
    // Start and detect the PMU. Check whether the chip responds.
    if (pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, 21, 22)) {
        Serial.println(F("[START] PMU found"));

        // Enable battery status monitoring.
        pmu.enableBattVoltageMeasure();
        pmu.enableBattDetection();
        delay(50); // Required pause for the first valid PMU readings.
        
        // Read and print battery status.
        if (pmu.isBatteryConnect()) {
            uint16_t vbatt = pmu.getBattVoltage(); // Voltage in millivolts.
            int percent = pmu.getBatteryPercent(); // Capacity in percent.
            Serial.print(F("[BATTERY] Voltage: "));
            Serial.print(vbatt);
            Serial.print(F(" mV | Level: "));
            Serial.print(percent);
            Serial.println(F(" %"));
        } else {
            Serial.println(F("[BATTERY] Battery not connected (powered via USB)"));
        }

        // Enable power rail for the LoRa module at 3.3V.
        pmu.setALDO2Voltage(3300); 
        pmu.enableALDO2();         
        Serial.println(F("[START] -> LoRa power ON"));
        
        pmu.disableALDO3(); // Disable power rail for the GPS chip.
        
        // PMU charging LED indication while charging.
        pmu.setChargingLedMode(XPOWERS_CHG_LED_CTRL_CHG);
    } else {
        Serial.println(F("[START] ERROR: PMU not found"));
    }
    
    Serial.println(F("[START] Detecting wake-up reason."));
    // Determine why the processor is running — whether it was triggered by power-on, a sensor, or a reset.
    esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
    
    // ESP_SLEEP_WAKEUP_EXT1 means the chip was woken by a signal from external pins (a sensor).
    if (wakeupReason == ESP_SLEEP_WAKEUP_EXT1) {
        // Read the bitmask to identify which specific pin woke the chip.
        uint64_t wakeupPinMask = esp_sleep_get_ext1_wakeup_status();
        
        // Use the bitmask to determine which sensor triggered the wake-up and set the payload accordingly.
        if (wakeupPinMask & (1ULL << PIN_MOTION)) {
            payload = 1; // Message 1 = Motion.
            Serial.println(F("[START] Alarm detected: MOTION (Pin 14)"));
        } else if (wakeupPinMask & (1ULL << PIN_VIBRATION)) {
            payload = 2; // Message 2 = Vibration.
            Serial.println(F("[START] Alarm detected: VIBRATION (Pin 25)"));
        } else if (wakeupPinMask & (1ULL << PIN_MICROPHONE)) {
            payload = 3; // Message 3 = Sound.
            Serial.println(F("[START] Alarm detected: MICROPHONE (Pin 13)"));
        }
    } else {
        // If a sensor did not trigger the wake-up — the board was just powered on or reset via button.
        payload = 0;
        Serial.println(F("[START] Normal start (Reset or battery connected)."));
    }

    // Decide what to do next.
    if (payload > 0) {
        Serial.println(F("[START] Valid payload detected. Starting SPI and LoRa."));
        SPI.begin(5, 19, 27, 18);  // Start SPI communication to LoRa module. Parameters: (SCK, MISO, MOSI, SS/CS).
        os_init();  // Start LMIC core for LoRaWAN communication.
        LMIC_reset(); // Reset current LMIC state.
        sendData(&sendJob);  // Send message. This calls our sendData function defined earlier.
    } else {
        // If a sensor did not wake us (board was just powered on, payload is 0), skip transmission and go straight to sleep.
        Serial.println(F("[START] Payload is 0. Board going to sleep."));
        sleepBoard(); 
    }
}

// 8. MAIN LOOP (LORA TASK PROCESSING)
void loop() {
    // LMIC handles data transmission inside this function.
    // Must be called continuously until a successful transmission occurs (EV_TXCOMPLETE).
    os_runloop_once();
    
    // Safety watchdog using millis(). If the device has been awake for more than 40 seconds, something is wrong.
    // This can happen when the join attempt stalls due to poor signal.
    if (millis() > 40000) {
        Serial.println(F("\n[LOOP] ERROR: 40-second timeout expired. Message not sent. Board going to sleep."));
        // The board goes to sleep to avoid draining the battery during a prolonged failed network search.
        sleepBoard();
    }
}