/**
 * BLESecure.h - Library for BLE secure pairing with encryption
 * 
 * This library extends the BLE functionality of arduino-pico core
 * to provide an easier interface for secure pairing and encryption.
 * 
 * Based on the BTstackLib library for Arduino-Pico.
 */

 #ifndef BLE_SECURE_H
 #define BLE_SECURE_H
 
 #include <Arduino.h>
 #include <BTstackLib.h>
 #include "btstack_event.h"
 #include "bluetooth.h"
 #include "ble/sm.h"
 #include "BluetoothLock.h"
 #include "gap.h"
 // We don't need to include BluetoothHCI.h since we'll use other methods
 
 // Security levels
 typedef enum {
     SECURITY_LOW = 0,          // No encryption, no authentication
     SECURITY_MEDIUM = 1,       // Encryption, no MITM protection (Just Works)
     SECURITY_HIGH = 2,         // Encryption with MITM protection
     SECURITY_HIGH_SC = 3       // Encryption with MITM protection and Secure Connections
 } BLESecurityLevel;
 
 // Note: We use the io_capability_t enum from bluetooth.h directly
 
 // Pairing status
 typedef enum {
     PAIRING_IDLE = 0,
     PAIRING_STARTED = 1,
     PAIRING_COMPLETE = 2,
     PAIRING_FAILED = 3
 } BLEPairingStatus;
 
 class BLESecureClass {
 public:
     BLESecureClass();
     
     // Initialize the security manager
     void begin(io_capability_t ioCapability = IO_CAPABILITY_DISPLAY_YES_NO);
     
     // Set the security level for connections
     void setSecurityLevel(BLESecurityLevel level, bool enableBonding = true);
     
     // Allow LTK reconstruction without a device DB entry (for peripheral role)
     void allowReconnectionWithoutDatabaseEntry(bool allow);
     
     // Set fixed passkey (0-999999) or NULL for random
     void setFixedPasskey(uint32_t passkey);
     
     // Request pairing when a device connects
     void requestPairingOnConnect(bool enable);
     
     // Manually request pairing with a connected device
     bool requestPairing(BLEDevice* device);
     
     // Bond with a device (store keys for reconnection)
     bool bondWithDevice(BLEDevice* device);
     
     // Remove bonding information for a device
     bool removeBonding(BLEDevice* device);
     
     // Remove all stored bonding information
     void clearAllBondings();
     
     // Callback for handling passkey display
     void setPasskeyDisplayCallback(void (*callback)(uint32_t passkey));
     
     // Callback for handling passkey entry requests
     void setPasskeyEntryCallback(void (*callback)(void));
     
     // Set passkey for entry method (call this from the passkey entry callback)
     void setEnteredPasskey(uint32_t passkey);
     
     // Callback for pairing status updates
     void setPairingStatusCallback(void (*callback)(BLEPairingStatus status, BLEDevice* device));
     
     // Callback for numeric comparison (call acceptNumericComparison from this)
     void setNumericComparisonCallback(void (*callback)(uint32_t passkey, BLEDevice* device));
     
     // Accept or reject numeric comparison
     void acceptNumericComparison(bool accept);
     
     // Get the current pairing status
     BLEPairingStatus getPairingStatus();
     
     // Get the encryption status for a connection
     bool isEncrypted(BLEDevice* device);
     
     // Process security manager events - should be called from the main event handler
     void handleSMEvent(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size);
     
     // Flag to indicate if pairing should be automatically requested on connect
     bool _requestPairingOnConnect;
     
 private:
     BLEPairingStatus _pairingStatus;
     BLESecurityLevel _securityLevel;
     io_capability_t _ioCapability;
     uint32_t _fixedPasskey;
     bool _useFixedPasskey;
     bool _bondingEnabled;
     
     // Callback functions
     void (*_passkeyDisplayCallback)(uint32_t passkey);
     void (*_passkeyEntryCallback)(void);
     void (*_pairingStatusCallback)(BLEPairingStatus status, BLEDevice* device);
     void (*_numericComparisonCallback)(uint32_t passkey, BLEDevice* device);
     
     // Store the current device handle for callbacks
     hci_con_handle_t _currentDeviceHandle;
     
     // Register for Security Manager events
     void setupSMEventHandler();
 };
 
 extern BLESecureClass BLESecure;
 
 #endif // BLE_SECURE_H