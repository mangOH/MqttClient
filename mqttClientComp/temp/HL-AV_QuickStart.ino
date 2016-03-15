/*
 * Sample sketch using SWIR_MQTTClient library. V0.92
 *
 * Purpose:
 *      Enable Sierra Wireless HL serie module to send/receive MQTT data to/from AirVantage server
 *
 * Tested Platforms:
 *      This source code has been tested on:
 *          - Arduino Mega2560, the extension of this file must be "ino"
 *          - STNucleo L053R8 (mbed), the extension of this file must be "cpp"
 *      Set the targeted platform in "swir_platform_def.h"
 *
 * Requirements:
 *      Paho's Embedded MQTTClient C client shall be installed within Arduino's IDE or mbed's online compiler 
 *      HL module shall be inserted into the "HL shield", stacked over Arduino Mega2560 or STNuleo L053R8 (tested)
 *      HL Shiled's Pin 0 (RX) & Pin 1 (TX) are physically linked to HL module's UART using jumpers J6 & J8 (HL Shield)
 *      SIM card inserted into slot 1 & Antenna connected
 *      For program log output:
 *          Arduino: USB to TTL Serial Cable's RX/TX are connected to HL Shield's Pin2 & Pin8 (by default)
 *          STNucleo: USB to TTL Serial Cable's RX/TX are connected to Nucleo's D2/RX & D8/TX pins (by default)
 *      Register your module on AirVantage portal (IMEI & SN may be copied from the program log output)
 *      Assign the password (as set in AirVantage portal) to the flag below: MODULE_AV_PASSWORD
 *
 * Sample Scenarios:
 *      This sample code provides 2 scenarios:
 *      - Publish elapsed time (ms) to AirVantage every 10s (flag ALARM_SCENARIO must be desactivated)
 *      - Simple Alarm System can be turned on/off on AirVantage portal. When armed, it reads PIR sensor and publish motion
 *        detection status to AirVantage
 *
 * Create your own App:
 *      Use this sample sketch as a starting point to create your app:
 *      Provide your one-time setup code in doCustomSetup(), instead of modifying setup()
 *      Place your code to perform task on a regular basis in doCustomLoopTask(), instead of modifying loop()
 *      Align your Publish/Subscribe keys to your AirVantage Application Model
 *      Handle incoming messages from AirVantage in messageArrived()
 *      Call postData(keyName, keyValue) to publish key/value data to AirVantage
 *
 * Nhon Chu, May 2015
 *
 */


#include "swir_platform_def.h"                                          //define the Targeted Platform (Arduino or mBed) in this include file


#ifdef TARGET_ARDUINO
    #include <SPI.h>
    #include <SoftwareSerial.h>
#endif

#include "MQTTClient.h"                                                 //Eclipse Paho's MQTT Client interface
#include "swir_debug.h"
#include "swir_mqtt.h"                                                  //Sierra Wireless MQTT Client Wrapper interface for HL serie Modules


//--------------------------
//--- Baseline Configuration
#define APN                     "internet.maingate.se"                  //APN for Maingate
#define APN_LOGIN               ""                                      //APN login
#define APN_PASSWORD            ""                                      //APN password

#define SIM_PIN_CODE            0                                       //default is 0000, it's an integer!

#define BAUDRATE                115200                                  //Default baudrate for HL Serie Modules

#define AUTH_WITH_MODULE_IMEI                                           //Use Module's IMEI and MODULE_AV_PASSWORD for authentication
#define MODULE_AV_PASSWORD      "sierra"                                //Specify your module's password, as set in AirVantage

                                                                        //disable AUTH_WITH_MODULE_IMEI to use below alternate login/pwd
#define ALTERNATE_IDENTIFIER    "359569050023259"                       //Specify your system's identifier (alternate option)
#define ALTERNATE_PASSWORD      "toto"                                  //Specify your system's password (alternate option)

#define QOS_PUBLISH             0                                       //Level of Quality of Service: could be 0, 1 or 2

#define TRACE_ENABLE                                                    //Trace toggle. Disable this flag to turn off tracing capability

#ifdef TARGET_MBED
#define TRACE_RX_PIN            D2                                      //Tracing Debug info using USB to TTL serial cable, D2 RX PIN
#define TRACE_TX_PIN            D8                                      //Tracing Debug info using USB to TTL serial cable, D8 TX PIN
#endif

#ifdef TARGET_ARDUINO
#define TRACE_RX_PIN            2                                       //Tracing Debug info using USB to TTL serial cable, RX PIN
#define TRACE_TX_PIN            8                                       //Tracing Debug info using USB to TTL serial cable, TX PIN
#endif

#define DELAY_TRY_CONNECTION    5000                                    //Delay (ms) to make new attempt to connect to AirVantage
#define DELAY_DO_TASK           10000                                   //Periodical delay (ms) to execute application defined task

#define ON_VALUE                "true"                                  //default positive value for a boolean data key

#define PUBLISH_DATA_NAME       "MilliSec"                              //Name of the data being published to AirVantage
#define INCOMING_DATA_NAME      "home.TurnOn"                           //default incoming data key

//-----------------------------
//--- Baseline Global variables
#ifdef TARGET_MBED
MODULE_SERIAL_CLASS             _moduleSerial(SERIAL_TX, SERIAL_RX);    //Use STNucleo's D1 (TX) and D0 (RX) pins to communicate with HL Module, if (SB13 & SB14 OFF, SB62 & SB63 ON)
                                                                        //or use Nucleo's CN3 RX/TX to communicate with HL Module, if (SB13 & SB14 ON, SB62 & SB63 OFF)
SWIR_MQTTClient                 _mqttClient(_moduleSerial);             //Use predefined serial object to communicate with HL module
#endif

#ifdef TARGET_ARDUINO
SWIR_MQTTClient                 _mqttClient(MODULE_SERIAL_OBJECT);      //Use Arduino's Hardware serial to communicate with HL module
#endif

int                             _nIsSimReady = 0;                       //Track SIM state, set SIM PIN only once


//-----------------------------
//Alarm specific scenario
//#define ALARM_SCENARIO                                                //enable this flag to implement the Simple Alarm scenario

#ifdef ALARM_SCENARIO
#ifdef TARGET_MBED
#define ALARM_STATUS_LED_PIN    D9                                      // The LED is connected to D9 pin
#define PIR_PIN                 D10                                     // PIR's Output is connected to D10 pin

DigitalOut                      _ledAlarmOnOff(ALARM_STATUS_LED_PIN);   //variable binding to this output pin
DigitalIn                       _pirState(PIR_PIN);                     //variable binding to this input pin
#endif

#ifdef TARGET_ARDUINO
#define ALARM_STATUS_LED_PIN    9                                       // The LED is connected to Pin #8
#define PIR_PIN                 10                                      // PIR's Output is connected to Pin #9
#endif

#undef  PUBLISH_DATA_NAME
#define PUBLISH_DATA_NAME       "home.intrusion"                        //Name of the data being published to AirVantage
#undef  INCOMING_DATA_NAME
#define INCOMING_DATA_NAME      "home.TurnOn.Alarm"                     //default incoming data key
    
//--- Alarm app Global variables
int                             _nIsAlarmOn = 0;                        //Alarm status (on/off)

#endif  //ALARM_SCENARIO

//-----------------------------
//--- Functions declaration
void doCustomSetup();
void doCustomLoopTask();
int messageArrived(const char* szKey, const char* szValue, const char* szTimestamp);


void setup()
{
    _mqttClient.setBaudRate(BAUDRATE);                                  //Select the working baud rate for the HL Module

    #ifdef TRACE_ENABLE                                                 //USB to TTL serial cable connected to pre-defined Pins
    _mqttClient.setDebugPort(TRACE_RX_PIN, TRACE_TX_PIN, BAUDRATE);
    #endif                                                              //from this point, we can use SWIR_TRACE macro

    SWIR_TRACE(F("Hello, let's get started !\r\nBoot your HL Module now!"));

    _mqttClient.setPublishQos(QOS_PUBLISH);                             //Set Quality of Service

    doCustomSetup();                                                    //provide your one-time setup code in doCustomSetup()
}

void loop()
{
    if (!_mqttClient.isConnected())
    {
        if (!_nIsSimReady)
        {
            _nIsSimReady = _mqttClient.setSimPIN(SIM_PIN_CODE);         //set PIN code
        }

        if (_mqttClient.isDataCallReady())
        {
            _mqttClient.setAPN(APN, APN_LOGIN, APN_PASSWORD);           //set APN

            #ifdef AUTH_WITH_MODULE_IMEI
            _mqttClient.connect(MODULE_AV_PASSWORD);                    //recommended option, try establishing mqtt session
            #else
            _mqttClient.connect(ALTERNATE_IDENTIFIER, ALTERNATE_PASSWORD);      //alternative option, use a different identifier/pwd
            #endif

            if (_mqttClient.isConnected())
            {
                _mqttClient.subscribe(messageArrived);                  //if connected, then provided the callback to receive mqtt msg from AirVantage
            }
        }        
        DELAY_MS(DELAY_TRY_CONNECTION);                                 //if not connected, let's retry in DELAY_TRY_CONNECTION (ms)
    }
    else
    {
        int nRSSI, nBER, nEcLo;
                                                                        //retrieving RSSI (Received Signal Strength Indication)
        if (_mqttClient.getSignalQuality(nRSSI, nBER, nEcLo)) 
        {
            if (nEcLo == -99)
            {
                SWIR_TRACE(F("2G signal strength: RSSI=%d"), nRSSI);
            }
            else
            {
                SWIR_TRACE(F("3G signal strength: RSSI=%d"), nRSSI);
            }
        }
        _mqttClient.loop();                                             //Must be called. Enable SWIR_MQTTClient to handle background tasks

        doCustomLoopTask();                                             //let's perform this task on a regular basis
        
        DELAY_MS(DELAY_DO_TASK);                                        //let's do this again in DELAY_DO_TASK (ms)
    }
}

void postData(char* szDataKey, unsigned long ulDataValue)
{
    if (_mqttClient.isConnected())
    {
        _mqttClient.publish(szDataKey, ulDataValue);
    }
}

void doCustomSetup()
{
    //Place your one-time setup code here

    #ifdef ALARM_SCENARIO
    #ifdef TARGET_ARDUINO
    pinMode(ALARM_STATUS_LED_PIN, OUTPUT);
    pinMode(PIR_PIN, INPUT);
    #endif
    #endif
}

void doCustomLoopTask()
{
    //Replace the below sample task by your code to be executed on a regular basis

#ifdef ALARM_SCENARIO
    if (!_nIsAlarmOn)
    {
        return;                                     //Alarm system is OFF, nothing to do
    }

    //should handle toogle delay... just make it simple for the tutorial
    #ifdef TARGET_ARDUINO
    if (digitalRead(PIR_PIN) == HIGH)
    #endif
    #ifdef TARGET_MBED
    if (_pirState)
    #endif
    {
        postData(PUBLISH_DATA_NAME, 1);             //Motion detected, let's post this event to AV
    }
    else
    {
        postData(PUBLISH_DATA_NAME, 0);             //No Motion detected, let's post this event to AV
    }
#else
    unsigned long ulElapsedSeconds = 0;
    
    #ifdef TARGET_MBED
    ulElapsedSeconds = time(NULL);
    #endif

    #ifdef TARGET_ARDUINO
    ulElapsedSeconds = millis();
    #endif

    postData(PUBLISH_DATA_NAME, ulElapsedSeconds);  //let's publish the number of milliseconds since the device has started
#endif
}


int messageArrived(const char* szKey, const char* szValue, const char* szTimestamp)
{
    SWIR_TRACE(F("\r\n>>> Message from AirVantage: %s = %s @ %s\r\n"), szKey, szValue, szTimestamp);
    
    //based on szKey and szValue (depending on Application Model defined in AirVantage), you can trigger an action...

    STRING_CLASS sKey = STRING_CLASS(szKey);
    STRING_CLASS sValue = STRING_CLASS(szValue);

    if (sKey == INCOMING_DATA_NAME)
    {
        if (sValue == ON_VALUE)
        {
            //perform action corresponding to INCOMING_DATA_NAME = ON_VALUE
            #ifdef ALARM_SCENARIO
            #ifdef TARGET_ARDUINO
            digitalWrite(ALARM_STATUS_LED_PIN, HIGH);
            #else
            _ledAlarmOnOff = true;
            #endif
            _nIsAlarmOn = 1;
            #endif
        }
        else
        {
            //Perform action corresponding to INCOMING_DATA_NAME != ON_VALUE
            #ifdef ALARM_SCENARIO
            #ifdef TARGET_ARDUINO
            digitalWrite(ALARM_STATUS_LED_PIN, LOW);
            #else
            _ledAlarmOnOff = false;
            #endif
            _nIsAlarmOn = 0;

            postData(PUBLISH_DATA_NAME, 0);
            #endif
        }
        
        return 0;
    }
    
    return 1;   //unknown key
}

#ifdef TARGET_MBED
int main()
{
    setup();
    
    while (1)
    {
        loop();
    }
}
#endif