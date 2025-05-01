/**
 * SecurePairingHighSC/src/main.cpp - Example of BLE pairing with HIGH_SC security level
 * 
 * This example demonstrates how to set up a BLE peripheral with the HIGH_SC security level.
 * This is the highest security level, using encryption with MITM protection
 * and Secure Connections, providing the strongest security guarantees.
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
  
 // Global variable to store the current MTU size
 uint16_t current_mtu_size = 23; // ATT_DEFAULT_MTU is 23 bytes
  
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
   
   // Reset MTU size to default
   current_mtu_size = 23; // Default is 23 bytes
   
   // Let BLENotify know of disconnection
   BLENotify.handleDisconnection();
 }
  
 // GATT notification callback
 void gattCharacteristicNotification(BLEDevice* device, uint16_t value_handle, uint8_t* value, uint16_t length) {
   Serial.print("Received notification on handle: ");
   Serial.print(value_handle);
   Serial.print(", data: ");
   for (int i = 0; i < length; i++) {
     Serial.print((char)value[i]);
   }
   Serial.println();
 }
 
 // Callback for displaying passkey during pairing
 void onPasskeyDisplay(uint32_t passkey) {
   Serial.print("Please enter this passkey on your device: ");
   Serial.println(passkey);
 }
  
 // Callback for numeric comparison during pairing
 void onNumericComparison(uint32_t passkey, BLEDevice* device) {
   Serial.print("Do the following numbers match? ");
   Serial.println(passkey);
   Serial.println("Please verify this number on both devices");
   Serial.println("For this example, automatically confirming...");
   
   // In a real application, you would get confirmation from the user
   // For example via a hardware button press or serial input
   BLESecure.acceptNumericComparison(true);
 }
  
 // Callback for passkey entry
 void onPasskeyEntry() {
   Serial.println("Passkey entry required.");
   Serial.println("Please enter the passkey via Serial:");
   Serial.println("Format: 'passkey:123456'");
 }
  
 // Callback for pairing status updates
 void onPairingStatus(BLEPairingStatus status, BLEDevice* device) {
   switch (status) {
     case PAIRING_IDLE:
       Serial.println("Pairing idle");
       break;
     case PAIRING_STARTED:
       Serial.println("Pairing started using Secure Connections");
       break;
     case PAIRING_COMPLETE:
       Serial.println("Pairing complete - connection is now secured with highest level!");
       Serial.println("Using encryption with MITM protection and Secure Connections");
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
   Serial.println("BLE HIGH_SC Security Example");
  
   // Initialize the BLENotify library
   BLENotify.begin();
  
   // Set device name
   BTstack.setup("HighSCSecBLE");
   
   // Configure for better debugging of notifications
   Serial.println("Setting up for enhanced notification debugging");
   
   // Register GATT notification callback to monitor subscription events 
   BTstack.setGATTCharacteristicNotificationCallback(gattCharacteristicNotification);
   
   // Initialize BLE security with Numeric Comparison capability
   // This provides the strongest MITM protection when paired with appropriate central device
   BLESecure.begin(IO_CAPABILITY_DISPLAY_YES_NO);
   
   // Set security level to HIGH_SC - encryption with MITM protection and Secure Connections
   // Set bonding enabled to store LTK
   BLESecure.setSecurityLevel(SECURITY_HIGH_SC, true);
 
   // Allow LTK reconstruction without database entry
   // This helps with peripherals that don't have full bonding management
   BLESecure.allowReconnectionWithoutDatabaseEntry(true);
   
   // Request pairing automatically when a device connects
   BLESecure.requestPairingOnConnect(true);
   
   // Register callbacks for security events
   BLESecure.setPasskeyDisplayCallback(onPasskeyDisplay);
   BLESecure.setPasskeyEntryCallback(onPasskeyEntry);
   BLESecure.setPairingStatusCallback(onPairingStatus);
   BLESecure.setNumericComparisonCallback(onNumericComparison);
   
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
   
   Serial.println("BLE peripheral started with HIGH_SC security (Encryption with MITM protection and Secure Connections");
   Serial.println("Waiting for connections...");
 }
  
 void loop() {
   // If connected and paired, send a notification every 5 seconds
   static unsigned long lastNotify = 0;
   
   if (deviceConnected && BLESecure.getPairingStatus() == PAIRING_COMPLETE) {
     if (millis() - lastNotify > 5000) {
       // Create a message with timestamp
       char message[30];
       sprintf(message, "secure msg: %lu", millis() / 1000);
       
       // Debug MTU and notification process
       Serial.print("Current MTU size: ");
       Serial.println(current_mtu_size);
       
       // Check if client is subscribed to notifications
       if (BLENotify.isSubscribed(char_handle)) {
         Serial.print("Client is subscribed. Attempting to send notification (");
         Serial.print(strlen(message));
         Serial.print(" bytes, MTU: ");
         Serial.print(current_mtu_size);
         Serial.println(")");
         
         // Send notification using BLENotify
         bool success = BLENotify.notify(char_handle, message, strlen(message));
         if (success) {
           Serial.print("Sent highly secure notification: ");
           Serial.println(message);
         } else {
           Serial.println("Failed to send notification!");
         }
       } else {
         Serial.println("Client is not subscribed to notifications");
       }
       
       lastNotify = millis();
     }
   }
   
   // Read serial input for passkey entry (if requested)
   if (Serial.available()) {
     String input = Serial.readStringUntil('\n');
     if (input.startsWith("passkey:")) {
       String passkeyStr = input.substring(8);
       uint32_t passkey = passkeyStr.toInt();
       
       if (passkey <= 999999) {
         Serial.print("Setting entered passkey: ");
         Serial.println(passkey);
         BLESecure.setEnteredPasskey(passkey);
       } else {
         Serial.println("Invalid passkey (must be 0-999999)");
       }
     }
   }
   
   // Process BLE events
   BTstack.loop();
   // Process any pending notifications
   BLENotify.update();
   
   delay(10);
 }