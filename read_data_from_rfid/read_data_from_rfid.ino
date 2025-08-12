#include <SPI.h>
#include <MFRC522.h>

#define RST_PIN  D3  // D1
#define SS_PIN   D4  // D2

MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();

  // Set default key (factory key)
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  Serial.println("📲 Scan RFID card to read employee data...");
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

  Serial.println("\n📍 Card Detected:");
  printUID();

  readBlock(4, "Employee ID");
  readBlock(5, "Surname");
  readBlock(6, "First Name");

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  delay(3000);
}

void printUID() {
  Serial.print("UID: ");
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.println();
}

void readBlock(byte blockAddr, const char* label) {
  byte buffer[18];
  byte size = sizeof(buffer);

  // Authenticate
  if (mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &key, &(mfrc522.uid)) != MFRC522::STATUS_OK) {
    Serial.print("❌ Auth failed for block "); Serial.println(blockAddr);
    return;
  }

  // Read
  if (mfrc522.MIFARE_Read(blockAddr, buffer, &size) != MFRC522::STATUS_OK) {
    Serial.print("❌ Read failed for block "); Serial.println(blockAddr);
    return;
  }

  // Print
  Serial.print(label); Serial.print(": ");
  for (int i = 0; i < 16; i++) {
    if (buffer[i] >= 32 && buffer[i] <= 126) Serial.write(buffer[i]);
  }
  Serial.println();
}
