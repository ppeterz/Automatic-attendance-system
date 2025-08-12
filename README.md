
# Automatic Attendance System

This project provides a C++-based automatic attendance system using the ESP8266 microcontroller. The system is designed to capture attendance data via hardware connections (such as RFID, fingerprint, or other sensors), then record the data automatically to a Google Sheet through WiFi. This guide details hardware setup, software installation, and configuration to ensure a seamless deployment.

---

---

## Overview

The Automatic Attendance System leverages ESP8266 WiFi capabilities to record attendance events and update them to a remote Google Sheet. The system is ideal for classrooms, offices, or events requiring real-time, digital attendance tracking.

---

## Hardware Requirements

- **ESP8266 Board** (e.g., NodeMCU, Wemos D1 Mini)
- **Sensor Module** (e.g., RFID reader, fingerprint sensor, etc.)
- **Breadboard and Jumper Wires**
- **Power Source** (Micro USB cable or regulated 5V supply)
- **(Optional) Indicator LEDs, buzzer**

---

## Wiring and Circuit Connection

**Follow the schematic/image provided in the repository** (refer to the circuit diagram in the project files for exact connections).

### Example: RFID Module with ESP8266

| RFID Pin | ESP8266 Pin |
|----------|-------------|
| VCC      | 3V3         |
| GND      | GND         |
| RST      | D3          |
| SDA      | D2          |
| SCK      | D5          |
| MOSI     | D7          |
| MISO     | D6          |

- Connect the sensor module to the ESP8266 as per the above table.
- Double-check each connection to avoid damaging the components.

**Note:** If using a different sensor, refer to its datasheet and the image in the repository for correct pin mapping.

---

## Software Requirements

- **Arduino IDE** (version 1.8.x or newer)
- **ESP8266 Board support for Arduino**
- **Required Libraries:**
  - `ESP8266WiFi`
  - `WiFiClientSecure`
  - Sensor-specific library (e.g., `MFRC522` for RFID)
  - Google Sheets communication library (e.g., [HTTPSRedirect](https://github.com/burnsra/HTTPSRedirect) or [ESP8266 Google Sheets Logger](https://github.com/StorageB/ESP8266-google-script))

---

## Arduino IDE Setup

1. **Install Arduino IDE**
   - Download from [Arduino website](https://www.arduino.cc/en/software).
2. **Add ESP8266 Board Support**
   - Go to `File` → `Preferences`.
   - In "Additional Boards Manager URLs", add:  
     ```
     http://arduino.esp8266.com/stable/package_esp8266com_index.json
     ```
   - Go to `Tools` → `Board` → `Boards Manager…`, search for `ESP8266`, and install it.

---

## Library Installation

Install required libraries using Arduino IDE's Library Manager:

1. Go to `Sketch` → `Include Library` → `Manage Libraries…`
2. Search and install:
   - `ESP8266WiFi`
   - `WiFiClientSecure`
   - Sensor library (e.g., `MFRC522`)
   - `HTTPSRedirect` or another Google Sheets logger library

Or, install via ZIP files if needed (`Sketch > Include Library > Add .ZIP Library...`).

---

## Code Uploading

1. Open the project `.ino` file in Arduino IDE.
2. Select your board under `Tools > Board > NodeMCU 1.0 (ESP8266)` or the appropriate ESP8266 variant.
3. Select the correct port under `Tools > Port`.
4. Connect your ESP8266 via USB.
5. Click the "Upload" button (right arrow) to flash the code.

**Note:** If you encounter upload errors, check your board selection, drivers, and USB connection.

---

## Google Sheets Integration

To log attendance data to Google Sheets:

1. **Google Script Setup:**
   - Create a new Google Sheet.
   - Go to `Extensions > Apps Script`.
   - Copy a Google Apps Script (see example below) to accept HTTP POST requests and write data to the sheet.
   - Deploy the script as a web app (set access as "Anyone, even anonymous").
   - Copy the script's web app URL.

2. **Arduino Code Configuration:**
   - In your Arduino sketch, set the Google Script web app URL.
   - Use the required library (`HTTPSRedirect`, etc.) to send attendance data (e.g., student ID, timestamp) via HTTPS POST.

**Example Google Apps Script:**
```javascript
function doPost(e){
  var ss = SpreadsheetApp.openById('GOOGLE_SHEET_ID');
  var sheet = ss.getSheetByName('Sheet1');
  var data = JSON.parse(e.postData.contents);
  sheet.appendRow([data.student_id, data.timestamp]);
  return ContentService.createTextOutput("Success");
}
```

**Replace `GOOGLE_SHEET_ID` with your sheet's ID, and update your Arduino code with the correct script URL.**

---

## Usage

1. Power on the ESP8266 and sensor module.
2. The device will connect to your WiFi network (configure SSID and password in the code).
3. When a user interacts with the sensor (e.g., presents an RFID card), their data is sent to Google Sheets.
4. Check your Google Sheet to verify attendance records.

---

## Troubleshooting

- **Cannot upload code:** Check USB cable, drivers, and board selection.
- **WiFi not connecting:** Verify SSID/password, signal strength, and router compatibility.
- **Attendance not logged:** Confirm Google Script deployment and permissions, and check Arduino serial output for errors.
- **Sensor not detected:** Check wiring and install the correct sensor library.

---

## Contributing

Contributions are welcome! Please fork the repository and submit pull requests for improvements or additional sensor support.

---



## Contact

For questions or support, open an issue on GitHub or contact the repository owner.

---

**References:**
- [ESP8266 Arduino Core](https://github.com/esp8266/Arduino)
- [Google Apps Script Documentation](https://developers.google.com/apps-script)
- [HTTPSRedirect Library](https://github.com/burnsra/HTTPSRedirect)
