/**
 * BLESecure.cpp - Library for BLE secure pairing with encryption
 *
 * Implementation of the BLESecure class for arduino-pico core
 */

#include "BLESecure.h"
#include "BluetoothLock.h"

// Core BTstack headers for LE Device DB and GAP
#include "ble/le_device_db.h" 
// #include "gap.h" // included in BLESecure.h              
#include "hci.h" // For hci_con_handle_t

// Fallback if NVM_NUM_DEVICE_DB_ENTRIES is not directly available here.
// It's defined in btstack_config.h as 16.
#ifndef NVM_NUM_DEVICE_DB_ENTRIES
#define NVM_NUM_DEVICE_DB_ENTRIES 16
#endif

// Ensure BD_ADDR_TYPE_UNKNOWN is defined, it's usually 0xff in BTstack
#ifndef BD_ADDR_TYPE_UNKNOWN
#define BD_ADDR_TYPE_UNKNOWN 0xff
#endif

// Define the callback macros for handling btstack events with C++ class methods
#define CCALLBACKNAME _BLESECURECB
#include <ctocppcallback.h>

#define SMEVENTCB(class, cbFcn)                                                                                                                                                                                   \
    (_BLESECURECB<void(uint8_t, uint16_t, uint8_t *, uint16_t), __COUNTER__>::func = std::bind(&class ::cbFcn, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4), \
     static_cast<btstack_packet_handler_t>(_BLESECURECB<void(uint8_t, uint16_t, uint8_t *, uint16_t), __COUNTER__ - 1>::callback))

// BLESecureClass implementation
BLESecureClass::BLESecureClass() : _pairingStatus(PAIRING_IDLE),
                                   _securityLevel(SECURITY_MEDIUM),
                                   _ioCapability(IO_CAPABILITY_DISPLAY_YES_NO),
                                   _fixedPasskey(0),
                                   _useFixedPasskey(false),
                                   _requestPairingOnConnect(false),
                                   _passkeyDisplayCallback(nullptr),
                                   _passkeyEntryCallback(nullptr),
                                   _pairingStatusCallback(nullptr),
                                   _numericComparisonCallback(nullptr),
                                   _userConnectedCallback(nullptr),
                                   _userDisconnectedCallback(nullptr),
                                   _currentDeviceHandle(HCI_CON_HANDLE_INVALID),
                                   _bondingEnabled(true)
{
}

void BLESecureClass::begin(io_capability_t ioCapability)
{
    // Store the IO capability
    _ioCapability = ioCapability;

    BluetoothLock b;

    // Initialize Security Manager
    // sm_init(); // Unnecessary for arduino-pico, already done in BTstackLib

    // Set IO capabilities
    sm_set_io_capabilities(ioCapability);

    // Set default MITM protection based on IO capabilities
    bool mitm = (ioCapability != IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(mitm ? SM_AUTHREQ_MITM_PROTECTION : 0);

    // Register for Security Manager events
    setupSMEventHandler();
}

void BLESecureClass::setSecurityLevel(BLESecurityLevel level, bool enableBonding)
{
    _securityLevel = level;
    _bondingEnabled = enableBonding;

    BluetoothLock b;

    uint8_t auth_req = 0;

    switch (level)
    {
    case SECURITY_LOW:
        // No authentication or encryption
        break;

    case SECURITY_MEDIUM:
        // Encryption, no MITM
        auth_req = enableBonding ? SM_AUTHREQ_BONDING : 0;
        break;

    case SECURITY_HIGH:
        // Encryption with MITM
        auth_req = SM_AUTHREQ_MITM_PROTECTION | (enableBonding ? SM_AUTHREQ_BONDING : 0);
        break;

    case SECURITY_HIGH_SC:
        // Encryption with MITM and SC
        auth_req = SM_AUTHREQ_MITM_PROTECTION |
                   SM_AUTHREQ_SECURE_CONNECTION |
                   (enableBonding ? SM_AUTHREQ_BONDING : 0);
        break;
    }

    sm_set_authentication_requirements(auth_req);
}

void BLESecureClass::allowReconnectionWithoutDatabaseEntry(bool allow)
{
    BluetoothLock b;
    sm_allow_ltk_reconstruction_without_le_device_db_entry(allow ? 1 : 0);
}

void BLESecureClass::setFixedPasskey(uint32_t passkey)
{
    if (passkey <= 999999)
    {
        BluetoothLock b;
        _fixedPasskey = passkey;
        _useFixedPasskey = true;
        sm_use_fixed_passkey_in_display_role(_fixedPasskey);
    }
    else
    {
        _useFixedPasskey = false;
    }
}

void BLESecureClass::requestPairingOnConnect(bool enable)
{
    _requestPairingOnConnect = enable;
}

bool BLESecureClass::requestPairing(BLEDevice *device)
{
    if (!device)
        return false;

    hci_con_handle_t handle = device->getHandle();
    if (handle == HCI_CON_HANDLE_INVALID)
        return false;

    BluetoothLock b;

    // Update pairing status
    _pairingStatus = PAIRING_STARTED;
    _currentDeviceHandle = handle;

    // Callback if registered
    if (_pairingStatusCallback)
    {
        _pairingStatusCallback(_pairingStatus, device);
    }

    // Request pairing
    sm_request_pairing(handle);
    return true;
}

bool BLESecureClass::bondWithDevice(BLEDevice *device)
{
    if (!device)
        return false;

    // For BTstack, bonding is specified by the authentication requirements
    // which is now controlled by the _bondingEnabled flag in setSecurityLevel

    // Temporarily enable bonding if it's disabled
    bool originalBondingState = _bondingEnabled;
    if (!_bondingEnabled)
    {
        // Re-apply security level with bonding enabled
        setSecurityLevel(_securityLevel, true);
    }

    // Request pairing which will now bond
    bool result = requestPairing(device);

    // Restore original bonding state if we changed it
    if (!originalBondingState)
    {
        setSecurityLevel(_securityLevel, false);
    }

    return result;
}

bool BLESecureClass::removeBonding(BLEDevice *device) {
    if (!device) {
        Serial.println("removeBonding: BLEDevice object is NULL.");
        return false;
    }

    hci_con_handle_t handle = device->getHandle();
    if (handle == HCI_CON_HANDLE_INVALID) {
        Serial.println("removeBonding: Invalid connection handle from BLEDevice.");
        return false;
    }

    Serial.println("Attempting to remove bonding for specific device.");
    BluetoothLock b;

    int device_db_index = sm_le_device_index(handle);

    if (device_db_index < 0) {
        Serial.print("removeBonding: Device not found in LE Device DB (sm_le_device_index returned ");
        Serial.print(device_db_index);
        Serial.println("). Cannot get address to remove bond. It might not be bonded or not connected.");
        return false;
    }

    Serial.print("removeBonding: Found device in LE DB at index: ");
    Serial.println(device_db_index);

    int addr_type_int;
    bd_addr_t addr;
    le_device_db_info(device_db_index, &addr_type_int, addr, NULL /* irk */);
    bd_addr_type_t current_addr_type = (bd_addr_type_t)addr_type_int;

    if (current_addr_type == BD_ADDR_TYPE_LE_PUBLIC || current_addr_type == BD_ADDR_TYPE_LE_RANDOM) {
        Serial.print("removeBonding: Calling gap_delete_bonding for AddrType: ");
        Serial.print(current_addr_type);
        Serial.print(", Addr: ");
        Serial.println(bd_addr_to_str(addr));

        gap_delete_bonding(current_addr_type, addr); 

        Serial.println("removeBonding: gap_delete_bonding called. Verifying DB state:");
        le_device_db_dump(); 

        int current_db_count = le_device_db_count();
        Serial.print("removeBonding: le_device_db_count() after gap_delete_bonding: ");
        Serial.println(current_db_count);


        Serial.println("removeBonding: Disconnecting device.");
        gap_disconnect(handle); 

        return true; 
    } else {
        Serial.print("removeBonding: Could not retrieve a valid LE address type for device at DB index ");
        Serial.print(device_db_index);
        Serial.print(" (type was "); Serial.print(current_addr_type); Serial.println("). Bond not removed.");
        return false;
    }
}

void BLESecureClass::clearAllBondings() {
    Serial.println(">>> Using gap_delete_bonding w/ Dumps <<<"); 
    BluetoothLock b;

    Serial.println("Initial LE Device DB Dump (before any deletions):");
    le_device_db_dump(); // DUMP 1: See initial state

    int initial_bond_count = le_device_db_count();
    Serial.print("Found ");
    Serial.print(initial_bond_count);
    Serial.println(" bonded device(s) reported by le_device_db_count(). Attempting to delete via GAP API...");

    if (initial_bond_count > 0) {
        int bonds_deleted_count = 0;
        // Iterate through all possible physical slots in the LE Device DB
        for (int slot_index = 0; slot_index < NVM_NUM_DEVICE_DB_ENTRIES; ++slot_index) {
            int addr_type_int;
            bd_addr_t addr;

            // Get info for the bond potentially at physical slot 'slot_index'
            le_device_db_info(slot_index, &addr_type_int, addr, NULL /* irk */);
            bd_addr_type_t current_addr_type = (bd_addr_type_t)addr_type_int;

            // Check if the slot contains a valid LE bond entry
            // Valid LE public or random addresses. BD_ADDR_TYPE_UNKNOWN (0xff) means empty.
            if (current_addr_type == BD_ADDR_TYPE_LE_PUBLIC || current_addr_type == BD_ADDR_TYPE_LE_RANDOM) {
                Serial.print("Slot "); Serial.print(slot_index);
                Serial.print(": Found valid LE device to delete - AddrType: "); Serial.print(current_addr_type);
                Serial.print(", Addr: "); Serial.println(bd_addr_to_str(addr));

                gap_delete_bonding(current_addr_type, addr);
                bonds_deleted_count++;
                Serial.println("Called gap_delete_bonding(). DB state after this call (dump may not reflect immediate flash change):");
                le_device_db_dump(); // DUMP 2: Inside loop, after a gap_delete_bonding call
            }
        }
        Serial.print("Called gap_delete_bonding() for ");
        Serial.print(bonds_deleted_count);
        Serial.println(" potential entries based on initial scan of all slots.");
    } else {
        Serial.println("No bonds reported by le_device_db_count() initially.");
    }

    Serial.println("Final LE Device DB Dump (after all gap_delete_bonding attempts):");
    le_device_db_dump(); // DUMP 3: After the loop

    int final_count = le_device_db_count();
    if (final_count == 0) {
        Serial.println("SUCCESS: All bondings appear cleared (le_device_db_count is 0).");
    } else {
        Serial.print("INFO: After all attempts, ");
        Serial.print(final_count);
        Serial.println(" bond(s) still reported by le_device_db_count. Initial count was " + String(initial_bond_count));
        Serial.println("This might indicate that gap_delete_bonding did not fully clear all entries from SM perspective or TLV backend.");
    }
    // The re-application of SM settings will be handled by BLESecure.begin()/setSecurityLevel()
    // called from the main.cpp's BOOTSEL logic AFTER this function returns.
}

void BLESecureClass::setPasskeyDisplayCallback(void (*callback)(uint32_t passkey))
{
    _passkeyDisplayCallback = callback;
}

void BLESecureClass::setPasskeyEntryCallback(void (*callback)(void))
{
    _passkeyEntryCallback = callback;
}

void BLESecureClass::setEnteredPasskey(uint32_t passkey)
{
    if (_pairingStatus == PAIRING_STARTED && _currentDeviceHandle != HCI_CON_HANDLE_INVALID)
    {
        BluetoothLock b;
        sm_passkey_input(_currentDeviceHandle, passkey);
    }
}

void BLESecureClass::setPairingStatusCallback(void (*callback)(BLEPairingStatus status, BLEDevice *device))
{
    _pairingStatusCallback = callback;
}

void BLESecureClass::setNumericComparisonCallback(void (*callback)(uint32_t passkey, BLEDevice *device))
{
    _numericComparisonCallback = callback;
}

void BLESecureClass::acceptNumericComparison(bool accept)
{
    if (_pairingStatus == PAIRING_STARTED && _currentDeviceHandle != HCI_CON_HANDLE_INVALID)
    {
        BluetoothLock b;
        // The arduino-pico implementation accepts only the connection handle
        // The 'accept' parameter is ignored as the implementation always confirms
        sm_numeric_comparison_confirm(_currentDeviceHandle);
    }
}

BLEPairingStatus BLESecureClass::getPairingStatus()
{
    return _pairingStatus;
}

bool BLESecureClass::isEncrypted(BLEDevice *device)
{
    if (!device)
        return false;

    hci_con_handle_t handle = device->getHandle();
    if (handle == HCI_CON_HANDLE_INVALID)
        return false;

    BluetoothLock b;
    return gap_encryption_key_size(handle) > 0;
}

void BLESecureClass::setupSMEventHandler()
{
    // Register for security manager events using the standard event handler
    static btstack_packet_callback_registration_t sm_event_callback_registration;
    sm_event_callback_registration.callback = SMEVENTCB(BLESecureClass, handleSMEvent);
    sm_add_event_handler(&sm_event_callback_registration);
}

// New methods for connection/disconnection handling with auto-pairing
void BLESecureClass::setBLEDeviceConnectedCallback(void (*callback)(BLEStatus status, BLEDevice *device))
{
    _userConnectedCallback = callback;
    BTstack.setBLEDeviceConnectedCallback(internalConnectionCallback);
}

void BLESecureClass::setBLEDeviceDisconnectedCallback(void (*callback)(BLEDevice *device))
{
    _userDisconnectedCallback = callback;
    BTstack.setBLEDeviceDisconnectedCallback(internalDisconnectionCallback);
}

// Internal connection callback that handles auto-pairing
void BLESecureClass::internalConnectionCallback(BLEStatus status, BLEDevice *device)
{
    // Auto-request pairing if enabled
    if (status == BLE_STATUS_OK && BLESecure._requestPairingOnConnect)
    {
        Serial.println("Auto-requesting pairing as configured in BLESecure");
        BLESecure.requestPairing(device);
    }

    // Call the user's callback if registered
    if (BLESecure._userConnectedCallback)
    {
        BLESecure._userConnectedCallback(status, device);
    }
}

// Internal disconnection callback
void BLESecureClass::internalDisconnectionCallback(BLEDevice *device)
{
    // Reset pairing status if this was the device we were pairing with
    if (BLESecure._currentDeviceHandle == device->getHandle())
    {
        BLESecure._pairingStatus = PAIRING_IDLE;
        BLESecure._currentDeviceHandle = HCI_CON_HANDLE_INVALID;
    }

    // Call the user's callback if registered
    if (BLESecure._userDisconnectedCallback)
    {
        BLESecure._userDisconnectedCallback(device);
    }
}

void BLESecureClass::handleSMEvent(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;

    if (packet_type != HCI_EVENT_PACKET)
        return;

    switch (hci_event_packet_get_type(packet))
    {
    case SM_EVENT_JUST_WORKS_REQUEST:
    {
        // Just Works request - auto-confirm if that's our capability
        hci_con_handle_t handle = sm_event_just_works_request_get_handle(packet);
        sm_just_works_confirm(handle);
        Serial.println("Accepting Just Works pairing request");
        break;
    }

    case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
    {
        // Passkey display - pass to callback if registered
        uint32_t passkey = sm_event_passkey_display_number_get_passkey(packet);
        hci_con_handle_t handle = sm_event_passkey_display_number_get_handle(packet);

        if (_passkeyDisplayCallback)
        {
            _passkeyDisplayCallback(passkey);
        }
        Serial.print("Please enter passkey on other device: ");
        Serial.println(passkey);
        break;
    }

    case SM_EVENT_PASSKEY_INPUT_NUMBER:
    {
        // Passkey input - pass to callback if registered
        if (_passkeyEntryCallback)
        {
            _passkeyEntryCallback();
        }
        Serial.println("Passkey entry requested - use setEnteredPasskey() to provide the value");
        break;
    }

    case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
    {
        // Numeric comparison - pass to callback if registered
        uint32_t passkey = sm_event_numeric_comparison_request_get_passkey(packet);
        hci_con_handle_t handle = sm_event_numeric_comparison_request_get_handle(packet);
        BLEDevice device(handle);

        Serial.print("Numeric comparison requested. Does this match? ");
        Serial.println(passkey);

        if (_numericComparisonCallback)
        {
            _numericComparisonCallback(passkey, &device);
        }
        else
        {
            // Auto-accept if no callback
            sm_numeric_comparison_confirm(handle);
        }
        break;
    }

    case SM_EVENT_PAIRING_STARTED:
    {
        // Pairing started
        _pairingStatus = PAIRING_STARTED;
        hci_con_handle_t handle = sm_event_pairing_started_get_handle(packet);
        _currentDeviceHandle = handle;

        Serial.println("Pairing started");

        if (_pairingStatusCallback)
        {
            BLEDevice device(handle);
            _pairingStatusCallback(_pairingStatus, &device);
        }
        break;
    }

    case SM_EVENT_PAIRING_COMPLETE:
    {
        // Pairing completed
        hci_con_handle_t handle = sm_event_pairing_complete_get_handle(packet);
        BLEDevice device(handle);

        if (sm_event_pairing_complete_get_status(packet) == ERROR_CODE_SUCCESS)
        {
            _pairingStatus = PAIRING_COMPLETE;
            Serial.println("Pairing complete - success");
        }
        else
        {
            _pairingStatus = PAIRING_FAILED;
            Serial.print("Pairing failed, status: ");
            Serial.print(sm_event_pairing_complete_get_status(packet));
            Serial.print(", reason: ");
            Serial.println(sm_event_pairing_complete_get_reason(packet));
        }

        if (_pairingStatusCallback)
        {
            _pairingStatusCallback(_pairingStatus, &device);
        }

        _currentDeviceHandle = HCI_CON_HANDLE_INVALID;
        break;
    }

    case SM_EVENT_REENCRYPTION_STARTED:
    {
        // Re-encryption with previously bonded device
        // This happens when reconnecting to a bonded device
        _pairingStatus = PAIRING_STARTED;
        hci_con_handle_t handle = sm_event_reencryption_started_get_handle(packet);
        _currentDeviceHandle = handle;

        Serial.println("Re-encryption started with bonded device");

        if (_pairingStatusCallback)
        {
            BLEDevice device(handle);
            _pairingStatusCallback(_pairingStatus, &device);
        }
        break;
    }

    case SM_EVENT_REENCRYPTION_COMPLETE:
    {
        // Re-encryption complete
        hci_con_handle_t handle = sm_event_reencryption_complete_get_handle(packet);
        BLEDevice device(handle);

        if (sm_event_reencryption_complete_get_status(packet) == ERROR_CODE_SUCCESS)
        {
            _pairingStatus = PAIRING_COMPLETE;
            Serial.println("Re-encryption complete - success");
        }
        else
        {
            _pairingStatus = PAIRING_FAILED;
            Serial.print("Re-encryption failed, status: ");
            Serial.println(sm_event_reencryption_complete_get_status(packet));
        }

        if (_pairingStatusCallback)
        {
            _pairingStatusCallback(_pairingStatus, &device);
        }

        _currentDeviceHandle = HCI_CON_HANDLE_INVALID;
        break;
    }
    }
}

// Create a global instance
BLESecureClass BLESecure;