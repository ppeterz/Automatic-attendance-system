#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <EEPROM.h>
#include <HTTPSRedirect.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

// Pin definitions
#define SS_PIN D4   // RFID SS (SDA) pin (GPIO 0)
#define RST_PIN D3  // RFID RST pin (GPIO 2)
#define SIGN_IN_LED D0   // Green LED (GPIO 16)
#define SIGN_OUT_LED D8  // Red LED (GPIO 12)
#define ERROR_LED D8     // Error LED (GPIO 13)

// Google Script Deployment ID
const char* GScriptId ="AKfycbxdALbPeCePCRjbbmYjIq1XuI8BlXLzXweTUMfjo5bmEKp0vRby9hPArxlOgFl8Z_5nxQ";
const char* host = "script.google.com";
const int httpsPort = 443;
String url = String("/macros/s/") + GScriptId + "/exec";

// NTP setup for Africa/Lagos (UTC+1)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000);

// EEPROM configuration
#define EEPROM_SIZE 4096
#define MAX_EMPLOYEES_PER_DAY 20
#define MAX_DAYS_STORAGE 2
#define EMPLOYEE_RECORD_SIZE sizeof(Employee)
#define DAY_RECORD_SIZE (sizeof(DayInfo) + (MAX_EMPLOYEES_PER_DAY * EMPLOYEE_RECORD_SIZE) + sizeof(uint32_t))
#define EEPROM_HEADER_SIZE 50
#define EEPROM_DATA_START EEPROM_HEADER_SIZE

// LCD setup
LiquidCrystal_I2C lcd(0x27, 16, 2); // I2C address 0x27, 16x2 display

// Data structures
struct DayInfo {
  char date[12];        // e.g., "2025-06-16"
  int employeeCount;    // 0 to MAX_EMPLOYEES_PER_DAY
  bool isValid;         // Validity flag
};

struct Employee {
  char employeeId[20];  // e.g., "EM002"
  char surname[30];     // e.g., "Aderogba"
  char firstName[30];   // e.g., "Olajide"
  char signInTime[10];  // e.g., "20:37:20"
  char signOutTime[10]; // e.g., "23:59:59"
  char date[12];        // e.g., "2025-06-16"
  bool signedIn;        // True if currently signed in
  bool hasSignedOut;    // True if signed out
  unsigned long lastScanTime; // Millis of last scan
  bool isValid;         // Validity flag
};

struct DayRecord {
  DayInfo dayInfo;
  Employee employees[MAX_EMPLOYEES_PER_DAY];
  uint32_t checksum;    // Data integrity checksum
};

// Global variables
MFRC522 mfrc522(SS_PIN, RST_PIN);
HTTPSRedirect* client = nullptr;
Employee dailyAttendance[MAX_EMPLOYEES_PER_DAY];
int attendanceCount = 0;
DayInfo storedDays[MAX_DAYS_STORAGE];
int storedDaysCount = 0;
String currentDate = "";
bool isOnlineMode = false;
bool needsSync = false;
unsigned long lastCardScanTime = 0;
const unsigned long SCAN_TIMEOUT = 3000; // 3 seconds
unsigned long lastSyncAttempt = 0;
const unsigned long SYNC_INTERVAL = 300000; // 5 minutes
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 60000; // 1 minute
bool dateIsAccurate = false;
unsigned long bootTime = 0;

// LCD control variables
enum LCDState { LCD_DEFAULT, LCD_CARD_DETECTED, LCD_WELCOME, LCD_GOODBYE, LCD_ERROR, LCD_SHOW };
LCDState lcdState = LCD_DEFAULT;
unsigned long lcdMessageStartTime = 0;
const unsigned long LCD_MESSAGE_DURATION = 4000; // 4 seconds
const unsigned long LCD_CARD_DETECT_DURATION = 1000; // 1 second
String lcdMessage = "";
bool lcdNeedsUpdate = true;

// Function declarations
void connectToWiFi();
void initializeHTTPSClient();
bool readEmployeeData(Employee& emp);
void bufferToString(byte* buffer, int length, char* output, int outputSize);
void processAttendance(Employee& emp);
int findEmployeeIndex(const char* employeeId);
bool hasEmployeeSignedOutToday(const char* employeeId);
void saveToEEPROM();
void loadFromEEPROM();
int findOrCreateDayIndex(const String& date);
bool isEEPROMFull();
void syncEEPROMData();
bool sendEmployeeDataToSheetsWithRetry(const Employee& emp);
void updateEEPROMAfterSync();
void handlePeriodicSync();
bool validateEEPROMData();
void sendToGoogleSheets(const Employee& emp, const String& action);
void sendSignOutToGoogleSheets(const Employee& emp, const String& signOutTime);
void updateDateTime();
void setCurrentDateOffline();
String getCurrentTime();
void checkNewDay();
void createNewSheet();
void flashLED(int pin, int count);
void loadTodaysAttendanceFromSheets();
void parseSheetResponse(const String& response);
void clearEEPROM();
bool isEmployeeValid(const Employee& emp);
uint32_t calculateChecksum(const DayRecord& record);
void updateLCD(LCDState state, const String& line1, const String& line2, const Employee* emp);
void sendPLXDAQData(const Employee& emp, const String& action, const String& time);

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== RFID Attendance System Starting ===");

  // Initialize PLX-DAQ
  Serial.println("CLEARDATA");
  Serial.println("LABEL,Date,EmployeeID,Surname,FirstName,SignInTime,SignOutTime,Action");
  Serial.println("MSG,RFID Attendance System Started");

  // Initialize LCD with debug
  Wire.begin(D2, D1); // SDA=D2 (GPIO 4), SCL=D1 (GPIO 5)
  lcd.begin();
  lcd.backlight();
  Serial.println("LCD Initialized");
  delay(1000); // Allow initialization to settle
  updateLCD(LCD_SHOW, "LCD Test", "", nullptr);
  Serial.println("LCD Test Displayed");
  delay(2000);

  // Show WiFi connection status
  updateLCD(LCD_SHOW, "Connecting WiFi...", "", nullptr);
  Serial.println("Displaying: Connecting WiFi...");
  delay(500); // Ensure message displays

  // Initialize pins
  pinMode(SIGN_IN_LED, OUTPUT);
  pinMode(SIGN_OUT_LED, OUTPUT);
  pinMode(ERROR_LED, OUTPUT);
  digitalWrite(SIGN_IN_LED, LOW);
  digitalWrite(SIGN_OUT_LED, LOW);
  digitalWrite(ERROR_LED, LOW);

  // Initialize SPI and MFRC522
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println("MFRC522 Initialized");

  // Connect to WiFi
  connectToWiFi();

  // Update LCD for Sheets connection if online
  if (isOnlineMode) {
    delay(500);
    Serial.println("Displaying: Connecting Sheets...");
    delay(500); // Ensure message displays
    updateLCD(LCD_SHOW, "Connecting Sheets...", "", nullptr);
    initializeHTTPSClient();
    timeClient.begin();
    timeClient.setTimeOffset(3600);
    timeClient.forceUpdate();
    updateDateTime();
    dateIsAccurate = true;
    updateLCD(LCD_SHOW, "Init", "", nullptr);
    loadTodaysAttendanceFromSheets();
  } else {
    setCurrentDateOffline();
    dateIsAccurate = false;
  }

  // Show initialization complete
  updateLCD(LCD_SHOW, "Init Complete", "", nullptr);
  Serial.println("Displaying: Init Complete");
  delay(2000); // Display for 2 seconds

  // Switch to default screen
  updateLCD(LCD_DEFAULT, "", "", nullptr);
  Serial.println("Switched to Default Screen");

  Serial.println("=== System Ready ===");
  Serial.println(isOnlineMode ? "Mode: ONLINE" : "Mode: OFFLINE");
  Serial.println("Present RFID card to record attendance");
  Serial.println("MSG,System Ready - Present RFID Card");
}

void loop() {
  static bool syncInProgress = false;
  yield();
  ESP.wdtFeed();

  // Update LCD if state has timed out
  if (lcdState != LCD_DEFAULT && millis() - lcdMessageStartTime >
      (lcdState == LCD_CARD_DETECTED ? LCD_CARD_DETECT_DURATION : LCD_MESSAGE_DURATION)) {
    updateLCD(LCD_DEFAULT, "", "", nullptr);
  }

  if (isOnlineMode) {
    static unsigned long lastTimeUpdate = 0;
    if (millis() - lastTimeUpdate > 60000) {
      updateDateTime();
      lastTimeUpdate = millis();
    }
    if (!syncInProgress) {
      handlePeriodicSync();
      syncInProgress = needsSync;
    }
  } else {
    if (millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
      updateLCD(LCD_SHOW, "Reconnecting...", "", nullptr);
      Serial.println("Attempting WiFi reconnection...");
      Serial.println("MSG,Attempting WiFi Reconnection");

      if (WiFi.status() == WL_CONNECTED) {
        updateLCD(LCD_SHOW, "WiFi", "Reconnected...", nullptr);
        Serial.println("WiFi reconnected! Switching to online mode...");
        Serial.println("MSG,WiFi Reconnected");
        isOnlineMode = true;
        lastReconnectAttempt = millis();
        timeClient.begin();
        updateDateTime();
        initializeHTTPSClient();
        // Trigger sync immediately after reconnection
        if (needsSync) {
          Serial.println("Initiating sync after WiFi reconnection...");
          Serial.println("MSG,Sync After Reconnection Started");
          syncEEPROMData();
          lastSyncAttempt = millis(); // Reset sync timer
        }
      }
      lastReconnectAttempt = millis();
      updateLCD(LCD_DEFAULT, "", "", nullptr);
    }
  }

  checkNewDay();

  if (syncInProgress) {
    yield();
    return;
  }

  // Check for new card
  if (!mfrc522.PICC_IsNewCardPresent()) {
    yield();
    return;
  }

  // Read card serial
  if (!mfrc522.PICC_ReadCardSerial()) {
    yield();
    return;
  }

  // Check scan timeout
  if (millis() - lastCardScanTime < SCAN_TIMEOUT) {
    Serial.println("Wait 3 seconds between scans");
    Serial.println("MSG,Scan Too Fast");
    updateLCD(LCD_ERROR, "Scan Too Fast", "'", nullptr);
    flashLED(ERROR_LED, 2);
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    yield();
    return;
  }

  Serial.println("\n--- Card Detected ---");
  Serial.println("MSG,Card Detected");

  Employee emp;
  if (readEmployeeData(emp)) {
    Serial.println("Employee: ID=" + String(emp.employeeId) + ", Name=" + String(emp.firstName) + " " + String(emp.surname));

    // Show card detected with employee name
    updateLCD(LCD_CARD_DETECTED, "", "", &emp);
    delay(1500); // Show name for 1.5 seconds

    processAttendance(emp);
    lastCardScanTime = millis();
  } else {
    Serial.println("Error reading employee data");
    Serial.println("MSG,Card Read Error");
    updateLCD(LCD_ERROR, "Invalid card", "", nullptr);
    flashLED(ERROR_LED, 3);
  }

  // Ensure card is halted and crypto stopped
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  yield();
  ESP.wdtFeed();
}

void connectToWiFi() {
  WiFiManager wifiManager;
  wifiManager.setTimeout(180);
  Serial.println("Starting WiFiManager... Connect to AP 'RFID_Attendance' to configure WiFi");
  Serial.println("MSG,Starting WiFi Configuration");
  updateLCD(LCD_SHOW, "Starting WiFi", "Configuration", nullptr);
  if (!wifiManager.autoConnect("RFID_Attendance")) {
    Serial.println("Failed to connect to WiFi and hit timeout");
    Serial.println("MSG,WiFi Connection Failed");
    updateLCD(LCD_ERROR, "WiFi Failed", "", nullptr);
    isOnlineMode = false;
    delay(1000);
  } else {
    Serial.println("Connected to WiFi! IP: " + WiFi.localIP().toString());
    Serial.println("MSG,WiFi Connected");
    updateLCD(LCD_SHOW, "WiFi Connected", ""+ WiFi.localIP().toString(), nullptr);
    isOnlineMode = true;
    delay(1000);
  }
}

void initializeHTTPSClient() {
  if (client) {
    delete client;
    client = nullptr;
    Serial.println("Deleted previous HTTPS client");
  }

  client = new HTTPSRedirect(httpsPort);
  if (!client) {
    Serial.println("Failed to allocate HTTPS client - OOM");
    Serial.println("MSG,Client Allocation Failed");
    updateLCD(LCD_ERROR, "OOM Error", "", nullptr);
    return;
  }

  client->setInsecure();
  client->setPrintResponseBody(true);
  client->setContentTypeHeader("application/json");

  Serial.print("Connecting to ");
  Serial.println(host);
  Serial.println("MSG,Connecting to Google Sheets");

  bool connected = false;
  const int maxRetries = 5;
  unsigned long lastAttempt = millis();

  for (int i = 0; i < maxRetries; i++) {
    ESP.wdtFeed();
    if (client->connect(host, httpsPort)) {
      connected = true;
      updateLCD(LCD_SHOW, "Sheets Connected", "", nullptr);
      Serial.println("Connected to Google Sheets");
      Serial.println("MSG,Google Sheets Connected");

      client->setTimeout(5000);
      break;
    }
    Serial.println("Connection failed. Retrying... Attempt " + String(i + 1) + "/" + String(maxRetries));
    Serial.println("MSG,Sheets Connection Retry");
    updateLCD(LCD_SHOW, "Sheets Connection", "Retry "+ String (i + 1) + "/" + String(maxRetries), nullptr);
    delay(1500);
    yield();
  }

  if (!connected) {
    delete client;
    client = nullptr;
    Serial.println("Failed to connect to Google Sheets - OFFLINE mode");
    Serial.println("MSG,Sheets Connection Failed");
    updateLCD(LCD_SHOW, "Check: Internet ", "access", nullptr);
    delay(2000);
    isOnlineMode = false;
    updateLCD(LCD_DEFAULT, "Offline Mode", "No internet", nullptr);
    Serial.println("Check: Internet access, GScriptId, or Google Script deployment");
  }
}

bool readEmployeeData(Employee& emp) {
  Serial.println("Reading employee data...");
  MFRC522::MIFARE_Key key = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  byte sector = 1;
  byte trailerBlock = 7;
  byte buffer[18];
  byte size = sizeof(buffer);

  memset(&emp, 0, sizeof(Employee));

  // Authenticate
  Serial.println("Authenticating...");
  ESP.wdtFeed();
  if (mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid)) != MFRC522::STATUS_OK) {
    Serial.println("Authentication failed");
    Serial.println("MSG,Card Authentication Failed");
    updateLCD(LCD_ERROR, "Invalid Card", "", nullptr);
    return false;
  }

  // Read employee ID
  Serial.println("Reading employee ID...");
  ESP.wdtFeed();
  if (mfrc522.MIFARE_Read(4, buffer, &size) != MFRC522::STATUS_OK) {
    Serial.println("Read employee ID failed");
    Serial.println("MSG,Employee ID Read Failed");
    updateLCD(LCD_ERROR, "Invalid Card", "", nullptr);
    return false;
  }
  bufferToString(buffer, 16, emp.employeeId, sizeof(emp.employeeId));
  Serial.println("Employee ID: " + String(emp.employeeId));

  // Read surname
  Serial.println("Reading surname...");
  ESP.wdtFeed();
  if (mfrc522.MIFARE_Read(5, buffer, &size) != MFRC522::STATUS_OK) {
    Serial.println("Read surname failed");
    Serial.println("MSG,Surname Read Failed");
    updateLCD(LCD_ERROR, "Invalid Card", "", nullptr);
    return false;
  }
  bufferToString(buffer, 16, emp.surname, sizeof(emp.surname));
  Serial.println("Surname: " + String(emp.surname));

  // Read first name
  Serial.println("Reading first name...");
  ESP.wdtFeed();
  if (mfrc522.MIFARE_Read(6, buffer, &size) != MFRC522::STATUS_OK) {
    Serial.println("Read first name failed");
    Serial.println("MSG,First Name Read Failed");
    updateLCD(LCD_ERROR, "Invalid Card", "", nullptr);
    return false;
  }
  bufferToString(buffer, 16, emp.firstName, sizeof(emp.firstName));
  Serial.println("First Name: " + String(emp.firstName));

  // Validate data
  if (strlen(emp.employeeId) == 0 || strlen(emp.surname) == 0 || strlen(emp.firstName) == 0) {
    Serial.println("Empty data on card");
    Serial.println("MSG,Empty Card Data");
    updateLCD(LCD_ERROR, "Empty data", "On card", nullptr);
    return false;
  }

  strcpy(emp.date, currentDate.c_str());
  emp.isValid = true;
  emp.lastScanTime = millis();
  Serial.println("Employee data read successfully");
  return true;
}

void bufferToString(byte* buffer, int length, char* output, int outputSize) {
  Serial.println("Converting buffer to string...");
  int j = 0;
  for (int i = 0; i < length && j < outputSize - 1; i++) {
    ESP.wdtFeed();
    if (buffer[i] == 0) break;
    if (buffer[i] >= 32 && buffer[i] <= 126) {
      output[j++] = (char)buffer[i];
    }
  }
  output[j] = '\0';
  // Trim trailing spaces
  while (j > 0 && output[j-1] == ' ') {
    output[--j] = '\0';
  }
  Serial.println("Output: " + String(output));
}

void processAttendance(Employee& emp) {
  Serial.println("Processing attendance...");
  if (!dateIsAccurate) {
    Serial.println("WARNING: Date may be inaccurate - will correct when online");
    Serial.println("MSG,Inaccurate Date Warning");
  }

  if (!isOnlineMode) {
    Serial.println("Offline mode - attendance recording disabled");
    Serial.println("MSG,Offline Mode - No Record");
    updateLCD(LCD_ERROR, "Offline Mode", "Connect wifi", nullptr);
    flashLED(ERROR_LED, 3);
    return;
  }

  strcpy(emp.date, currentDate.c_str());

  int index = findEmployeeIndex(emp.employeeId);
  Serial.println("Employee index: " + String(index));

  if (index == -1) {
    if (isOnlineMode && hasEmployeeSignedOutToday(emp.employeeId)) {
      Serial.println("Employee already signed out today");
      Serial.println("MSG,Already Signed Out");
      updateLCD(LCD_ERROR, "Already Signed", "Out", nullptr);
      flashLED(ERROR_LED, 4);
      return;
    }

    if (attendanceCount < MAX_EMPLOYEES_PER_DAY) {
      int currentIndex = attendanceCount;

      dailyAttendance[currentIndex] = emp;
      strcpy(dailyAttendance[currentIndex].signInTime, getCurrentTime().c_str());
      dailyAttendance[currentIndex].signedIn = true;
      dailyAttendance[currentIndex].hasSignedOut = false;
      dailyAttendance[currentIndex].isValid = true;
      dailyAttendance[currentIndex].lastScanTime = millis();

      attendanceCount++;

      Serial.println("SIGN-IN at: " + String(dailyAttendance[currentIndex].signInTime));
      sendPLXDAQData(dailyAttendance[currentIndex], "SignIn", dailyAttendance[currentIndex].signInTime);

      if (isOnlineMode) {
        sendToGoogleSheets(dailyAttendance[currentIndex], "sign_in");
      }

      Serial.println("Updated attendanceCount: " + String(attendanceCount));
      updateLCD(LCD_WELCOME, "Welcome " + String(dailyAttendance[currentIndex].firstName), "", nullptr);
      flashLED(SIGN_IN_LED, 2);
    } else {
      Serial.println("Max attendance records reached");
      Serial.println("MSG,Max Records Reached");
      updateLCD(LCD_ERROR, "Max Records", "", nullptr);
      flashLED(ERROR_LED, 5);
    }
  } else {
    if (dailyAttendance[index].hasSignedOut) {
      Serial.println("Employee already signed out");
      Serial.println("MSG,Already Signed Out");
      updateLCD(LCD_ERROR, "Already Signed", "Out", nullptr);
      flashLED(ERROR_LED, 4);
      return;
    }

    String signOutTime = getCurrentTime();
    strcpy(dailyAttendance[index].signOutTime, signOutTime.c_str());
    dailyAttendance[index].signedIn = false;
    dailyAttendance[index].hasSignedOut = true;
    dailyAttendance[index].lastScanTime = millis();

    Serial.println("SIGN-OUT at: " + signOutTime);
    sendPLXDAQData(dailyAttendance[index], "SignOut", signOutTime);

    if (isOnlineMode) {
      sendSignOutToGoogleSheets(dailyAttendance[index], signOutTime);
    }

    updateLCD(LCD_GOODBYE, "Goodbye " + String(dailyAttendance[index].firstName), "", nullptr);
    flashLED(SIGN_OUT_LED, 2);
  }
  Serial.println("Attendance processing complete");
}

void sendPLXDAQData(const Employee& emp, const String& action, const String& time) {
  // Format: DATA,Date,EmployeeID,Surname,FirstName,SignInTime,SignOutTime,Action
  char employeeIdStr[20];
  strncpy(employeeIdStr, emp.employeeId, sizeof(employeeIdStr) - 1);
  employeeIdStr[sizeof(employeeIdStr) - 1] = '\0';
  if (strlen(employeeIdStr) == 0) {
    strcpy(employeeIdStr, "INVALID");
  }

  char data[128];
  snprintf(data, sizeof(data), "DATA,%s,%s,%s,%s,%s,%s,%s",
           emp.date,
           employeeIdStr,
           emp.surname,
           emp.firstName,
           (action == "SignIn" ? time.c_str() : emp.signInTime),
           (action == "SignOut" ? time.c_str() : emp.signOutTime),
           action.c_str());
  Serial.println(data);
}

int findEmployeeIndex(const char* employeeId) {
  for (int i = 0; i < attendanceCount; i++) {
    if (strcmp(dailyAttendance[i].employeeId, employeeId) == 0) {
      return i;
    }
  }
  return -1;
}

bool hasEmployeeSignedOutToday(const char* employeeId) {
  if (!isOnlineMode || !client->connected()) return false;

  char payload[128];
  snprintf(payload, sizeof(payload), "{\"command\": \"check_employee_status\", \"sheet_name\": \"%s\", \"employee_id\": \"%s\"}",
           currentDate.c_str(), employeeId);

  ESP.wdtFeed();
  if (client->POST(url, host, payload)) {
    String response = client->getResponseBody();
    return response.indexOf("signed_out") != -1;
  }
  return false;
}

uint32_t calculateChecksum(const DayRecord& record) {
  uint32_t checksum = 0;
  const uint8_t* ptr = (const uint8_t*)&record;
  for (size_t i = 0; i < sizeof(DayRecord) - sizeof(uint32_t); i++) {
    checksum += ptr[i];
  }
  return checksum;
}

void saveToEEPROM() {
  // This function is now a no-op since offline storage is disabled
  Serial.println("Offline mode");
}

void loadFromEEPROM() {
  // This function is now a no-op since offline storage is disabled
  Serial.println("Offline");
  storedDaysCount = 0;
  attendanceCount = 0;
  memset(dailyAttendance, 0, sizeof(dailyAttendance));
}

int findOrCreateDayIndex(const String& date) {
  for (int i = 0; i < storedDaysCount; i++) {
    if (strcmp(storedDays[i].date, date.c_str()) == 0) {
      return i;
    }
  }

  if (storedDaysCount < MAX_DAYS_STORAGE) {
    return storedDaysCount++;
  }
  return -1;
}

bool isEEPROMFull() {
  return storedDaysCount >= MAX_DAYS_STORAGE;
}

bool isEmployeeValid(const Employee& emp) {
  return emp.isValid &&
         strlen(emp.employeeId) > 0 &&
         strlen(emp.firstName) > 0 &&
         strlen(emp.surname) > 0 &&
         strlen(emp.date) > 0 &&
         (strlen(emp.signInTime) > 0 || strlen(emp.signOutTime) > 0);
}

void syncEEPROMData() {
  // This function is now a no-op since offline storage is disabled
  Serial.println("Offline storage disabled - no EEPROM sync");
}

bool sendEmployeeDataToSheetsWithRetry(const Employee& emp) {
  char payload[256]; // Pre-allocated buffer for JSON payload
  snprintf(payload, sizeof(payload),
           "{\"action\":\"update\",\"date\":\"%s\",\"employee_id\":\"%s\",\"surname\":\"%s\",\"first_name\":\"%s\",\"sign_in_time\":\"%s\",\"sign_out_time\":\"%s\",\"status\":\"%s\"}",
           emp.date, emp.employeeId, emp.surname, emp.firstName,
           emp.signInTime, emp.signOutTime, emp.hasSignedOut ? "Signed Out" : "Present");

  const int maxRetries = 3;
  ESP.wdtDisable(); // Disable WDT temporarily
  ESP.wdtEnable(30000); // Set WDT timeout to 30 seconds
  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    ESP.wdtFeed(); // Feed WDT before connection attempt
    if (!client->connected()) {
      if (!client->connect(host, httpsPort)) {
        Serial.println("Reconnection failed on attempt " + String(attempt));
        delay(1000); // Wait before retry
        continue;
      }
    }

    Serial.println("Sync attempt " + String(attempt) + "/" + String(maxRetries));
    String url = String("/macros/s/") + GScriptId + String("/exec");
    client->POST(url, payload, "application/json");
    ESP.wdtFeed(); // Feed WDT during POST
    int status = client->getStatusCode();
    ESP.wdtFeed(); // Feed WDT after status check
    String response = client->getResponseBody();

    if (status == 200) {
      Serial.println("Employee " + String(emp.employeeId) + " synced successfully");
      client->stop(); // Close connection
      ESP.wdtDisable(); // Disable WDT after success
      return true;
    } else {
      Serial.println("Sync failed, status: " + String(status) + ", response: " + response);
      delay(1000); // Wait before next attempt
    }
  }
  client->stop(); // Close connection
  ESP.wdtDisable(); // Disable WDT after failure
  return false;
}

void updateEEPROMAfterSync() {
  // This function is now a no-op since offline storage is disabled
  Serial.println("Offline storage disabled - no EEPROM update");
}

void handlePeriodicSync() {
  if (needsSync && isOnlineMode && (millis() - lastSyncAttempt > SYNC_INTERVAL)) {
    Serial.println("Initiating periodic sync at " + String(millis() / 1000) + " seconds");
    Serial.println("MSG,Periodic Sync Started");
    syncEEPROMData();
    lastSyncAttempt = millis();
  }
}

bool validateEEPROMData() {
  if (storedDaysCount < 0 || storedDaysCount > MAX_DAYS_STORAGE) {
    Serial.println("Invalid storedDaysCount: " + String(storedDaysCount));
    Serial.println("MSG,Invalid EEPROM Data");
    updateLCD(LCD_ERROR, "EEPROM Error", "", nullptr);
    return false;
  }

  bool valid = true;
  for (int i = 0; i < storedDaysCount; i++) {
    if (!storedDays[i].isValid || strlen(storedDays[i].date) == 0 ||
        storedDays[i].employeeCount < 0 || storedDays[i].employeeCount > MAX_EMPLOYEES_PER_DAY) {
      Serial.println("Invalid day " + String(i));
      Serial.println("MSG,Invalid Day Data");
      updateLCD(LCD_ERROR, "EEPROM Error", "", nullptr);
      valid = false;
    }
  }
  return valid;
}

void sendToGoogleSheets(const Employee& emp, const String& action) {
  if (!client->connected()) {
    if (!client->connect(host, httpsPort)) {
      Serial.println("Reconnection failed - OFFLINE mode");
      Serial.println("MSG,Sheets Reconnection Failed");
      isOnlineMode = false;
      saveToEEPROM();
      needsSync = true;
      updateLCD(LCD_ERROR, "Offline Mode", "", nullptr);
      return;
    }
  }

  char payload[256];
  snprintf(payload, sizeof(payload), "{\"command\": \"create_sheet\", \"sheet_name\": \"%s\"}", currentDate.c_str());
  ESP.wdtFeed();
  client->POST(url, host, payload);

  snprintf(payload, sizeof(payload),
           "{\"command\": \"insert_row\", \"sheet_name\": \"%s\", \"values\": \"%s,%s,%s,%s,%s,\"}",
           currentDate.c_str(), currentDate.c_str(), emp.employeeId, emp.surname, emp.firstName, emp.signInTime);

  ESP.wdtFeed();
  if (client->POST(url, host, payload)) {
    String response = client->getResponseBody();
    if (response.indexOf("Success") >= 0) {
      Serial.println("Data sent successfully");
    } else {
      Serial.println("Response: " + response);
      Serial.println("MSG,Sheets Send Failed");
      updateLCD(LCD_ERROR, "Send Failed", "", nullptr);
    }
  } else {
    Serial.println("Send failed - OFFLINE mode");
    Serial.println("MSG,Sheets Send Failed");
    isOnlineMode = false;
    saveToEEPROM();
    needsSync = true;
    updateLCD(LCD_ERROR, "Offline Mode", "", nullptr);
  }
}

void sendSignOutToGoogleSheets(const Employee& emp, const String& signOutTime) {
  if (!client->connected()) {
    if (!client->connect(host, httpsPort)) {
      Serial.println("Reconnection failed - OFFLINE mode");
      Serial.println("MSG,Sheets Reconnection Failed");
      isOnlineMode = false;
      saveToEEPROM();
      needsSync = true;
      updateLCD(LCD_ERROR, "Offline Mode", "", nullptr);
      return;
    }
  }

  char payload[128];
  snprintf(payload, sizeof(payload),
           "{\"command\": \"update_row\", \"sheet_name\": \"%s\", \"employee_id\": \"%s\", \"sign_out_time\": \"%s\"}",
           currentDate.c_str(), emp.employeeId, signOutTime.c_str());

  ESP.wdtFeed();
  if (client->POST(url, host, payload)) {
    String response = client->getResponseBody();
    if (response.indexOf("Success") >= 0) {
      Serial.println("Sign-out updated");
    } else {
      Serial.println("Response: " + response);
      Serial.println("MSG,Sign-Out Update Failed");
      updateLCD(LCD_ERROR, "Update Failed", "", nullptr);
    }
  } else {
    Serial.println("Update failed - OFFLINE mode");
    Serial.println("MSG,Sign-Out Update Failed");
    isOnlineMode = false;
    saveToEEPROM();
    needsSync = true;
    updateLCD(LCD_ERROR, "Offline Mode", "", nullptr);
  }
}

void updateAttendanceDates(const String& oldDate, const String& newDate) {
  Serial.println("Updating attendance dates from " + oldDate + " to " + newDate);

  for (int i = 0; i < attendanceCount; i++) {
    if (strcmp(dailyAttendance[i].date, oldDate.c_str()) == 0) {
      strcpy(dailyAttendance[i].date, newDate.c_str());
    }
  }

  for (int i = 0; i < storedDaysCount; i++) {
    if (strcmp(storedDays[i].date, oldDate.c_str()) == 0) {
      strcpy(storedDays[i].date, newDate.c_str());

      int dayAddress = EEPROM_DATA_START + (i * DAY_RECORD_SIZE);
      DayRecord dayRecord;
      EEPROM.get(dayAddress, dayRecord);

      strcpy(dayRecord.dayInfo.date, newDate.c_str());
      for (int j = 0; j < dayRecord.dayInfo.employeeCount; j++) {
        strcpy(dayRecord.employees[j].date, newDate.c_str());
      }

      dayRecord.checksum = calculateChecksum(dayRecord);
      EEPROM.put(dayAddress, dayRecord);
    }
  }

  EEPROM.put(sizeof(int), storedDays);
  EEPROM.commit();
  Serial.println("Attendance dates updated and saved");
  Serial.println("MSG,Attendance Dates Updated");
  updateLCD(LCD_DEFAULT, "", "", nullptr);
}

void updateDateTime()

 {
  if (isOnlineMode) {
    timeClient.update();
    unsigned long ntpEpoch = timeClient.getEpochTime();
    if (ntpEpoch < 1577836800) { // Minimum epoch (Jan 1, 2020)
      Serial.println("Invalid NTP epoch, using offline time");
      setCurrentDateOffline();
      return;
    }
    setTime(ntpEpoch);
  }

  Serial.println("NTP Time: " + timeClient.getFormattedTime());
  Serial.println("System Time: " + getCurrentTime());

  char dateStr[12];
  int y = year(), m = month(), d = day();
  if (y < 2020 || m < 1 || m > 12 || d < 1 || d > 31) {
    Serial.println("Invalid time values: " + String(y) + "-" + String(m) + "-" + String(d));
    setCurrentDateOffline();
    return;
  }
  snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", y, m, d);

  Serial.println("Formatted date: " + String(dateStr));
  if (currentDate.length() == 0) {
    currentDate = dateStr; // Initialize if empty
    Serial.println("Initialized currentDate: " + currentDate);
    updateLCD(LCD_SHOW, "Initializing", "", nullptr);
  } else if (currentDate != dateStr) {
    String oldDate = currentDate;
    currentDate = dateStr;
    dateIsAccurate = true;

    Serial.println("Date updated: " + oldDate + " -> " + currentDate);
    Serial.println("MSG,Date Updated");

    int dateAddress = EEPROM_SIZE - 20;
    for (int i = 0; i < 11; i++) {
      EEPROM.write(dateAddress + i, currentDate.c_str()[i]);
    }
    EEPROM.commit();

    if (attendanceCount > 0 && oldDate != currentDate) {
      updateAttendanceDates(oldDate, currentDate);
    }

    if (needsSync) {
      syncEEPROMData();
    }
  }
  updateLCD(LCD_DEFAULT, "", "", nullptr);
}

void setCurrentDateOffline() {
  char storedDate[12];
  int dateAddress = EEPROM_SIZE - 20;

  for (int i = 0; i < 11; i++) {
    storedDate[i] = EEPROM.read(dateAddress + i);
  }
  storedDate[11] = '\0';

  if (strlen(storedDate) == 10 && storedDate[4] == '-' && storedDate[7] == '-') {
    unsigned long daysSinceBoot = millis() / (24UL * 60UL * 60UL * 1000UL);

    if (daysSinceBoot == 0) {
      currentDate = String(storedDate);
      Serial.println("Offline mode - using stored date: " + currentDate);
    } else {
      currentDate = String(storedDate);
      Serial.println("Offline mode - estimated date: " + currentDate + " (may need correction when online)");
    }
  } else {
    currentDate = "2025-06-17";
    Serial.println("Offline mode - using fallback date: " + currentDate);
  }

  dateIsAccurate = false;
  Serial.println("MSG,Offline Mode");
  updateLCD(LCD_DEFAULT, "", "", nullptr);
}

String getCurrentTime() {
  char timeStr[9];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", hour(), minute(), second());
  return String(timeStr);
}

void checkNewDay() {
  static String lastDate = "";
  if (lastDate != currentDate && lastDate != "") {
    Serial.println("New day detected - resetting attendance");
    Serial.println("MSG,New Day Detected");
    Serial.println("CLEARDATA"); // Clear Excel data for new day
    Serial.println("LABEL,Date,EmployeeID,Surname,FirstName,SignInTime,SignOutTime,Action"); // Reset headers
    attendanceCount = 0;
    memset(dailyAttendance, 0, sizeof(dailyAttendance));
    if (isOnlineMode) {
      createNewSheet();
    }
    updateLCD(LCD_DEFAULT, "", "", nullptr);
  }
  lastDate = currentDate;
}

void createNewSheet() {
  if (!client->connected()) {
    if (!client->connect(host, httpsPort)) {
      Serial.println("Connection failed for sheet creation");
      Serial.println("MSG,Sheet Creation Failed");
      updateLCD(LCD_ERROR, "Sheet Failed", "", nullptr);
      return;
    }
  }

  char payload[128];
  snprintf(payload, sizeof(payload), "{\"command\": \"create_sheet\", \"sheet_name\": \"%s\"}", currentDate.c_str());

  ESP.wdtFeed();
  if (client->POST(url, host, payload)) {
    Serial.println("Sheet created: " + currentDate);
    Serial.println("MSG,Sheet Created");
    updateLCD(LCD_DEFAULT, "Sheet Created", "", nullptr);
    delay(1000);
  } else {
    Serial.println("Sheet creation failed");
    Serial.println("MSG,Sheet Creation Failed");
    updateLCD(LCD_ERROR, "Sheet Failed", "", nullptr);
  }
  updateLCD(LCD_DEFAULT, "", "", nullptr);
}

void flashLED(int pin, int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(pin, HIGH);
    delay(200);
    digitalWrite(pin, LOW);
    delay(200);
    yield();
  }
}

void loadTodaysAttendanceFromSheets() {
  if (!isOnlineMode || !client->connected()) {
    Serial.println("Cannot load attendance - offline or not connected");
    Serial.println("MSG,Cannot Load Attendance");
    return;
  }

  // Create sheet for currentDate first
  createNewSheet();

  char payload[128];
  snprintf(payload, sizeof(payload), "{\"command\": \"get_today_data\", \"sheet_name\": \"%s\"}", currentDate.c_str());

  ESP.wdtFeed();
  if (client->POST(url, host, payload)) {
    String response = client->getResponseBody();
    parseSheetResponse(response);
  } else {
    Serial.println("Failed to load today's attendance");
    Serial.println("MSG,Load Attendance Failed");
    updateLCD(LCD_ERROR, "Load Failed", "", nullptr);
  }
}

void parseSheetResponse(const String& response) {
  Serial.println("Parsing response: " + response);
  Serial.println("Available heap: " + String(ESP.getFreeHeap()) + " bytes");

  // Clear existing attendance to avoid duplicates
  attendanceCount = 0;
  memset(dailyAttendance, 0, sizeof(dailyAttendance));

  // Use DynamicJsonDocument to adapt to memory availability
  DynamicJsonDocument doc(2048); // Start with 2048 bytes, can grow if needed
  DeserializationError error = deserializeJson(doc, response);

  if (error) {
    Serial.println("JSON parsing failed: " + String(error.c_str()));
    Serial.println("MSG,JSON Parsing Failed");
    updateLCD(LCD_ERROR, "JSON Error", "", nullptr);
    return;
  }

  if (!doc["success"].as<bool>()) {
    Serial.println("Response indicates failure: " + doc["message"].as<String>());
    Serial.println("MSG,Data Error");
    updateLCD(LCD_ERROR, "Data Error", "", nullptr);
    return;
  }

  JsonArray data = doc["data"];
  int recordCount = 0;

  for (JsonObject record : data) {
    if (recordCount >= MAX_EMPLOYEES_PER_DAY) {
      Serial.println("Max employees reached: " + String(MAX_EMPLOYEES_PER_DAY));
      Serial.println("MSG,Max Employees Reached");
      updateLCD(LCD_ERROR, "Max Records", "", nullptr);
      break;
    }

    String recordDate = record["date"].as<String>();
    if (recordDate != currentDate) continue;

    String employeeId = record["employee_id"].is<String>() ? record["employee_id"].as<String>() : String(record["employee_id"].as<long>());
    if (employeeId.length() == 0 || employeeId == "0") continue;

    String surname = record["surname"].as<String>();
    String firstName = record["first_name"].as<String>();
    String signIn = record["sign_in_time"].as<String>();
    String signOut = record["sign_out_time"].as<String>();
    String status = record["status"].as<String>();

    if (surname.length() == 0 || firstName.length() == 0 || signIn.length() < 8) continue;

    Employee& emp = dailyAttendance[recordCount];
    memset(&emp, 0, sizeof(Employee));
    strncpy(emp.employeeId, employeeId.c_str(), sizeof(emp.employeeId) - 1);
    strncpy(emp.surname, surname.c_str(), sizeof(emp.surname) - 1);
    strncpy(emp.firstName, firstName.c_str(), sizeof(emp.firstName) - 1);
    strncpy(emp.date, currentDate.c_str(), sizeof(emp.date) - 1);
    strncpy(emp.signInTime, signIn.c_str(), sizeof(emp.signInTime) - 1);
    if (signOut.length() >= 8) strncpy(emp.signOutTime, signOut.c_str(), sizeof(emp.signOutTime) - 1);
    emp.signedIn = (status == "Present");
    emp.hasSignedOut = (status == "Signed Out");
    emp.isValid = true;
    emp.lastScanTime = millis();

    sendPLXDAQData(emp, emp.hasSignedOut ? "SignOut" : "SignIn",
                   emp.hasSignedOut ? emp.signOutTime : emp.signInTime);
    recordCount++;
  }

  attendanceCount = recordCount;
  Serial.println("Found " + String(recordCount) + " valid records for today");
  Serial.println("MSG,Loaded " + String(recordCount) + " Records");
  if (recordCount > 0) {
    updateLCD(LCD_DEFAULT, "Loaded " + String(recordCount) + " Rec", "", nullptr);
    delay(1000);
  }
  updateLCD(LCD_DEFAULT, "", "", nullptr);
}

void clearEEPROM() {
  Serial.println("Clearing EEPROM...");
  storedDaysCount = 0;
  memset(storedDays, 0, sizeof(storedDays));

  for (int i = 0; i < EEPROM_SIZE; i += 4) {
    EEPROM.put(i, (uint32_t)0);
    if (i % 100 == 0) yield();
  }

  EEPROM.put(0, storedDaysCount);
  EEPROM.put(sizeof(int), storedDays);

  if (EEPROM.commit()) {
    Serial.println("EEPROM cleared");
    Serial.println("MSG,EEPROM Cleared");
  } else {
    Serial.println("EEPROM commit failed");
    Serial.println("MSG,EEPROM Clear Failed");
    updateLCD(LCD_ERROR, "EEPROM Error", "", nullptr);
  }
}

void updateLCD(LCDState state, const String& line1, const String& line2, const Employee* emp) {
  String combinedMessage = line1;
  if (line2.length() > 0) {
    combinedMessage += "|" + line2;
  }

  if (state == lcdState && combinedMessage == lcdMessage && !lcdNeedsUpdate) return;

  lcd.clear();
  lcdState = state;
  lcdMessage = combinedMessage;
  lcdMessageStartTime = millis();
  lcdNeedsUpdate = false;

  switch (state) {
    case LCD_DEFAULT: {
      lcd.setCursor(0, 0);
      lcd.print("PCI");
      lcd.setCursor(0, 1);
      String status = isOnlineMode ? "Online " : "Offline ";
      if (needsSync && !isOnlineMode) status += "Sync ";
      status += String(attendanceCount) + " Staff";
      if (status.length() > 16) status = status.substring(0, 16);
      lcd.print(status);
      break;
    }
    case LCD_CARD_DETECTED:
      lcd.setCursor(0, 0);
      lcd.print("Card Detected");
      lcd.setCursor(0, 1);
      if (emp != nullptr) {
        String name = String(emp->firstName);
        if (name.length() > 16) {
          name = name.substring(0, 16);
        }
        lcd.print(name);
      } else {
        lcd.print("Reading...");
      }
      break;
    case LCD_WELCOME:
    case LCD_GOODBYE:
    case LCD_SHOW:
    case LCD_ERROR: {
      lcd.setCursor(0, 0);
      String firstLine = line1;
      if (firstLine.length() > 16) {
        firstLine = firstLine.substring(0, 16);
      }
      lcd.print(firstLine);

      for (int i = firstLine.length(); i < 16; i++) {
        lcd.print(" ");
      }

      lcd.setCursor(0, 1);
      String secondLine = line2;
      if (secondLine.length() > 16) {
        secondLine = secondLine.substring(0, 16);
      }
      lcd.print(secondLine);

      for (int i = secondLine.length(); i < 16; i++) {
        lcd.print(" ");
      }
      break;
    }
  }
}