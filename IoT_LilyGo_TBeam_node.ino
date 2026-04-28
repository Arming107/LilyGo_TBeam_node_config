/*
 * PROJECT: LoRaWAN 433 MHz implementation on LilyGo T-Beam v1.2 node for IoT
 * AUTHOR: Armin Gembal
 * DATE: April 2026
 * NOTE: Keys in this file are placeholders. Replace them with real TTSS keys.
 */

// 1. KNIHOVNY A INICIALIZACE OBJEKTŮ
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Wire.h>
#include "XPowersLib.h"
#include <esp_wifi.h>
#include <esp_bt.h>
#include "driver/rtc_io.h"

// Vytvoření objektu pro správu napájení (Power Management Unit) AXP2101, který je na desce T-Beam.
XPowersAXP2101 pmu; 

// 2. NASTAVENÍ KLÍČŮ PRO LORAWAN PRO OTAA PŘIPOJENÍ (změňte si klíče podle vlastních)
// APPEUI: Identifikátor aplikace. V LSB (Least Significant Byte) formátu. 
// Tyto klíče se posílají při žádosti o připojení do sítě (Join Request).
static const u1_t PROGMEM APPEUI[8]={ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8);}

// DEVEUI: Unikátní identifikátor zařízení (Device EUI). Také v LSB formátu.
static const u1_t PROGMEM DEVEUI[8]={ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8);}

// APPKEY: Aplikační klíč (heslo). V MSB (Most Significant Byte) formátu.
// Slouží k šifrování komunikace mezi zařízením a serverem.
static const u1_t PROGMEM APPKEY[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
void os_getDevKey (u1_t* buf) { memcpy_P(buf, APPKEY, 16);}

// 3. DEFINICE PINŮ A GLOBÁLNÍCH PROMĚNNÝCH
// Tento pin slouží k připojení senzoru.
#define PIN_SENZOR GPIO_NUM_13  // Váš senzor.
#define PIN_LED 4               // Pin pro integrovanou indikační LED diodu.

// Zde se uloží informace, co zařízení probudilo (1=senzor, 0=běžný start).
uint8_t payload = 0;

// Struktura, která říká knihovně LMIC, jak naplánovat úlohy (např. odeslání dat).
static osjob_t sendjob;

// Definice zapojení LoRa čipu na této konkrétní desce.
// LMIC musí vědět, na kterých pinech ESP32 má hledat LoRa modul.
const lmic_pinmap lmic_pins = {
    .nss = 18,               // Pin pro výběr čipu (Chip Select) pro SPI.
    .rxtx = LMIC_UNUSED_PIN, // Nezapojeno (přepínání RX/TX si modul řeší sám).
    .rst = 23,               // Pin pro resetování LoRa modulu.
    .dio = {26, 33, 32},     // Piny pro hardwarová přerušení od LoRa modulu (Data In/Out).
};

// 4. FUNKCE PRO PŘECHOD DO REŽIMU HLUBOKÉHO SPÁNKU (DEEP SLEEP)
// Tato funkce minimalizuje spotřebu baterie na absolutní minimum a čeká na přerušení ze senzorů.
void uspani_desky() {
    Serial.println(F("\n[USPÁVÁNÍ] Startuji proces uspání desky."));
    
    // Vypnutí WiFi a Bluetooth, pokud by náhodou běžely.
    esp_wifi_stop();
    esp_bt_controller_disable();
        
    // Vypnutí napěťových větví, které teď nepotřebujeme.
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
    
    // Konfigurace pinu během spánku pomocí RTC (Real Time Clock).
    // Zapínáme "pulldown" (stahuje napětí k nule) a vypínáme "pullup" (stahuje napětí k 3.3V).
    // Tím zajistíme, že pin nebude samovolně generovat falešné poplachy.
    rtc_gpio_pulldown_en(PIN_SENZOR);
    rtc_gpio_pullup_dis(PIN_SENZOR);

    Serial.println(F("[USPÁVÁNÍ] Čekám 2 vteřiny na ustálení napájení."));
    delay(2000); // Necháme čas elektronice, aby se zklidnila po odpojení větví.

    // Nastavíme pin senzoru jako vstup pro čtení stavu.
    pinMode(PIN_SENZOR, INPUT);

    Serial.print(F("[USPÁVÁNÍ] Kontrola senzoru. Čekám na stav LOW na pinu."));
    
    // Ochrana před uvíznutím v probouzecí smyčce.
    // Pokud senzor neustále hlásí HIGH (např. někdo drží senzor pohybu aktivní), deska by se po
    // uspání okamžitě znovu probudila. Proto zde čekáme, dokud nejsou všechny senzory v klidu (LOW).
    while(digitalRead(PIN_SENZOR) == HIGH) {
        delay(300); // Čekáme 300 ms a kontrolujeme znovu.
        Serial.print(F("."));
    }
    Serial.println(F("\n[USPÁVÁNÍ] HOTOVO (Napájení je stabilní, senzor tichý)."));

    Serial.println(F("[USPÁVÁNÍ] Nastavuji buzení pro pin 13."));
    
    // Nastavení probuzení pro náš senzor.
    esp_sleep_enable_ext0_wakeup(PIN_SENZOR, 1);
    
    Serial.println(F("[USPÁVÁNÍ] Uspání hotovo."));
    Serial.flush(); // Zajišťuje, že se text do sériové linky odešle celý předtím, než se čip vypne.
    
    digitalWrite(PIN_LED, HIGH); // Rozsvítí se LED na znamení, že jdeme spát.
    pinMode(PIN_LED, OUTPUT);    // LED se vypne, aby při spánku nesvítila.
    
    // Vypíná hlavní procesor a přesouvá se do režimu hlubokého spánku.
    esp_deep_sleep_start();
}

// 5. OBSLUHA UDÁLOSTÍ LORA SÍTĚ
// Tuto funkci volá samotná knihovna LMIC podle toho, v jaké fázi se zrovna nachází.
void onEvent (ev_t ev) {
    switch (ev) {
        case EV_JOINING:
            // Deska se právě snaží připojit k síti.
            Serial.println(F("\n[LORA] Probíhá přihlášení."));
            break;
            
        case EV_JOINED:
            // Deska se úspěšně připojila. Obdržela síťové klíče.
            Serial.println(F("\n[LORA] Úspěšně přihlášeno."));
            // Vypnutí tzv. Link Check módu. LMIC by jinak ověřoval kvalitu spojení.
            LMIC_setLinkCheckMode(0); 
            break;
            
        case EV_TXCOMPLETE:
            // Data byla úspěšně odeslána.
            Serial.println(F("\n[LORA] Data odeslána na TTN."));
            // Deska z důvodu šetření baterie jde spát.
            uspani_desky(); 
            break;
            
        default:
            // Ostatní systémové události (např. EV_RXCOMPLETE) pro jednoduchost ignorujeme.
            break;
    }
}

// 6. FUNKCE PRO ODESLÁNÍ DAT
// Tato funkce připraví data a vloží je do fronty pro odeslání do sítě.
void do_send(osjob_t* j) {
    // Kontrola, zda neprobíhá jiný přenos.
    if (!(LMIC.opmode & OP_TXRXPEND)) {
        Serial.print(F("[LORA] Zařazuji do fronty payload: "));
        Serial.println(payload);
        
        // Vložení dat do fronty.
        // Parametry: port, odeílané data, délka dat, potvrzení odeslání (0 = nechceme ACK).
        LMIC_setTxData2(1, &payload, 1, 0);
    } else {
        Serial.println(F("[LORA] Chyba: OP_TXRXPEND (Vysílač je zaneprázdněn)."));
    }
}

// 7. HLAVNÍ NASTAVENÍ PŘI STARTU / PROBUZENÍ
// Setup() se spustí vždy při zapnutí, restartu, ale i po probuzení z deep sleepu
void setup() {
    Serial.begin(115200); // Rychlost sériové linky pro výpisy do konzole.
    analogWrite(PIN_LED, 254); // Nastavení minimálního jasu LED diody.

    delay(1000); // Počkáme sekundu na ustálení systému a spuštění sériového portu v PC.
    
    Serial.println(F("\n\n======================================="));
    Serial.println(F("========== DESKA SE PROBUDILA ========="));
    Serial.println(F("======================================="));

    // Inicializace I2C sběrnice pro komunikaci s PMU.
    Wire.begin(21, 22);
    
    // Spuštění a detekce PMU. Zjišťujeme, zda s námi čip komunikuje.
    if (pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, 21, 22)) {
        Serial.println(F("[START] PMU nalezeno"));
        // Zapnutí napájecí větve pro LoRa modul s napětím 3.3V.
        pmu.setALDO2Voltage(3300); 
        pmu.enableALDO2();
        Serial.println(F("[START] -> LoRa napájení ZAPNUTO"));
        pmu.disableALDO3(); // Vypínáme napájecí větev pro GPS čip.
    } else {
        Serial.println(F("[START] CHYBA: PMU nenalezeno"));
    }
    
    Serial.println(F("[START] Detekce důvodu probuzení."));
    // Zjistíme, proč vlastně procesor běží. Jestli ho probudilo zapnutí, senzor nebo restart.
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    // ESP_SLEEP_WAKEUP_EXT1 znamená, že čip probudil signál z externího pinu.
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        payload = 1; // Zpráva 1 = Senzor aktivován.
        Serial.println(F("[START] Senzor aktivován (Pin 13)"));
    } else {
        // Pokud nás neprobudil senzor, ale desku jsme teprve zapli nebo resetovali tlačítkem.
        payload = 0;
        Serial.println(F("[START] Běžný start (Reset nebo připojení baterie)."));
    }

    // Rozhodování, co dál.
    if (payload > 0) {
        Serial.println(F("[START] Zjištěn platný payload. Spouštění SPI a LoRa."));
        SPI.begin(5, 19, 27, 18);  // Start SPI komunikace k LoRa modulu. Parametry: (SCK, MISO, MOSI, SS/CS).
        os_init();  // Start LMIC jádra pro LoRaWAN kominukaci.
        LMIC_reset(); // Reset aktuálního stavu v LMIC.
        do_send(&sendjob);  // Odeslání zprávy. Toto zavolá naši funkci do_send z dřívější části kódu.
    } else {
        // Pokud nás neprobudil senzor (jen jsme to zapli a payload je 0), nic neposíláme a jdeme rovnou do režimu spánku.
        Serial.println(F("[START] Payload je 0. Deska se vypíná."));
        uspani_desky(); 
    }
}

// 8. HLAVNÍ SMYČKA (ZPRACOVÁNÍ LORA ÚLOH)
void loop() {
    // V této funkci se LMIC stará o odesílání dat.
    // Musí se volat neustále dokola, dokud nedojde k úspěšnému odeslání (EV_TXCOMPLETE).
    os_runloop_once();
    
    // Ochranný mechanismus pomocí millis() hlídá zařízení. Pokud je zařízení vzhůru déle než 40 sekund, něco je špatně.
    // Mohlo dojít k zaseknutí při připojování k síti kvůli špatnému signálu.
    if (millis() > 40000) {
        Serial.println(F("\n[LOOP] CHYBA: Timeout 40 vteřin vypršel. Zpráva se neodeslala. Desla se vypíná."));
        // Proto jde deska spát, aby nevybila baterku dlouhým marným hledáním sítě.
        uspani_desky();
    }
}