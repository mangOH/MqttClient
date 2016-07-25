#include "legato.h"
#include "interfaces.h"

const char mqttBrokerURI[] = "tcp://eu.airvantage.net:1883";
// TODO: read using API
const char deviceIMEI[] = "359377060016085";
const uint8_t mqttPassword[] = {'S', 'W', 'I'};
const char publishTopic[] = "359377060016085/messages/json";

mqtt_SessionRef_t mqttSession;


void onConnectionLost(void* context)
{
    LE_FATAL("Connection lost!");
}


void onMessageArrived(mqtt_MessageRef_t msg, void* context)
{
    LE_FATAL("The publisher received a message!");
}


void publishTimerHandler(le_timer_Ref_t timer)
{
    static int messageData = 0;
    uint8_t payload[32];
    sprintf((char*)payload, "{ \"value\": %d}", messageData);
    size_t payloadLen = strlen((char*)payload);
    const bool retain = false;
    mqtt_Publish(
        mqttSession,
        publishTopic,
        payload,
        payloadLen,
        MQTT_QOS0_TRANSMIT_ONCE,
        retain);
    LE_INFO("Message published with data=%d", messageData);
    messageData++;
}


COMPONENT_INIT
{
    char clientId[32];
    strcpy(clientId, deviceIMEI);
    strcat(clientId, "-pub");
    LE_ASSERT_OK(mqtt_CreateSession(mqttBrokerURI, clientId, &mqttSession));

    const uint16_t keepAliveInSeconds = 60;
    const bool cleanSession = true;
    const char* username = deviceIMEI;
    uint16_t connectTimeout = 20;
    uint16_t retryInterval = 10;
    mqtt_SetConnectOptions(
        mqttSession,
        keepAliveInSeconds,
        cleanSession,
        username,
        mqttPassword,
        NUM_ARRAY_MEMBERS(mqttPassword),
        connectTimeout,
        retryInterval);

    mqtt_AddConnectionLostHandler(mqttSession, &onConnectionLost, NULL);
    mqtt_AddMessageArrivedHandler(mqttSession, &onMessageArrived, NULL);

    LE_FATAL_IF(mqtt_Connect(mqttSession) != LE_OK, "Connection failed");

    le_timer_Ref_t timer = le_timer_Create("MQTT Publish");
    LE_ASSERT_OK(le_timer_SetHandler(timer, &publishTimerHandler));
    LE_ASSERT_OK(le_timer_SetMsInterval(timer, 5000));
    LE_ASSERT_OK(le_timer_SetRepeat(timer, 0));
    LE_ASSERT_OK(le_timer_Start(timer));
    LE_INFO("Publish timer started");
}

