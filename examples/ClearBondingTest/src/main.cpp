#include <Arduino.h>
#include <BTstackLib.h>
#include <BLESecure.h>
#include "ble/le_device_db.h" // required for le_device_db_dump

// Configuration
const char* DEVICE_NAME = "BondClearTestPico";
const BLESecurityLevel SECURITY_LEVEL = SECURITY_MEDIUM;
const io_capability_t IO_CAPABILITY = IO_CAPABILITY_NO_INPUT_NO_OUTPUT;

// Global State
bool deviceConnected = false;
BLEDevice* connectedDevicePtr = nullptr;

// Callbacks
void bleDeviceConnectedCallback(BLEStatus status, BLEDevice* device) {
    if (status == BLE_STATUS_OK) {
        Serial.println("Device connected!");
        digitalWrite(LED_BUILTIN, HIGH);
        deviceConnected = true;
        connectedDevicePtr = device;
    } else {
        Serial.print("Connection failed, status: ");
        Serial.println(status);
        digitalWrite(LED_BUILTIN, LOW);
    }
}

void bleDeviceDisconnectedCallback(BLEDevice* device) {
    Serial.println("Device disconnected!");
    digitalWrite(LED_BUILTIN, LOW);
    deviceConnected = false;
    connectedDevicePtr = nullptr;
    // Only restart advertising if we are not in the middle of a BOOTSEL reset
    // The BOOTSEL logic will handle restarting advertising.
    if (!BOOTSEL) { // Assuming BOOTSEL is low when not pressed after initial check
         Serial.println("Restarting advertising after normal disconnect.");
         BTstack.startAdvertising();
    }
}

void onPairingStatusCallback(BLEPairingStatus status, BLEDevice* device) {
    Serial.print("Pairing Status: ");
    switch (status) {
        case PAIRING_IDLE: Serial.println("IDLE"); break;
        case PAIRING_STARTED: Serial.println("STARTED"); break;
        case PAIRING_COMPLETE: 
            Serial.println("COMPLETE - Device Bonded/Re-encrypted.");
            Serial.println("LE Device DB Dump after pairing/re-encryption:");
            le_device_db_dump(); // See what's in the DB now
            break;
        case PAIRING_FAILED: Serial.println("FAILED"); break;
        default: Serial.println("UNKNOWN"); break;
    }
}

void setup() {
    Serial.begin(115200);
    // Wait for serial, with timeout, but not indefinitely
    // unsigned long setupStartTime = millis();
    // while (!Serial && (millis() - setupStartTime < 3000)) {
    //     delay(100);
    // }
    while (!Serial) delay(10); // Wait for serial to be ready
    delay(100); // Give some time for the serial to stabilize
    Serial.println();
    Serial.println("BLESecure ClearBondingTest Example (with LTK Reconstruction Toggle)");

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    BTstack.setup(DEVICE_NAME); // Initializes BTstack, HCI, L2CAP, SM, GATT client/server

    // Initial BLESecure configuration for normal operation
    BLESecure.begin(IO_CAPABILITY);
    BLESecure.setSecurityLevel(SECURITY_LEVEL, true); // Enable bonding
    BLESecure.requestPairingOnConnect(true);      
    BLESecure.allowReconnectionWithoutDatabaseEntry(true); // Default: allow LTK reconstruction

    BLESecure.setBLEDeviceConnectedCallback(bleDeviceConnectedCallback);
    BLESecure.setBLEDeviceDisconnectedCallback(bleDeviceDisconnectedCallback);
    BLESecure.setPairingStatusCallback(onPairingStatusCallback);

    BTstack.startAdvertising();
    Serial.println("Advertising started. Waiting for connections...");
    Serial.println("Press and HOLD BOOTSEL button to clear all bondings and disable LTK reconstruction for next pairing.");
}

void loop() {
    BTstack.loop();

    static bool bootsel_action_taken = false;
    static unsigned long bootsel_press_start_time = 0;
    const unsigned long BOOTSEL_HOLD_DURATION_MS = 500; 

    if (BOOTSEL) { 
        if (bootsel_press_start_time == 0) { 
            bootsel_press_start_time = millis();
        } else if (!bootsel_action_taken && (millis() - bootsel_press_start_time >= BOOTSEL_HOLD_DURATION_MS)) {
            Serial.println("BOOTSEL button held - triggering bond clear action!");
            
            if (deviceConnected && connectedDevicePtr) {
                Serial.println("Disconnecting current device...");
                BTstack.bleDisconnect(connectedDevicePtr);
                // Give BTstack time to process disconnection
                for(int i=0; i<100; ++i) { BTstack.loop(); delay(10); } 
            }

            Serial.println("Attempting to clear all BLE bondings (e.g., flash erase)...");
            BLESecure.clearAllBondings(); // This is your v6 from Canvas (attempts flash erase)

            Serial.println("BOOTSEL: Temporarily DISALLOWING LTK reconstruction for next pairing attempt.");
            BLESecure.allowReconnectionWithoutDatabaseEntry(false); // *** KEY STEP ***

            // Re-initialize BLESecure's SM settings after clearing and changing LTK policy
            Serial.println("BOOTSEL: Re-applying Security Manager settings.");
            BLESecure.begin(IO_CAPABILITY); 
            BLESecure.setSecurityLevel(SECURITY_LEVEL, true); 
            BLESecure.requestPairingOnConnect(true); 
            // At this point, LTK reconstruction is still false.
            // It will be set back to true only after a successful new pairing,
            // or upon next full device reset/setup.

            Serial.println("BOOTSEL: Restarting advertising...");
            BTstack.startAdvertising();
            
            bootsel_action_taken = true; 
            Serial.println("Action complete. Release BOOTSEL. Pico is ready for a fresh pairing.");
        }
    } else { 
        if (bootsel_press_start_time != 0) { // If it was pressed
             Serial.println("BOOTSEL button released.");
        }
        bootsel_press_start_time = 0; 
        bootsel_action_taken = false; 
    }

    static unsigned long lastBlinkTime = 0;
    if (!deviceConnected && (millis() - lastBlinkTime > 1000)) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        lastBlinkTime = millis();
    }
    if(deviceConnected) {
      digitalWrite(LED_BUILTIN, HIGH);
    }

    delay(10); 
}