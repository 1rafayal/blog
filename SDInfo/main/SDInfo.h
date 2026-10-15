#pragma once

#include <Arduino.h>
#include <SPI.h>

// these are the default SPI pins for a NodeMCU ESP32s dev board
#define PIN_CS   5
#define PIN_SCK  18
#define PIN_MISO 19
#define PIN_MOSI 23

//these are the stanarad SPI commands that we will be using.
enum SDCommand : uint8_t {
    CMD0  = 0,   // GO_IDLE_STATE
    CMD8  = 8,   // SEND_IF_COND
    CMD9  = 9,   // SEND_CSD
    CMD10 = 10,  // SEND_CID
    CMD41 = 41,  // SD_SEND_OP_COND (ACMD41)
    CMD55 = 55,  // APP_CMD
    CMD58 = 58   // READ_OCR
};

class SDInfo {
private:
    uint8_t _csPin;
    SPISettings _spiSettings;

    //send the command to retrieve the R1 response here.
    uint8_t sendCmd(uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *tail = nullptr, uint8_t tailLen = 0) {
        digitalWrite(_csPin, LOW);
        SPI.transfer(0xFF); //this syns up the clocks.
        SPI.transfer(0x40 | cmd);
        SPI.transfer(arg >> 24);
        SPI.transfer(arg >> 16);
        SPI.transfer(arg >> 8);
        SPI.transfer(arg);
        SPI.transfer(crc);

        //wait for the card to respond here by listening for a specific response.
        uint8_t r = 0xFF;
        for (int i = 0; i < 10; i++) {
            r = SPI.transfer(0xFF);
            if (!(r & 0x80)) break;
        }

        //if the r3 and r7 bytes are available, read them here.
        for (uint8_t i = 0; i < tailLen; i++) {
            tail[i] = SPI.transfer(0xFF);
        }

        digitalWrite(_csPin, HIGH);
        SPI.transfer(0xFF); //extra clock byte so the card releases the MISO line
        return r;
    }

    //return the CSD/CID here.
    bool readRegister(uint8_t cmd, uint8_t *buf) {
        digitalWrite(_csPin, LOW);
        SPI.transfer(0xFF);
        SPI.transfer(0x40 | cmd);
        for (int i = 0; i < 4; i++) SPI.transfer(0x00);
        SPI.transfer(0xFF);

        uint8_t r = 0xFF;
        for (int i = 0; i < 10; i++) {
            r = SPI.transfer(0xFF);
            if (!(r & 0x80)) break;
        }

        bool success = false;
        Serial.printf("CMD%u R1 -> 0x%02X (Expected: 0x00)\n", cmd, r);
        if (r == 0x00) {
            uint32_t start = millis();
            uint8_t token;
            do {
                token = SPI.transfer(0xFF);
            } while (token == 0xFF && millis() - start < 200);

            Serial.printf("CMD%u data token -> 0x%02X (Expected: 0xFE)\n", cmd, token);
            if (token == 0xFE) { //this is our data ttoken
                for (int i = 0; i < 16; i++) buf[i] = SPI.transfer(0xFF);
                SPI.transfer(0xFF); //drop two bytes from the CRC across these lines.
                SPI.transfer(0xFF);
                success = true;
            }
        }
        digitalWrite(_csPin, HIGH);
        SPI.transfer(0xFF);
        return success;
    }

    void printHex(const char *label, uint8_t *b) {
        Serial.print(label);
        for (int i = 0; i < 16; i++) Serial.printf("%02X ", b[i]);
        Serial.println();
    }

    // the actual init sequence. Must be called inside an SPI transaction.
    bool initCard() {
        Serial.println("\n--- Initializing SD Card Over Raw SPI ---");

        //Wake up the card for SPI mode (80 clocks with CS high)
        for (int i = 0; i < 10; i++) SPI.transfer(0xFF);

        // send CMD0 to reset the card into SPI mode
        uint8_t r = 0xFF;
        for (int i = 0; i < 5 && r != 0x01; i++) {
            r = sendCmd(CMD0, 0, 0x95);
        }
        Serial.printf("CMD0 Reset -> 0x%02X (Expected: 0x01)\n", r);
        if (r != 0x01) return false;

        // send CMD8 to check the covltage for v2 cards
        uint8_t t[4] = {0};
        r = sendCmd(CMD8, 0x1AA, 0x87, t, 4);
        Serial.printf("CMD8 Voltage Check -> 0x%02X\n", r);
        if (r != 0x01 || t[2] != 0x01 || t[3] != 0xAA) {
            Serial.println("Error: Card is either ancient V1 or has a voltage mismatch.");
            return false;
        }

        // start the ACMD41 loop to initialise the memory arrary
        uint32_t start = millis();
        do {
            sendCmd(CMD55, 0, 0x65);
            r = sendCmd(CMD41, 0x40000000UL, 0x77); // this part tests for high cpapcity memory support
        } while (r != 0x00 && millis() - start < 2000);

        Serial.printf("ACMD41 Initialization -> 0x%02X (Expected: 0x00)\n", r);
        return (r == 0x00);
    }

public:
    SDInfo(uint8_t csPin) : _csPin(csPin), _spiSettings(400000, MSBFIRST, SPI_MODE0) {}

    bool begin() {
        // FIX 1: actually start the SPI peripheral on the right pins
        SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, _csPin);

        pinMode(_csPin, OUTPUT);
        digitalWrite(_csPin, HIGH);

        // FIX 2: apply the 400 kHz / mode 0 settings for the whole init
        SPI.beginTransaction(_spiSettings);
        bool ok = initCard();
        SPI.endTransaction();
        return ok;
    }

    void printCardInfo() {
        SPI.beginTransaction(_spiSettings);

        uint8_t csd[16], cid[16];

        // read the CSD, then work out the capacity for whichever format it is
        if (!readRegister(CMD9, csd)) {
            Serial.println("CSD read failed (see R1 data token.");
        } else {
            printHex("CSD Register: ", csd);
            uint8_t csdVer = csd[0] >> 6;
            Serial.printf("CSD structure version: %u\n", csdVer);

            uint64_t capacityMB = 0;
            if (csdVer == 1) {
                // CSD v2 (SDHC/SDXC): capacity = (C_SIZE + 1) * 512 KB
                uint32_t cSize = ((uint32_t)(csd[7] & 0x3F) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
                capacityMB = ((uint64_t)cSize + 1) / 2;
            } else if (csdVer == 0) {
                // CSD v1 (standard capacity, <= 2 GB)
                uint32_t cSize     = ((uint32_t)(csd[6] & 0x03) << 10) | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
                uint8_t  cSizeMult = ((csd[9] & 0x03) << 1) | (csd[10] >> 7);
                uint8_t  readBlLen = csd[5] & 0x0F;
                uint64_t bytes = ((uint64_t)(cSize + 1)) << (cSizeMult + 2) << readBlLen;
                capacityMB = bytes >> 20;
            } else {
                Serial.println("Unknown CSD structure version.");
            }

            if (capacityMB) {
                Serial.printf("Capacity: %lu MB (~%d GB)\n", (unsigned long)capacityMB, (int)(capacityMB / 1000));
            }
        }

        // reads the CID
        if (readRegister(CMD10, cid)) {
            printHex("CID Register: ", cid);
            Serial.printf("Manufacturer ID: 0x%02X\n", cid[0]);
            Serial.printf("OEM ID: %c%c\n", cid[1], cid[2]);

            Serial.print("Product Name: ");
            for (int i = 3; i <= 7; i++) Serial.write(cid[i]);
            Serial.println();

            uint32_t sn = ((uint32_t)cid[9] << 24) | ((uint32_t)cid[10] << 16) | ((uint32_t)cid[11] << 8) | cid[12];
            Serial.printf("Serial Number: 0x%08lX\n", (unsigned long)sn);
        } else {
            Serial.println("Failed to read card identity from CID.");
        }

        SPI.endTransaction();
    }
};