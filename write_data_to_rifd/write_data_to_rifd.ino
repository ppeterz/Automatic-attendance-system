#include <SPI.h>
#include <MFRC522.h>

#define RST_PIN  D3 // D1
#define SS_PIN   D4  // D2

MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

String empID = "", surname = "", firstName = "";

void setup() {
  delay(1500);
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();

  // Default key (factory)
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  Serial.println("Enter Employee ID (max 16 chars):");
}

void loop() {
  static byte inputStage = 0;

  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 16) {
      Serial.println("Input too long! Max 16 characters. Try again:");
      return;
    }

    if (inputStage == 0) {
      empID = input;
      Serial.println("Enter Surname:");
      inputStage++;
    } else if (inputStage == 1) {
      surname = input;
      Serial.println("Enter First Name:");
      inputStage++;
    } else if (inputStage == 2) {
      firstName = input;
      Serial.println("Now tap an RFID card to write data...");
      inputStage++;
    }
  }

  if (inputStage == 3) {
    if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

    Serial.println("Card detected, writing data...");
    if (writeBlock(4, empID) && writeBlock(5, surname) && writeBlock(6, firstName)) {
      Serial.println("✅ Data written successfully!\n\nReading back...");
      readBlock(4, "Employee ID");
      readBlock(5, "Surname");
      readBlock(6, "First Name");
    } else {
      Serial.println("❌ Error writing data!");
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    Serial.println("\n---\nEnter Employee ID for another write:");
    empID = surname = firstName = "";
    inputStage = 0;
    delay(2000);
  }
}

bool writeBlock(byte blockAddr, String data) {
  byte buffer[16];
  data.getBytes(buffer, sizeof(buffer));

  if (mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &key, &(mfrc522.uid)) != MFRC522::STATUS_OK) {
    Serial.print("Auth failed at block "); Serial.println(blockAddr);
    return false;
  }

  if (mfrc522.MIFARE_Write(blockAddr, buffer, 16) != MFRC522::STATUS_OK) {
    Serial.print("Write failed at block "); Serial.println(blockAddr);
    return false;
  }

  return true;
}

void readBlock(byte blockAddr, const char* label) {
  byte buffer[18];
  byte size = sizeof(buffer);

  if (mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &key, &(mfrc522.uid)) != MFRC522::STATUS_OK) {
    Serial.print("Auth failed at block "); Serial.println(blockAddr);
    return;
  }

  if (mfrc522.MIFARE_Read(blockAddr, buffer, &size) != MFRC522::STATUS_OK) {
    Serial.print("Read failed for block "); Serial.println(blockAddr);
    return;
  }

  Serial.print(label); Serial.print(": ");
  for (byte i = 0; i < 16; i++) {
    if (buffer[i] >= 32 && buffer[i] <= 126) Serial.write(buffer[i]);
  }
  Serial.println();
}
