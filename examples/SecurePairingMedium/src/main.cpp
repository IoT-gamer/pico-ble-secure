/**
 * SecurePairingMedium/src/main.cpp - Example of BLE pairing with MEDIUM security level
 * 
 * This example demonstrates how to set up a BLE peripheral with the MEDIUM security level.
 * The MEDIUM security level uses encryption without MITM protection (Just Works pairing).
 * 
 * For the Raspberry Pi Pico with arduino-pico core.
 */

 #include <Arduino.h>
 #include <BTstackLib.h>
 #include <BLESecure.h>
 #include "BLENotify.h"
 
 // Define UUIDs for service and characteristic
 UUID service("37f29ab1-28c2-4bf4-a88b-9ddad94c7575");
 UUID characteristicUUID("37f29ab2-28c2-4bf4-a88b-9ddad94c7575");
 
 // Service and Characteristic handles
 uint16_t char_handle;
 
 // Flag to track if a device is connected
 bool deviceConnected = false;
 BLEDevice* connectedDevice = nullptr;
 
 // Callbacks for BLE events
 void bleDeviceConnected(BLEStatus status, BLEDevice* device) {
  if (status == BLE_STATUS_OK) {
    Serial.println("Device connected!");
    deviceConnected = true;
    connectedDevice = device;
    
    // No need to manually check BLESecure._requestPairingOnConnect
    // or call BLESecure.requestPairing() here
    // The BLESecure library now handles this automatically
    
  } else {
    Serial.print("Connection failed with status: ");
    Serial.println(status);
  }
} 
 
 void bleDeviceDisconnected(BLEDevice* device) {
   Serial.println("Device disconnected!");
   deviceConnected = false;
   connectedDevice = nullptr;
   
   // Let BLENotify know of disconnection
   BLENotify.handleDisconnection();
 }
 
 // Callback for pairing status updates
 void onPairingStatus(BLEPairingStatus status, BLEDevice* device) {
   switch (status) {
     case PAIRING_IDLE:
       Serial.println("Pairing idle");
       break;
     case PAIRING_STARTED:
       Serial.println("Pairing started (Just Works method)");
       break;
     case PAIRING_COMPLETE:
       Serial.println("Pairing complete - connection is now encrypted!");
       Serial.println("Note: This MEDIUM security level uses 'Just Works' pairing");
       Serial.println("which provides encryption but is vulnerable to MITM attacks");
       break;
     case PAIRING_FAILED:
       Serial.println("Pairing failed");
       break;
   }
 }
 
 // Callback for GATT characteristic write
 int gattWriteCallback(uint16_t characteristic_id, uint8_t* buffer, uint16_t buffer_size) {
   if (characteristic_id == char_handle) {
     Serial.print("Received data: ");
     for (int i = 0; i < buffer_size; i++) {
       Serial.print((char)buffer[i]);
     }
     Serial.println();
   }
   
   // Check if this is a write to the CCC descriptor (notifications enable/disable)
   if (buffer_size == 2) {
     uint16_t value = (buffer[1] << 8) | buffer[0];
     if (value == 0x0001) {
       // Find the corresponding characteristic handle from the descriptor handle
       uint16_t char_handle = characteristic_id - 1; // Usually CCC is right after the characteristic
       BLENotify.handleSubscriptionChange(char_handle, true);
       Serial.println("Notifications enabled by client");
     } else if (value == 0x0000) {
       uint16_t char_handle = characteristic_id - 1;
       BLENotify.handleSubscriptionChange(char_handle, false);
       Serial.println("Notifications disabled by client");
     }
   }
   
   return 0;
 }
 
 void setup() {
   // Initialize serial for debugging
   Serial.begin(115200);
   while (!Serial) delay(10);
   Serial.println("BLE MEDIUM Security Example");
 
   // Initialize the BLENotify library
   BLENotify.begin();
 
   // Set device name
   BTstack.setup("MediumSecBLE");
   
   // Initialize BLE security with No Input No Output capability
   // This will trigger "Just Works" pairing
   BLESecure.begin(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
   
   // Set security level to MEDIUM - encryption without MITM protection
   // Set Bonding enabled to store LTK
   BLESecure.setSecurityLevel(SECURITY_MEDIUM, true);
   
   // Allow LTK reconstruction without database entry
   // This helps with peripherals that don't have full bonding management
   BLESecure.allowReconnectionWithoutDatabaseEntry(true);
   
   // Request pairing automatically when a device connects
   BLESecure.requestPairingOnConnect(true);
   
   // Register callback for pairing status
   BLESecure.setPairingStatusCallback(onPairingStatus);
   
   // Register callbacks for BLE connection events
   BLESecure.setBLEDeviceConnectedCallback(bleDeviceConnected);
   BLESecure.setBLEDeviceDisconnectedCallback(bleDeviceDisconnected);
   
   // Register GATT write callback
   BTstack.setGATTCharacteristicWrite(gattWriteCallback);
   
   // Add service and characteristic
   BTstack.addGATTService(&service);
   char_handle = BLENotify.addNotifyCharacteristic(
     &characteristicUUID,
     ATT_PROPERTY_READ | 
     ATT_PROPERTY_WRITE |
     ATT_PROPERTY_NOTIFY
   );
   
   // Start advertising
   BTstack.startAdvertising();
   
   Serial.println("BLE peripheral started with MEDIUM security (Just Works pairing)");
   Serial.println("Waiting for connections...");
 }
 
 void loop() {
   // If connected and paired, send a notification every 5 seconds
   static unsigned long lastNotify = 0;
   
   if (deviceConnected && BLESecure.getPairingStatus() == PAIRING_COMPLETE) {
     if (millis() - lastNotify > 5000) {
       // Create a message with timestamp
       char message[20];
       sprintf(message, "Encrypted: %lu", millis() / 1000);
       
       // Check if client is subscribed to notifications
       if (BLENotify.isSubscribed(char_handle)) {
         // Send notification using BLENotify
         if (BLENotify.notify(char_handle, message, strlen(message))) {
           Serial.print("Sent encrypted notification: ");
           Serial.println(message);
         }
       }
       
       lastNotify = millis();
     }
   }
   
   // Process BLE events
   BTstack.loop();
   // Process any pending notifications
   BLENotify.update();
   
   delay(10);
 }