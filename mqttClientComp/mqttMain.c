/**
 * This module implements MQTT mqttClient_data_t.
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 *
 */
#include "legato.h"
#include "interfaces.h"
#include "json/swir_json.h"
#include "mqttMain.h"

static mqttClient_t mqttClient;

static int mqttMain_SendMessage(const char*, const char*);
static void mqttMain_SessionStateHandler(void*, void*);
static void mqttMain_IncomingMessageHandler(void*, void*);
static void mqttMain_SigTermEventHandler(int);

static int mqttMain_SendMessage(const char* key, const char* value)
{
  char* payload = swirjson_szSerialize(key, value, 0);

  mqttClient_msg_t msg;
  msg.qos = mqttClient.session.config.QoS;
  msg.retained = 0;
  msg.dup = 0;
  msg.id = 0;
  msg.payload = payload;
  msg.payloadLen = strlen(payload);

  char* topic = malloc(strlen(MQTT_CLIENT_TOPIC_NAME_PUBLISH) + strlen(mqttClient.deviceId) + 1);
  if (!topic)
  {
    LE_ERROR("malloc() failed");
    goto cleanup;
  }

  sprintf(topic, "%s%s", mqttClient.deviceId, MQTT_CLIENT_TOPIC_NAME_PUBLISH);
  LE_INFO("topic('%s') payload('%s')", topic, payload);

  int rc = mqttClient_publish(&mqttClient, topic, &msg);
  if (rc)
  {
    LE_ERROR("mqttClient_publish() failed(%d)", rc);
    goto cleanup;
  }

cleanup:
  if (topic) free(topic);
  if (payload) free(payload);
  return rc;
}

static void mqttMain_IncomingMessageHandler(void* reportPtr, void* incomingMessageHandler)
{
  mqttClient_inMsg_t* eventDataPtr = reportPtr;
  mqttApi_IncomingMessageHandlerFunc_t clientHandlerFunc = incomingMessageHandler;

  LE_ASSERT(reportPtr);
  LE_ASSERT(incomingMessageHandler);

  LE_DEBUG("topic('%s') key('%s') value('%s') ts('%s')", eventDataPtr->topicName, eventDataPtr->keyName, eventDataPtr->value, eventDataPtr->timestamp);
  clientHandlerFunc(eventDataPtr->topicName,
                    eventDataPtr->keyName,
                    eventDataPtr->value,
                    eventDataPtr->timestamp,
                    le_event_GetContextPtr());
}

static void mqttMain_SessionStateHandler(void* reportPtr, void* sessionStateHandler)
{
  mqttClient_connStateData_t* eventDataPtr = reportPtr;
  mqttApi_SessionStateHandlerFunc_t clientHandlerFunc = sessionStateHandler;

  clientHandlerFunc(eventDataPtr->isConnected,
                    eventDataPtr->connectErrorCode,
                    eventDataPtr->subErrorCode,
                    le_event_GetContextPtr());
}

static void mqttMain_SigTermEventHandler(int sigNum)
{
  LE_INFO("disconnect");
  mqttClient_disconnectData(&mqttClient);
}

__inline mqttClient_t* mqttMain_getClient(void)
{
  return &mqttClient;
};

void mqttApi_Config(const char* brokerUrl, int32_t portNumber, int32_t keepAlive, int32_t QoS)
{
  if (strlen(brokerUrl) > 0)
  {
    LE_INFO("MQTT Broker URL('%s' -> '%s')", mqttClient.config.brokerUrl, brokerUrl);
    strcpy(mqttClient.config.brokerUrl, brokerUrl); 
  }

  if (portNumber != -1)
  {
    LE_INFO("MQTT Broker Port(%d -> %d)", mqttClient.config.portNumber, portNumber);
    mqttClient.config.portNumber = portNumber;
  }

  if (keepAlive != -1)
  {
    LE_INFO("Keep Alive(%d -> %d seconds)", mqttClient.config.keepAlive, keepAlive);
    mqttClient.config.keepAlive = keepAlive;
  } 

  if (QoS != -1)
  {
    LE_INFO("QoS(%d -> %d)", mqttClient.config.QoS, QoS);
    mqttClient.config.QoS = QoS;
  } 
}

void mqttApi_Connect(const char* user, const char* password)
{
  LE_INFO("connect user('%s') password('%s')", user, password);
  mqttClient_connectUser(&mqttClient, user, password);
}

void mqttApi_Disconnect(void)
{
  LE_INFO("disconnect");
  mqttClient_disconnectData(&mqttClient);
}

void mqttApi_Send(const char* key, const char* value, int32_t* returnCode)
{
  int32_t rc = LE_OK;

  LE_INFO("send key('%s') value('%s')", key, value);
  rc = mqttMain_SendMessage(key, value);
  if (rc)
  {
    LE_ERROR("mqttMain_SendMessage() failed(%d)", rc);
    goto cleanup;
  }

cleanup:
  *returnCode = rc;
  return;
}

mqttApi_SessionStateHandlerRef_t mqttApi_AddSessionStateHandler(mqttApi_SessionStateHandlerFunc_t handlerPtr, void* contextPtr)
{
  LE_DEBUG("add session state handler(%p)", handlerPtr);
  le_event_HandlerRef_t handlerRef = le_event_AddLayeredHandler("MqttConnState",
                                                                mqttClient.connStateEvent,
                                                                mqttMain_SessionStateHandler,
                                                                (le_event_HandlerFunc_t)handlerPtr);

  le_event_SetContextPtr(handlerRef, contextPtr);
  return (mqttApi_SessionStateHandlerRef_t)(handlerRef);
}

void mqttApi_RemoveSessionStateHandler(mqttApi_SessionStateHandlerRef_t addHandlerRef)
{
  LE_DEBUG("remove session state handler(%p)", addHandlerRef);
  le_event_RemoveHandler((le_event_HandlerRef_t)addHandlerRef);
}

mqttApi_IncomingMessageHandlerRef_t mqttApi_AddIncomingMessageHandler(mqttApi_IncomingMessageHandlerFunc_t handlerPtr, void* contextPtr)
{
  LE_DEBUG("add incoming message handler(%p)", handlerPtr);
  le_event_HandlerRef_t handlerRef = le_event_AddLayeredHandler("MqttIncomingMessage",
                                                                mqttClient.inMsgEvent,
                                                                mqttMain_IncomingMessageHandler,
                                                                (le_event_HandlerFunc_t)handlerPtr);

  le_event_SetContextPtr(handlerRef, contextPtr);
  return (mqttApi_IncomingMessageHandlerRef_t)(handlerRef);
}

void mqttApi_RemoveIncomingMessageHandler(mqttApi_IncomingMessageHandlerRef_t addHandlerRef)
{
  LE_DEBUG("remove incoming message handler(%p)", addHandlerRef);
  le_event_RemoveHandler((le_event_HandlerRef_t)addHandlerRef);
}

COMPONENT_INIT
{
  LE_INFO("Init mqttClient");

  le_sig_Block(SIGTERM);
  le_sig_SetEventHandler(SIGTERM, mqttMain_SigTermEventHandler);

  mqttClient_init(&mqttClient);
}

