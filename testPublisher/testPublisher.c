#include "legato.h"
#include "interfaces.h"

char deviceIMEI[16];
mqtt_SessionRef_t mqttSession;


void onConnectionLost(void* context)
{
    LE_FATAL("Connection lost!");
}


void onMessageArrived
(
    const char* topic,
    const uint8_t* payload,
    size_t payloadLen,
    void* context
)
{
    LE_FATAL("The publisher received a message!");
}


void publishTimerHandler(le_timer_Ref_t timer)
{
    static int messageData = 0;
    uint8_t payload[64];
    sprintf((char*)payload, "{\"value\":%d}", messageData);
    size_t payloadLen = strlen((char*)payload);
    const bool retain = false;
    char publishTopic[64];
    strcpy(publishTopic, deviceIMEI);
    strcat(publishTopic, "/messages/json");
    const le_result_t publishResult = mqtt_Publish(
        mqttSession,
        publishTopic,
        payload,
        payloadLen,
        MQTT_QOS0_TRANSMIT_ONCE,
        retain);
    LE_INFO(
        "Message published with data=%d.  result=%s", messageData, LE_RESULT_TXT(publishResult));
    messageData++;
}


COMPONENT_INIT
{
    const char mqttBrokerURI[] = "ssl://eu.airvantage.net:8883";
    LE_ASSERT_OK(le_info_GetImei(deviceIMEI, NUM_ARRAY_MEMBERS(deviceIMEI)));
    char clientId[32];
    strcpy(clientId, deviceIMEI);
    strcat(clientId, "-pub");
    LE_ASSERT_OK(mqtt_CreateSession(mqttBrokerURI, clientId, &mqttSession));

    const uint8_t mqttPassword[] = {'S', 'W', 'I'};
    const uint16_t keepAliveInSeconds = 60;
    const bool cleanSession = true;
    const char* username = deviceIMEI;
    const uint16_t connectTimeout = 20;
    const uint16_t retryInterval = 10;
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
