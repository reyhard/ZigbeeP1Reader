#include <Arduino.h>
#include <zbhci.h>
#include <hci_display.h>
#include <OneButton.h>
#include "esp_task_wdt.h"
#include "settings.h"
#include "utils.h"

typedef struct {
  uint32_t high;
  uint32_t low;
} uint48_t;

typedef struct {
  uint32_t high;
  uint32_t mid;
  uint32_t low;
} uint56_t;

#define CONFIG_ZIGBEE_MODULE_PIN 0
#define CONFIG_USR_BUTTON_PIN 2
#define CONFIG_BLUE_LIGHT_PIN 3

#define REPORTING_PERIOD 10

const uint8_t au8ManufacturerName[] = {16,'L','I','L','Y','G','O', '.', 'P', '1', '_', 'R', 'e', 'a', 'd' , 'e' ,'r'};

QueueHandle_t msg_queue;

/**
 * Initialize a new OneButton instance for a button
 * connected to digital pin 4 and GND, which is active low
 * and uses the internal pull-up resistor.
 */
OneButton btn = OneButton(CONFIG_USR_BUTTON_PIN,     // Input pin for the button
                          true,  // Button is active LOW
                          true); // Enable internal pull-up resistor

void setup()
{
#ifdef DEBUG_SERIAL
    Serial.begin(115200);
#else
    Serial.begin(115200, SERIAL_8N1, SERIAL_PIN_RX, -1, false);
    Serial.println("");
    Serial.println("Swapping UART0 RX to inverted");
    Serial.flush();
    Serial.setRxInvert(true);
#endif
    //Serial1.begin(BAUD_RATE, SERIAL_8N1, RXD1, TXD1, true);
    delay(10);
    Serial.printf("Init P1 Meter\n");

    pinMode(CONFIG_ZIGBEE_MODULE_PIN, OUTPUT);
    digitalWrite(CONFIG_ZIGBEE_MODULE_PIN, HIGH);
    delay(500);

    pinMode(CONFIG_BLUE_LIGHT_PIN, OUTPUT);
    digitalWrite(CONFIG_BLUE_LIGHT_PIN, LOW);

    btn.attachClick(handleClick);
    btn.attachDoubleClick(handleDoubleClick);
    btn.setPressTicks(3000);
    btn.attachLongPressStart(handleLongPress);

    msg_queue = xQueueCreate(10, sizeof(ts_HciMsg));
    zbhci_Init(msg_queue);

    xTaskCreatePinnedToCore(
        zbhciTask,
        "zbhci",   // A name just for humans
        4096,          // This stack size can be checked & adjusted by reading the Stack Highwater
        NULL,
        5,             // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
        NULL ,
        ARDUINO_RUNNING_CORE);

    // zbhci_BdbFactoryReset();
    delay(100);
    zbhci_NetworkStateReq();
    setupDataReadout();
}


void loop()
{
    btn.tick();
    long now = millis();
    // Check if we want a full update of all the data including the unchanged data.
    if (now - LAST_FULL_UPDATE_SENT > UPDATE_FULL_INTERVAL)
    {
        for (int i = 0; i < NUMBER_OF_READOUTS; i++)
        {
            telegramObjects[i].sendData = true;
            LAST_FULL_UPDATE_SENT = millis();
        }
    }

    if (now - LAST_UPDATE_SENT > UPDATE_INTERVAL)
    {
        if (readP1Serial())
        {
            LAST_UPDATE_SENT = millis();
            sendDataToBroker();
        }
    }
}

uint8_t netState = 0;

// Debug
void handleClick(void)
{
    ts_DstAddr sDstAddr;

    sDstAddr.u16DstAddr = 0x0000;
    if (netState == 1)
    {
        int16_t h = (int) (1);
        uint32_t t = (int) (12183425);
        uint32_t tw = (int) (1183425);
        delay(100);
        zbhci_ZclSendReportCmd(0x02, sDstAddr, 1, 1, 0, 1, 0x702, 0x4000, ZCL_DATA_TYPE_DATA32, sizeof(t), (uint8_t *)&t);
        delay(100);
        zbhci_ZclSendReportCmd(0x02, sDstAddr, 1, 1, 0, 1, 0x702, 0x4001, ZCL_DATA_TYPE_DATA32, sizeof(tw), (uint8_t *)&tw);
        delay(100);
        zbhci_ZclSendReportCmd(0x02, sDstAddr, 1, 1, 0, 1, 0x702, 0x4002, ZCL_DATA_TYPE_DATA16, sizeof(h), (uint8_t *)&h);
    }
    else
    {
        Serial.println("Not joined the zigbee network");
    }

    digitalWrite(CONFIG_BLUE_LIGHT_PIN, true);
    delay(2000);
    digitalWrite(CONFIG_BLUE_LIGHT_PIN, false);
}

// Trigger full update of data on double click of button
void handleDoubleClick(void)
{
    LAST_FULL_UPDATE_SENT = 0;
    Serial.println("Trying to trigger full data update");
}

void handleLongPress()
{
    if (netState == 0)
    {
        Serial.println("Joining the zigbee network (sensor)");
        zbhci_BdbCommissionSteer();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    else if (netState == 1)
    {
        Serial.println("leave the zigbee network");
        zbhci_BdbFactoryReset();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        netState = 0;
    }
}


void zbhciTask(void *pvParameters)
{
    ts_HciMsg sHciMsg;
    ts_DstAddr sDstAddr;

    while (1)
    {
        bzero(&sHciMsg, sizeof(sHciMsg));
        if (xQueueReceive(msg_queue, &sHciMsg, portMAX_DELAY))
        {
            switch (sHciMsg.u16MsgType)
            {
                case ZBHCI_CMD_ACKNOWLEDGE:
                    // displayAcknowledg(&sHciMsg.uPayload.sAckPayload);
                break;

                case ZBHCI_CMD_NETWORK_STATE_RSP:
                    if (sHciMsg.uPayload.sNetworkStateRspPayloasd.u16NwkAddr == 0x0000)
                    {
                        zbhci_BdbFactoryReset();
                        vTaskDelay(1000 / portTICK_PERIOD_MS);
                        zbhci_NetworkStateReq();
                    }
                    else if (sHciMsg.uPayload.sNetworkStateRspPayloasd.u16NwkAddr != 0xFFFF)
                    {
                        netState = 1;
                    }
                break;

                case ZBHCI_CMD_NETWORK_STATE_REPORT:
                    netState = 1;
                    sDstAddr.u16DstAddr = 0x0000;
                    zbhci_ZclSendReportCmd(0x02, sDstAddr, 1, 1, 0, 1, 0x0000, 0x0005, ZCL_DATA_TYPE_CHAR_STR, sizeof(au8ManufacturerName), (uint8_t *)&au8ManufacturerName);
                break;

                case ZBHCI_CMD_ZCL_ONOFF_CMD_RCV:
                break;

                default:
                    Serial.printf("u16MsgType %d\n", sHciMsg.u16MsgType);
                break;
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

// Send single value
void sendMetric(String name, long metric, uint16_t attribute)
{
    ts_DstAddr sDstAddr;

    sDstAddr.u16DstAddr = 0x0000;
    zbhci_ZclSendReportCmd(0x02, sDstAddr, 1, 1, 0, 1, 0x702, attribute, ZCL_DATA_TYPE_DATA32, sizeof(metric), (uint8_t *)&metric);
    delay(100);
}

// Send 3 values at once
void sendMetric3(String name, uint56_t metric, uint16_t attribute)
{
    ts_DstAddr sDstAddr;

    sDstAddr.u16DstAddr = 0x0000;
    zbhci_ZclSendReportCmd(0x02, sDstAddr, 1, 1, 0, 1, 0x702, attribute, ZCL_DATA_TYPE_DATA56, sizeof(metric), (uint8_t *)&metric);
    delay(100);
}

void sendDataToBroker()
{
    for (int i = 0; i < NUMBER_OF_READOUTS; i++)
    {
        if (telegramObjects[i].sendData)
        {
#ifdef DEBUG
        Serial.println((String) "Sending: " + telegramObjects[i].name + " value: " + telegramObjects[i].value);
#endif
            if(telegramObjects[i].attributeID == 0x4004) {
                uint56_t report;
                telegramObjects[7].sendData = false;
                telegramObjects[8].sendData = false;
                telegramObjects[9].sendData = false;
                report.high = telegramObjects[7].value;
                report.mid = telegramObjects[8].value;
                report.low = telegramObjects[9].value;
                sendMetric3(telegramObjects[i].name, report, telegramObjects[i].attributeID);

            }else{
                sendMetric(telegramObjects[i].name, telegramObjects[i].value, telegramObjects[i].attributeID);
                telegramObjects[i].sendData = false;
            }
        }
    }
}

/***********************************
            Setup Methods
 ***********************************/

/**
   setupDataReadout()

   This method can be used to create more data readout to mqtt topic.
   Use the name for the mqtt topic.
   The code for finding this in the telegram see
    https://www.netbeheernederland.nl/_upload/Files/Slimme_meter_15_a727fce1f1.pdf for the dutch codes pag. 19 -23
   Use startChar and endChar for setting the boundies where the value is in between.
   Default startChar and endChar is '(' and ')'
   Note: Make sure when you add or remove telegramObject to update the NUMBER_OF_READOUTS accordingly.
*/
void setupDataReadout()
{
    
    // 0-0:96.14.0(0001)
    // 0-0:96.14.0 = Actual Tarif
    telegramObjects[0].name = "energy_tariff";
    telegramObjects[0].attributeID = (int) 16386;
    strcpy(telegramObjects[0].code, "0-0:96.14.0");

    // 1-0:1.8.1(000992.992*kWh)
    // 1-0:1.8.1 = Elektra verbruik laag tarief (DSMR v5.0)
    telegramObjects[1].name = "energy_low";
    telegramObjects[1].attributeID = (int) 16384;
    strcpy(telegramObjects[1].code, "1-0:1.8.1");
    telegramObjects[1].endChar = '*';

    // 1-0:1.8.2(000560.157*kWh)
    // 1-0:1.8.2 = Elektra verbruik hoog tarief (DSMR v5.0)
    telegramObjects[2].name = "energy_high";
    telegramObjects[2].attributeID = (int) 16385;
    strcpy(telegramObjects[2].code, "1-0:1.8.2");
    telegramObjects[2].endChar = '*';

    // 1-0:1.7.0(00.424*kW) Actueel verbruik
    // 1-0:1.7.x = Electricity consumption actual usage (DSMR v5.0)
    telegramObjects[3].name = "power";
    telegramObjects[3].attributeID = (int) 16387;
    strcpy(telegramObjects[3].code, "1-0:1.7.0");
    telegramObjects[3].endChar = '*';
    telegramObjects[3].sendThreshold = (long) 3;

    // 0-1:24.2.3(150531200000S)(00811.923*m3)
    // 0-1:24.2.3 = Gas (DSMR v5.0) on Belgian meters
    telegramObjects[4].name = "gas";
    telegramObjects[4].attributeID = (int) 16389;
    strcpy(telegramObjects[4].code, "0-1:24.2.1");
    telegramObjects[4].endChar = '*';

    // 0-1:24.2.3(150531200000S)(00811.923*m3)
    // 0-1:24.2.3 = Gas (DSMR v5.0) on Belgian meters
    telegramObjects[4].name = "gas";
    telegramObjects[4].attributeID = (int) 16389;
    strcpy(telegramObjects[4].code, "0-1:24.2.1");
    telegramObjects[4].endChar = '*';
    
    // 0-0:96.7.21(00008)
    // Number of power failures in any phases
    telegramObjects[5].name = "fail";
    telegramObjects[5].attributeID = (int) 16390;
    strcpy(telegramObjects[5].code, "0-0:96.7.21");

    // 0-0:96.7.9(00002)
    // Number of long power failures in any phases
    telegramObjects[6].name = "fail_long";
    telegramObjects[6].attributeID = (int) 16391;
    strcpy(telegramObjects[6].code, "0-0:96.7.9");
/*
    // 1-0:21.7.0(00.378*kW)
    // 1-0:21.7.0 = Instantaan vermogen Elektriciteit levering L1
    telegramObjects[7].name = "power_l1";
    telegramObjects[7].attributeID = 0x4004;
    strcpy(telegramObjects[7].code, "1-0:21.7.0");
    telegramObjects[7].endChar = '*';
    telegramObjects[7].sendThreshold = (long) 3;

    // 1-0:41.7.0(00.378*kW)
    // 1-0:41.7.0 = Instantaan vermogen Elektriciteit levering L2
    telegramObjects[8].name = "power_l2";
    telegramObjects[8].attributeID = 0x4004;
    strcpy(telegramObjects[8].code, "1-0:41.7.0");
    telegramObjects[8].endChar = '*';
    telegramObjects[8].sendThreshold = (long) 3;

    // 1-0:61.7.0(00.378*kW)
    // 1-0:61.7.0 = Instantaan vermogen Elektriciteit levering L3
    telegramObjects[9].name = "power_l3";
    telegramObjects[9].attributeID = 0x4004;
    strcpy(telegramObjects[9].code, "1-0:61.7.0");
    telegramObjects[9].endChar = '*';
    telegramObjects[9].sendThreshold = (long) 3;
*/

#ifdef DEBUG
    Serial.println("Data initialized:");
    for (int i = 0; i < NUMBER_OF_READOUTS; i++)
    {
        Serial.println(telegramObjects[i].name);
    }
#endif
}


long getValue(char *buffer, int maxlen, char startchar, char endchar)
{
    int s = findCharInArrayRev(buffer, startchar, maxlen - 2);
    int l = findCharInArrayRev(buffer, endchar, maxlen) - s - 1;

    // Verify data - if message is corrupted, then there is high chance of missing start of end char which would lead to reboot of ESP
    if(s <= 0 || l <= 0) {
#ifdef DEBUG
            Serial.println((String) "Wrong input data. s: " + s + " l: " + l);
#endif
        return 0;
    }

    char res[16];
    memset(res, 0, sizeof(res));

    if (strncpy(res, buffer + s + 1, l))
    {
        if (endchar == '*')
        {
            if (isNumber(res, l))
                return (1000 * atof(res));
        }
        else if (endchar == ')')
        {
            if (isNumber(res, l))
                return atof(res);
        }
    }

    return 0;
}

/**
 *  Decodes the telegram PER line. Not the complete message. 
 */
bool decodeTelegram(int len)
{
    int startChar = findCharInArrayRev(telegram, '/', len);
    int endChar = findCharInArrayRev(telegram, '!', len);
    bool validCRCFound = false;

#ifdef DEBUG
    for (int cnt = 0; cnt < len; cnt++)
    {
        Serial.print(telegram[cnt]);
    }
    Serial.print("\n");
#endif

    if (startChar >= 0)
    {
        // * Start found. Reset CRC calculation
        currentCRC = crc16(0x0000, (unsigned char *)telegram + startChar, len - startChar);
    }
    else if (endChar >= 0)
    {
        // * Add to crc calc
        currentCRC = crc16(currentCRC, (unsigned char *)telegram + endChar, 1);

        char messageCRC[5];
        strncpy(messageCRC, telegram + endChar + 1, 4);

        messageCRC[4] = 0; // * Thanks to HarmOtten (issue 5)
        validCRCFound = (strtol(messageCRC, NULL, 16) == currentCRC);

#ifdef DEBUG
        if (validCRCFound)
            Serial.println(F("CRC Valid!"));
        else
            Serial.println(F("CRC Invalid!"));
#endif
        currentCRC = 0;
    }
    else
    {
        currentCRC = crc16(currentCRC, (unsigned char *)telegram, len);
    }

    // Loops throug all the telegramObjects to find the code in the telegram line
    // If it finds the code the value will be stored in the object so it can later be send to the mqtt broker
    for (int i = 0; i < NUMBER_OF_READOUTS; i++)
    {
        if (strncmp(telegram, telegramObjects[i].code, strlen(telegramObjects[i].code)) == 0)
        {
            // Handle empty objects
            if(strlen(telegramObjects[i].code ) < 3) 
            { 
                continue; 
            };
#ifdef DEBUG
            Serial.println("Get value " + String(telegram));
#endif
            long newValue = getValue(telegram, len, telegramObjects[i].startChar, telegramObjects[i].endChar);
            if (newValue != telegramObjects[i].value)
            {
                // Do not send values if they change by some really minor value
                if(abs(telegramObjects[i].value - newValue) > telegramObjects[i].sendThreshold) {

                    telegramObjects[i].sendData = true;
                }else{  
#ifdef DEBUG
                    Serial.println((String) "Value of: " + telegramObjects[i].name + " with value: " + newValue + " old " + telegramObjects[i].value + " was rejected due to threshold");
#endif
                }
                telegramObjects[i].value = newValue;
            }
#ifdef DEBUG
            Serial.println((String) "Found a Telegram object: " + telegramObjects[i].name + " value: " + telegramObjects[i].value);
#endif
            break;
        }
    }

    return validCRCFound;
}

bool readP1Serial()
{
    if (Serial.available())
    {
#ifdef DEBUG
        Serial.println("Serial is available");
        Serial.println("Memset telegram");
#endif
        memset(telegram, 0, sizeof(telegram));
        while (Serial.available())
        {
            // Reads the telegram untill it finds a return character
            // That is after each line in the telegram
            int len = Serial.readBytesUntil('\n', telegram, P1_MAXLINELENGTH);

            telegram[len] = '\n';
            telegram[len + 1] = 0;

            bool result = decodeTelegram(len + 1);
            // When the CRC is check which is also the end of the telegram
            // if valid decode return true
            //result = true;
            if (result)
            {
                return true;
            }
        }
    }
    return false;
}
