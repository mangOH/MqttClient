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
    char payloadStr[payloadLen + 1];
    memcpy(payloadStr, payload, payloadLen);
    payloadStr[payloadLen] = '\0';
    LE_INFO("Received message! topic: \"%s\", payload: \"%s\"", topic, payloadStr);
}


COMPONENT_INIT
{
    const char mqttBrokerURI[] = "ssl://eu.airvantage.net:8883";
    LE_ASSERT_OK(le_info_GetImei(deviceIMEI, NUM_ARRAY_MEMBERS(deviceIMEI)));
    char clientId[32];
    strcpy(clientId, deviceIMEI);
    strcat(clientId, "-sub");
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

    char subscribeTopic[64];
    strcpy(subscribeTopic, deviceIMEI);
    strcat(subscribeTopic, "/messages/json");
    LE_FATAL_IF(
        mqtt_Subscribe(mqttSession, subscribeTopic, MQTT_QOS0_TRANSMIT_ONCE) != LE_OK,
        "failed to subscribe to %s",
        subscribeTopic);
    LE_INFO("Subscribed to topic (%s)", subscribeTopic);

    strcpy(subscribeTopic, deviceIMEI);
    strcat(subscribeTopic, "/errors");
    LE_FATAL_IF(
        mqtt_Subscribe(mqttSession, subscribeTopic, MQTT_QOS0_TRANSMIT_ONCE) != LE_OK,
        "failed to subscribe to %s",
        subscribeTopic);
    LE_INFO("Subscribed to topic (%s)", subscribeTopic);
}
