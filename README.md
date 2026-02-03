# Patro ESP32 Handheld RFID Reader/Writer

Firmware for a versatile handheld RFID reader and writer based on the ESP32 and MFRC522 module. The project features a robust, multi-tasking architecture using FreeRTOS, a modular codebase for easy maintenance, and a comprehensive JSON-based command and response protocol over **Bluetooth Low Energy (BLE)**.

![PatroRFID Project Photo](PatroRFID.png)

## ✨ Features

- **Dual Read/Write Modes:** Seamlessly switch between reading existing tags and writing new data.
- **Bluetooth Low Energy (BLE):** Modern, low-power wireless communication for connecting to a wide range of devices like smartphones and PCs.
- **Remote Device Control:** Change operational modes and toggle audible feedback remotely via BLE commands.
- **Advanced JSON Protocol:** A well-defined API for sending commands (e.g., change mode, write data) and receiving structured responses (e.g., read results, write confirmations).
- **Multi-Tasking Architecture:** Built on FreeRTOS to concurrently handle RFID operations, BLE communication, and user interface feedback without blocking.
- **Configurable User Feedback:** Provides both audible (buzzer) and visual (LED) feedback for operations, with sound being remotely configurable.
- **Modular Codebase:** The source code is organized into logical modules (BLE, RFID, UI), making it clean, scalable, and easy to maintain or extend.

## ⚙️ Hardware Specifications

- 1 x ESP32 Development Board
- 1 x MFRC522 RFID Reader Module (13.56MHz)
- 1 x Push Button, Momentary
- 1 x Passive Buzzer
- 1 x 5mm LED (any color)
- 1 x 220-330 Ohm Resistor
- Breadboard and Jumper Wires

## 🔌 Wiring Diagram

| Component        | ESP32 Pin |
| :--------------- | :-------- |
| **MFRC522 SDA**  | `GPIO 5`  |
| **MFRC522 SCK**  | `GPIO 18` |
| **MFRC522 MOSI** | `GPIO 23` |
| **MFRC522 MISO** | `GPIO 19` |
| **MFRC522 RST**  | `GPIO 4`  |
| **Buzzer (+)**   | `GPIO 22` |
| **Button**       | `GPIO 21` |
| **LED (+)**      | `GPIO 2`  |
| **VCC (3.3V)**   | `3V3`     |
| **GND**          | `GND`     |

_Note: The Button is configured with an internal pull-up resistor. The LED's anode connects to the GPIO pin via the current-limiting resistor._

## 💻 Software & Deployment

This project is configured for the **PlatformIO** ecosystem within Visual Studio Code.

1.  **Environment:** Ensure the [PlatformIO IDE extension](https://platformio.org/platformio-ide) is installed in VS Code.
2.  **Dependencies:** The required libraries (`MFRC522`, `ArduinoJson`) are listed in the `platformio.ini` file and will be installed automatically by PlatformIO upon the first build.
3.  **Build & Upload:**
    - Clone the repository.
    - Open the project folder in VS Code.
    - Use the PlatformIO toolbar to **Upload** the firmware to the ESP32.

## 📖 Operational Guide

1.  **Power On:** Apply power to the ESP32. The status LED will blink slowly, indicating it is advertising and ready for a BLE connection.
2.  **Connect via BLE:** Use a BLE scanner application (like nRF Connect, LightBlue, or a custom app) to connect to the device named `PatroRFID-Reader`. Once connected, the LED will stop blinking.
3.  **Subscribe to Notifications:** To receive data from the reader, you must subscribe to notifications on the device's characteristic.

### Reading a Tag

- By default, the device is in Read Mode.
- Press and hold the push button. The status LED will illuminate, indicating the RFID reader is active.
- Present a compatible RFID tag to the MFRC522 antenna.
- A successful read triggers a beep (if sound is enabled), and a `readResult` JSON payload is sent via BLE notification.

### Writing to a Tag

1.  **Enter Write Mode:** Send the `changeMode` command to switch the device to write mode.
2.  **Send Data:** Send the `writeData` command containing the string you wish to write.
3.  **Present Tag:** Present a compatible RFID tag to the antenna. The device will automatically attempt to write the data.
4.  **Receive Confirmation:** The device will send a `writeResult` JSON payload confirming the success or failure of the operation.
5.  **Exit Write Mode:** Send the `changeMode` command again to return to read mode.

## 📶 Communication Protocol (API)

Communication is handled via a single BLE characteristic that accepts JSON commands and sends JSON responses/data via notifications.

### Client → Device (Commands)

#### 1. Change Operational Mode

Switches between read and write modes.

```json
{
  "type": "changeMode",
  "content": "write"
}
```

- `content` can be `"write"` to activate write mode or `"stop"` to return to the default read mode.

#### 2. Send Data for Writing

Provides the data to be written to the next tag presented (must be in write mode).

```json
{
  "type": "writeData",
  "content": "Hello World"
}
```

- `content` is a string (up to 15 characters will be written).

#### 3. Toggle Sound

Enables or disables the audible buzzer feedback.

```json
{
  "type": "toggleSound",
  "content": "off"
}
```

- `content` can be `"on"` or `"off"`.

### Device → Client (Responses & Data)

#### 1. RFID Read Result

Sent after a read attempt is made.

- **Success:**

  ```json
  {
    "type": "readResult",
    "content": {
      "status": "ok",
      "uid": "A1:B2:C3:D4",
      "data": "Contents of Tag"
    }
  }
  ```

- **Failure:**
  ```json
  {
    "type": "readResult",
    "content": {
      "status": "error",
      "message": "Tag access error"
    }
  }
  ```

#### 2. RFID Write Result

Sent after a write attempt is made.

- **Success:**
  ```json
  {
    "type": "writeResult",
    "content": {
      "status": "ok",
      "uid": "A1:B2:C3:D4",
      "data": "Contents writed on Tag"
    }
  }
  ```
- **Failure:**
  ```json
  {
    "type": "writeResult",
    "content": {
      "status": "error",
      "message": "Authentication Failed"
    }
  }
  ```

#### 3. General Feedback

Sent in response to commands like changing mode or toggling sound.

```json
{
  "type": "feedback",
  "content": {
    "status": "ok",
    "mode": "write",
    "message": "Write mode activated"
  }
}
```

## 🏗️ Code Structure

The firmware is organized into a clean, modular architecture to promote separation of concerns:

- `main.cpp`: The main entry point, responsible for initialization and task creation.
- `config.h`: Centralized definitions for hardware pins and constants.
- `rtos_comm.h`: Declares all shared global variables and FreeRTOS primitives.
- `ble_comm.cpp / .h`: Manages all BLE functionality.
- `rfid_handler.cpp / .h`: Contains the FreeRTOS tasks for RFID reading and writing.
- `ui_handler.cpp / .h`: Manages user feedback tasks (LED and Buzzer).

## 📄 License

This project is licensed under the MIT License.
Feel free to use it in your projects -- just make sure to credit **Luis Felipe Patrocinio** ([GitHub](https://github.com/luisfpatrocinio)).
See the [LICENSE](./LICENSE) file for full details.
