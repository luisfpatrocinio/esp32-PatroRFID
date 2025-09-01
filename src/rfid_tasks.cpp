#include "rfid_tasks.h"
#include <ArduinoJson.h>
#include <BLE2902.h>

extern BLECharacteristic *pCharacteristic;

//==============================================================================
// RFID READER TASK
//==============================================================================
/**
 * @brief Checks for button press and reads RFID tags.
 * @param parameter Unused task parameter.
 * @note Reads UID and block 4 data, serializes as JSON, sends to BLE queue and triggers buzzer.
 */
