#include "Rfid.h"
#include "TandoConfig.h"
#include "Demo.h"
#include "Reactions.h"
#include <SPI.h>
#include <MFRC522.h>

// Owns the RC522 reader and its card-present/removal latch.

// Local declarations replace Arduino-generated prototypes.
static bool uidMatches(const byte *target, byte targetSize);
static void printUid();
static bool cardStillPresent();

static MFRC522 rfid(RFID_SS, RFID_RST);
static bool rfidLatched = false;
static bool rfidLatchedIsSleep = false;
static uint32_t rfidNoCardSince = 0;

bool uidMatches(const byte *target, byte targetSize) {
  if (rfid.uid.size != targetSize) return false;

  for (byte i = 0; i < targetSize; i++) {
    if (rfid.uid.uidByte[i] != target[i]) return false;
  }

  return true;
}

void printUid() {
  Serial.print("RFID UID:");
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) Serial.print(" 0");
    else Serial.print(" ");
    Serial.print(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();
}

bool cardStillPresent() {
  byte atqa[2];
  byte atqaSize = sizeof(atqa);

  MFRC522::StatusCode status = rfid.PICC_WakeupA(atqa, &atqaSize);

  return status == MFRC522::STATUS_OK ||
         status == MFRC522::STATUS_COLLISION;
}

void updateRFID(uint32_t now) {
  // First, manage a card that has already been handled.
  if (rfidLatched) {
    if (cardStillPresent()) {
      rfidNoCardSince = 0;

      if (rfidLatchedIsSleep && reaction == R_SLEEP) {
        // A held SLEEP tag is a continuous interaction. Keep sleep alive
        // and keep the active-demo timer from pausing for inactivity.
        sleepTagPresent = true;
        lastInteractionMs = now;
      }
      return;
    }

    if (rfidNoCardSince == 0) {
      rfidNoCardSince = now;
      return;
    }

    if ((now - rfidNoCardSince) >= RFID_REARM_MS) {
      if (rfidLatchedIsSleep) {
        if (reaction == R_SLEEP) {
          beginSleepWake(now);
        } else {
          // SLEEP was waiting behind a system-priority animation but the tag
          // disappeared before sleep actually started.
          sleepTagPresent = false;
          pendingSleepVisual = false;
        }
      }

      rfidLatched = false;
      rfidLatchedIsSleep = false;
      rfidNoCardSince = 0;
      Serial.println("RFID READY");
    }

    return;
  }

  // Keep polling RFID even while a short PET/FOOD animation is active.
  // Event handlers either start the reaction immediately, coalesce a pending
  // visual, or give SLEEP the appropriate priority.
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  Serial.println();
  printUid();

  rfidLatchedIsSleep = false;

  if (uidMatches(FOOD1_UID, FOOD1_UID_SIZE)) {
    handleFoodEvent(1, now);
  } else if (uidMatches(FOOD2_UID, FOOD2_UID_SIZE)) {
    handleFoodEvent(2, now);
  } else if (uidMatches(SLEEP_UID, SLEEP_UID_SIZE)) {
    Serial.println("SLEEP TAG ACCEPTED");
    rfidLatchedIsSleep = true;
    handleSleepEvent(now);
  } else {
    Serial.println("UNKNOWN RFID TAG");
    handleUnknownRfidEvent(now);
  }

  rfidLatched = true;
  rfidNoCardSince = 0;

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void setupRfid() {
  // ----------------------------------------------------------
  // RC522
  // ----------------------------------------------------------
  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  rfid.PCD_Init();
  delay(20);
  Serial.println("RC522 READY");
}
