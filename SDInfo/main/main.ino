#include "SDInfo.h"

// Create the SD driver object using our class
SDInfo card(PIN_CS);

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Turn on the SPI Hardware Bus using the pins defined in the header
    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);

    // Run our driver initialization
    if (card.begin()) {
        Serial.println("Card successfully ready!");
        card.printCardInfo();
    } else {
        Serial.println("Initialization failed. Check hardware or try a different card.");
    }
}

void loop() {
    // Keep loop empty for clean execution
}
