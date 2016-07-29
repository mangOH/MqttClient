#include "legato.h"
#include "interfaces.h"

const char mqttBrokerURI[] = "tcp://eu.airvantage.net:1883";
// TODO: read using API
const char deviceIMEI[] = "359377060016085";
const uint8_t mqttPassword[] = {'S', 'W', 'I'};
const char subscribeTopic[] = "359377060016085/messages/json";

mqtt_SessionRef_t mqttSession;


void onConnectionLost(void* context)
{
    LE_FATAL("Connection lost!");
}


void onMessageArrived(mqtt_MessageRef_t msg, void* context)
{
    char topic[1024];
    size_t topicLength = sizeof(topic);
    uint8_t payload[1024];
    size_t payloadLength = sizeof(payload);
    mqtt_GetMessage(msg, topic, topicLength, payload, &payloadLength);

    // We are going to treat the payload as a null terminated string when we print it, so add the
    // terminator
    payload[payloadLength < sizeof(payload) ? payloadLength : sizeof(payload) - 1] = '\0';
    LE_INFO("Received message! topic: \"%s\", payload: \"%s\"", topic, (char*)payload);
}


COMPONENT_INIT
{
    char clientId[32];
    strcpy(clientId, deviceIMEI);
    strcat(clientId, "-sub");
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

    mqtt_Subscribe(mqttSession, subscribeTopic, MQTT_QOS0_TRANSMIT_ONCE);
    LE_INFO("Subscribed to topic (%s)", subscribeTopic);
}

