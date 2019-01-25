#include <USBAPI.h>
#include "bluetooth.h"

//Do not remove or else doesn't compile. See https://stackoverflow.com/questions/8016780/undefined-reference-to-static-constexpr-char
constexpr char BluetoothManager::BEGIN_CONNECTION_PACKET[3];


BluetoothManager::BluetoothManager(LedController &ledArg, ButtonPressReceiver *buttonPressReceiver,
                                   ReturnToReadyModeCallback *returnToReadyModeCallback)
        : ledManager(ledArg), returnToReadyModeCallback(returnToReadyModeCallback),
          buttonPressReceiver(buttonPressReceiver) {
    BtSerial.begin(9600);
}

bool BluetoothManager::shouldStartBluetoothMode() {
    return doesContainBeginConnectionPacket(readBluetoothSerial());
}

void BluetoothManager::startBluetoothMode() {
    buttonPressReceiver->addListener(this);
    runnablesManager::addRunnable(this);
}

void BluetoothManager::onRun() {
    parseReceivedData(readBluetoothSerial());
}

bool BluetoothManager::doesContainBeginConnectionPacket(const char *receivedData) {
    return strstr(receivedData, BEGIN_CONNECTION_PACKET); //strstr checks if string contains substring
}

char *BluetoothManager::readBluetoothSerial() {
    static char content[256];

    unsigned char index = 0;

    //Keep looping while data is available
    while (BtSerial.available() and index < 255) {
        content[index] = char(BtSerial.read());
        delay(10); //Delay is necessary to allow proper reading
        index++;
    }

    content[index] = '\0'; // Add null terminator where string ends

    return content;
}


void BluetoothManager::sendPacket(const char *packetData) {
    //1.  Create packet
    size_t lengthOfData = strlen(packetData);

    char packet[lengthOfData + 3]; // +3 for start and end bytes and null terminator

    packet[0] = START_OF_PACKET;
    packet[1] = '\0';

    strcat(packet, packetData);

    packet[lengthOfData + 1] = END_OF_PACKET;
    packet[lengthOfData + 2] = '\0';

    //2.  Send packet
    BtSerial.write(packet);

    //3. Log operation for debugging
    Serial.print(F("Sent packet: "));
    Serial.println(packet);
}


void BluetoothManager::onButtonPressed(const unsigned char buttonPressed) {
    //Create string for containing the data to be sent
    char buttonNumber[3];
    sprintf(buttonNumber, "%05d", buttonPressed);

    char packetContent[4];
    packetContent[0] = BluetoothManager::BUTTON_PRESSED;
    packetContent[1] = '\0';

    strcat(packetContent, buttonNumber);

    packetContent[3] = '\0';

    //Send the data
    sendPacket(packetContent);
}

void BluetoothManager::returnToReadyMode() {
    runnablesManager::removeRunnable(this);
    buttonPressReceiver->removeListener();
    returnToReadyModeCallback->returnToReadyMode();
}

void BluetoothManager::parseReceivedData(const char *receivedData) {
    size_t lengthOfData = strlen(receivedData);

    unsigned char index = 0;

    while (index < lengthOfData) {
        if (receivedData[index] == ACKNOWLEDGE_BYTE) {
            index++;
        } else if (receivedData[index] == START_OF_PACKET) {
            bool readingPacketContent = true;

            while (readingPacketContent) {
                if (index >= lengthOfData) {
                    Serial.println(
                            F("Error: Did not receive end of packet byte. Part of packet might already been processed."));
                    break;
                }

                if (receivedData[index] ==
                    BEGIN_CONNECTION) {// Nothing to do. Acknowledge will be sent when end of packet byte received
                } else if (receivedData[index] == END_CONNECTION) {
                    returnToReadyMode();
                } else if (receivedData[index] == TURN_ON_LED or receivedData[index] == TURN_OFF_LED) {
                    if (index + 2 >= lengthOfData) {
                        Serial.println("Error: No bytes received indicating led number.");
                    }

                    char ledNumber[3];
                    ledNumber[0] = receivedData[index + 1];
                    ledNumber[1] = receivedData[index + 2];
                    ledNumber[2] = '\0';

                    if (receivedData[index] == TURN_ON_LED) {
                        ledManager.turnOnLed(static_cast<unsigned char>(atoi(ledNumber)));
                    } else {
                        ledManager.turnOffLed(static_cast<unsigned char>(atoi(ledNumber)));
                    }

                    index += 2; //Increment extra 2 because read 2 bytes for led numbers

                } else if (receivedData[index] == SHIFT_OUT) {
                    ledManager.shiftOutLEDs();
                } else if (receivedData[index] == END_OF_PACKET) {
                    BtSerial.write(ACKNOWLEDGE_BYTE);
                    readingPacketContent = false; //No false because done reading
                } else {
                    Serial.print(F("Error: Could not parse content received over bluetooth: "));
                    Serial.println(receivedData);
                    readingPacketContent = false;
                }

                index++;
            }
        } else {
            Serial.print(F("Error: Could not parse content received over bluetooth: "));
            Serial.println(receivedData);
            break;
        }
    }
}