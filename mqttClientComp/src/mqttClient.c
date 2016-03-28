/*******************************************************************************
 * Copyright (c) 2014 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Allan Stockdill-Mander/Ian Craggs - initial API and implementation and/or initial documentation
 *******************************************************************************/
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "legato.h"
#include "interfaces.h"
#include "json/swir_json.h"
#include "mqttClient.h"

static void mqttClient_newMsgData(mqttClient_msg_data_t*, MQTTString*, mqttClient_msg_t*);
static int mqttClient_getNextPacketId(mqttClient_t*);
static char mqttClient_isTopicMatched(char*, MQTTString*);
static int mqttClient_deliverMsg(mqttClient_t*, MQTTString*, mqttClient_msg_t*);

static void mqttClient_SendConnStateEvent(bool, int32_t, int32_t);
static void mqttClient_SendIncomingMessageEvent(const char*, const char*, const char*, const char*);

static void mqttClient_connExpiryHndlr(le_timer_Ref_t);
static void mqttClient_cmdExpiryHndlr(le_timer_Ref_t);
static void mqttClient_pingExpiryHndlr(le_timer_Ref_t);

static int mqttClient_sendConnect(mqttClient_t*, MQTTPacket_connectData*);
static int mqttClient_sendPublishAck(const char*, int, const char*);
static void mqttClient_onIncomingMessage(mqttClient_msg_data_t*);

static int mqttClient_processConnAck(mqttClient_t*);
static int mqttClient_processSubAck(mqttClient_t*);
static int mqttClient_processUnSubAck(mqttClient_t*);
static int mqttClient_processPubAck(mqttClient_t*);
static int mqttClient_processPubComp(mqttClient_t*);
static int mqttClient_processPublish(mqttClient_t*);
static int mqttClient_processPubRec(mqttClient_t*);
static void mqttClient_processPingResp(mqttClient_t*);

static void mqttClient_dataConnectionStateHandler(const char*, bool, void*);
static void mqttClient_socketFdEventHandler(int, short);

static int mqttClient_connect(mqttClient_t*);
static int mqttClient_close(mqttClient_t*);
static int mqttClient_write(mqttClient_t*, int);

static const char* mqttClient_connectionRsp(uint8_t);
static void mqttClient_dumpBuffer(const unsigned char*, unsigned int);

static int mqttClient_startSession(mqttClient_t*);
static int mqttClient_connectData(mqttClient_t*);

static void mqttClient_dumpBuffer(const unsigned char* buff, unsigned int len)
{
    unsigned char* ptr = (unsigned char*)buff;
    unsigned int idx = 0;

    LE_ASSERT(buff);

    for (idx = 0; idx < len;)
    {
        if ((len - idx) >= 16)
        {
            LE_DEBUG("0x%02x%02x%02x%02x 0x%02x%02x%02x%02x 0x%02x%02x%02x%02x 0x%02x%02x%02x%02x",
                    ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7],
                    ptr[8], ptr[9], ptr[10], ptr[11], ptr[12], ptr[13], ptr[14], ptr[15]);
            idx += 16;
            ptr += 16;
        }
        else
        {
            switch(len - idx)
            {
            case 15:
                LE_DEBUG("0x%02x%02x%02x%02x 0x%02x%02x%02x%02x 0x%02x%02x%02x%02x 0x%02x%02x%02x",
                    ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7],
                    ptr[8], ptr[9], ptr[10], ptr[11], ptr[12], ptr[13], ptr[14]);
                break;

            case 14:
                LE_DEBUG("0x%02x%02x%02x%02x 0x%02x%02x%02x%02x 0x%02x%02x%02x%02x 0x%02x%02x",
                    ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7],
                    ptr[8], ptr[9], ptr[10], ptr[11], ptr[12], ptr[13]);
                break;

            case 13:
                LE_DEBUG("0x%02x%02x%02x%02x 0x%02x%02x%02x%02x 0x%02x%02x%02x%02x 0x%02x",
                    ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7],
                    ptr[8], ptr[9], ptr[10], ptr[11], ptr[12]);
                break;

            case 12:
                LE_DEBUG("0x%02x%02x%02x%02x 0x%02x%02x%02x%02x 0x%02x%02x%02x%02x",
                    ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7],
                    ptr[8], ptr[9], ptr[10], ptr[11]);
                break;

            case 11:
                LE_DEBUG("0x%02x%02x%02x%02x 0x%02x%02x%02x%02x 0x%02x%02x%02x",
                    ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7],
                    ptr[8], ptr[9], ptr[10]);
                break;

            case 10:
                LE_DEBUG("0x%02x%02x%02x%02x 0x%02x%02x%02x%02x 0x%02x%02x",
                    ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7],
                    ptr[8], ptr[9]);
                break;

            case 9:
                LE_DEBUG("0x%02x%02x%02x%02x 0x%02x%02x%02x%02x 0x%02x",
                    ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7],
                    ptr[8]);
                break;

            case 8:
                LE_DEBUG("0x%02x%02x%02x%02x 0x%02x%02x%02x%02x", ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6], ptr[7]);
                break;

            case 7:
                LE_DEBUG("0x%02x%02x%02x%02x 0x%02x%02x%02x", ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5], ptr[6]);
                break;

            case 6:
                LE_DEBUG("0x%02x%02x%02x%02x 0x%02x%02x", ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5]);
                break;

            case 5:
                LE_DEBUG("0x%02x%02x%02x%02x 0x%02x", ptr[0], ptr[1], ptr[2], ptr[3], ptr[4]);
                break;

            case 4:
                LE_DEBUG("0x%02x%02x%02x%02x", ptr[0], ptr[1], ptr[2], ptr[3]);
                break;

            case 3:
                LE_DEBUG("0x%02x%02x%02x", ptr[0], ptr[1], ptr[2]);
                break;

            case 2:
                LE_DEBUG("0x%02x%02x", ptr[0], ptr[1]);
                break;

            case 1:
                LE_DEBUG("0x%02x", ptr[0]);
                break;

            default:
                break;
            }

            idx = len;
        }
    }
}

static void mqttClient_newMsgData(mqttClient_msg_data_t* msgData, MQTTString* topicName, mqttClient_msg_t* msg) 
{
  LE_ASSERT(msgData);
  LE_ASSERT(topicName);
  LE_ASSERT(msg);

  msgData->topicName = topicName;
  msgData->message = msg;
}

static int mqttClient_getNextPacketId(mqttClient_t* clientData) 
{
  LE_ASSERT(clientData);
  return clientData->session.nextPacketId = (clientData->session.nextPacketId == MQTT_CLIENT_MAX_PACKET_ID) ? 1 : clientData->session.nextPacketId + 1;
}

static void mqttClient_SendConnStateEvent(bool isConnected, int32_t connectErrorCode, int32_t subErrorCode)
{
  mqttClient_connStateData_t eventData;
  mqttClient_t* clientData = mqttMain_getClient();

  eventData.isConnected = isConnected;
  eventData.connectErrorCode = connectErrorCode;
  eventData.subErrorCode = subErrorCode;

  LE_DEBUG("MQTT connected(%d), error(%d), sub-error(%d)", eventData.isConnected, eventData.connectErrorCode, eventData.subErrorCode);
  le_event_Report(clientData->connStateEvent, &eventData, sizeof(eventData));
}

static void mqttClient_SendIncomingMessageEvent(const char* topicName, const char* keyName, const char* value, const char* timestamp)
{
  mqttClient_inMsg_t eventData;
  mqttClient_t* clientData = mqttMain_getClient();

  strcpy(eventData.topicName, topicName);
  strcpy(eventData.keyName, keyName);
  strcpy(eventData.value, value);
  strcpy(eventData.timestamp, timestamp);

  LE_DEBUG("Send MQTT incoming message('%s', ['%s':'%s'@'%s'])", eventData.topicName, eventData.keyName, eventData.value, eventData.timestamp);
  le_event_Report(clientData->inMsgEvent, &eventData, sizeof(eventData));
}

static int mqttClient_sendConnect(mqttClient_t* clientData, MQTTPacket_connectData* connectData)
{
  int rc = LE_OK;

  LE_ASSERT(clientData);
  LE_ASSERT(connectData);

  if (clientData->session.isConnected)
  {
    LE_WARN("already connected"); 
    goto cleanup;
  } 
    
  int len = MQTTSerialize_connect(clientData->session.tx.buf, sizeof(clientData->session.tx.buf), connectData);
  if (len <= 0)
  {
    LE_ERROR("MQTTSerialize_connect() failed(%d)", len);
    rc = len;
    goto cleanup;
  }

  LE_DEBUG("<--- CONNECT");
  rc = mqttClient_write(clientData, len);
  if (rc)
  {  
    LE_ERROR("mqttClient_write() failed(%d)", rc);
    goto cleanup;
  } 

  rc = le_timer_Start(clientData->session.cmdTimer);
  if (rc)
  {
    LE_ERROR("le_timer_Start() failed(%d)", rc);
    goto cleanup;
  }

cleanup:
  return rc;
}

static int mqttClient_sendPublishAck(const char* uid, int nAck, const char* message)
{
  mqttClient_t* clientData = mqttMain_getClient();
  char* payload = (char*) malloc(strlen(uid) + strlen(message)+48);

  if (nAck == 0)
  {
    sprintf(payload, "[{\"uid\": \"%s\", \"status\" : \"OK\"", uid);
  }
  else
  {
    sprintf(payload, "[{\"uid\": \"%s\", \"status\" : \"ERROR\"", uid);
  }

  if (strlen(message) > 0)
  {
    sprintf(payload, "%s, \"message\" : \"%s\"}]", payload, message);
  }
  else
  {     
    sprintf(payload, "%s}]", payload);
  }

  LE_DEBUG("ACK Message('%s')", payload);

  mqttClient_msg_t msg;
  msg.qos = clientData->session.config.QoS;
  msg.retained = 0;
  msg.dup = 0;
  msg.id = 0;
  msg.payload = payload;
  msg.payloadLen = strlen(payload);

  char* topic = malloc(strlen(MQTT_CLIENT_TOPIC_NAME_ACK) + strlen(clientData->deviceId) + 1);
  sprintf(topic, "%s%s", clientData->deviceId, MQTT_CLIENT_TOPIC_NAME_ACK);

  LE_INFO("<--- PUBLISH('%s')", topic);
  int rc = mqttClient_publish(clientData, topic, &msg);
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

static void mqttClient_onIncomingMessage(mqttClient_msg_data_t* md)
{
  char topicName[MQTT_CLIENT_TOPIC_NAME_LEN + 1] = {0};
  mqttClient_msg_t* message = md->message;
  char* payload = NULL;
  char* command = NULL;
  char* uid = NULL;
  char* timestamp = NULL;
  char* id = NULL;
  char* param = NULL;
  int32_t rc = LE_OK;

  memcpy(topicName, md->topicName->lenstring.data, md->topicName->lenstring.len);
  topicName[md->topicName->lenstring.len] = 0;

  LE_INFO("---> topic('%s') len(%u) data('%s')", topicName, message->payloadLen, message->payload);

  payload = malloc(message->payloadLen + 1);
  if (!payload)
  {
    LE_ERROR("malloc() failed");
    goto cleanup;
  }

  memcpy(payload, (char*)message->payload, message->payloadLen);
  payload[message->payloadLen] = 0;

  command = swirjson_getValue(payload, -1, "command");
  if (command)
  {
    int i = 0;

    uid = swirjson_getValue(payload, -1, "uid");
    timestamp = swirjson_getValue(payload, -1, "timestamp");
    id = swirjson_getValue(command, -1, "id");
    param = swirjson_getValue(command, -1, "params");

    for (i = 0; i < MQTT_CLIENT_AV_JSON_KEY_MAX_COUNT; i++)
    {
      char key[MQTT_CLIENT_AV_JSON_KEY_MAX_LENGTH];

      char* value = swirjson_getValue(param, i, key);
      if (value)
      {
        char fullKey[MQTT_CLIENT_KEY_NAME_LEN + 1];

        LE_DEBUG("--> AV message id('%s') key('%s') value('%s') ts('%s')", id, key, value, timestamp);

        sprintf(fullKey, "%s.%s", id, key);
        mqttClient_SendIncomingMessageEvent(topicName, fullKey, value, timestamp);
        free(value);
      }
      else
      {
        break;
      }
    }

    rc = mqttClient_sendPublishAck(uid, 0, "");
    if (rc)
    {
      LE_ERROR("mqttClient_sendPublishAck() failed(%d)", rc);
      goto cleanup;
    } 
  }
  else
  {
    LE_WARN("failed to find command key");
  }

cleanup:
  if (command) free(command);
  if (id) free(id);
  if (param) free(param);
  if (timestamp) free(timestamp);
  if (uid) free(uid);
  if (payload) free(payload);
}

static void mqttClient_connExpiryHndlr(le_timer_Ref_t timer)
{
  mqttClient_t* clientData = le_timer_GetContextPtr(timer);
  int32_t rc = LE_OK;

  LE_ASSERT(clientData);

  rc = mqttClient_close(clientData);
  if (rc)
  {
    LE_ERROR("mqttClient_close() failed(%d)", rc);
    goto cleanup;
  }

  LE_DEBUG("<--- reconnect");
  rc = mqttClient_connect(clientData);
  if (rc)
  {
    LE_ERROR("mqttClient_connect() failed(%d)", rc);
    goto cleanup;
  }

cleanup:
  return;
}

static void mqttClient_cmdExpiryHndlr(le_timer_Ref_t timer)
{
  mqttClient_t* clientData = le_timer_GetContextPtr(timer);
  int32_t rc = LE_OK;

  LE_ASSERT(clientData);

  if (clientData->session.cmdRetries < MQTT_CLIENT_MAX_SEND_RETRIES)
  {
    if (clientData->session.sock == MQTT_CLIENT_INVALID_SOCKET)
    {
      LE_DEBUG("<--- reconnect");
      rc = mqttClient_connect(clientData);
      if (rc)
      {
        LE_ERROR("mqttClient_connect() failed(%d)", rc);
        goto cleanup;
      }
    }
    else
    {
      LE_DEBUG("<--- resend CMD(%u)", clientData->session.cmdRetries++);
      rc = mqttClient_write(clientData, clientData->session.cmdLen);
      if (rc)
      {
        LE_ERROR("mqttClient_write() failed(%d)", rc);
        goto cleanup;
      }
    }
  }
  else
  {
    LE_ERROR("maximum retries reached(%u)", MQTT_CLIENT_MAX_SEND_RETRIES);
    goto cleanup;
  }

cleanup:
  return;
}

static void mqttClient_pingExpiryHndlr(le_timer_Ref_t timer)
{
  mqttClient_t* clientData = le_timer_GetContextPtr(timer);
  int32_t rc = LE_OK;

  LE_ASSERT(clientData);

  int len = MQTTSerialize_pingreq(clientData->session.tx.buf, sizeof(clientData->session.tx.buf));
  if (len < 0)
  {
    LE_ERROR("MQTTSerialize_pingreq() failed(%d)", len);
    goto cleanup;
  }

  LE_DEBUG("<--- PING");
  rc = mqttClient_write(clientData, len);
  if (rc)
  {
    LE_ERROR("mqttClient_write() failed(%d)", rc);
    goto cleanup;
  }

cleanup:
  return;
}

static const char* mqttClient_connectionRsp(uint8_t rc)
{
  switch(rc)
  {
  case 0: return "Connection Accepted";
  case 1: return "Connection Refused, unacceptable protocol version";
  case 2: return "Connection Refused, identifier rejected";
  case 3: return "Connection Refused, Server unavailable";
  case 4: return "Connection Refused, bad user name or password";
  case 5: return "Connection Refused, not authorized";
  default: break;
  }

  return "N/A";
}

static int mqttClient_processConnAck(mqttClient_t* clientData)
{
  int32_t rc = LE_OK;
  uint8_t sessionPresent = 0;
  uint8_t connack_rc = 0;

  LE_DEBUG("---> CONNACK");
  LE_ASSERT(clientData);

  rc = le_timer_Stop(clientData->session.cmdTimer);
  if (rc)
  {
    LE_ERROR("le_timer_Stop() failed(%d)", rc);
    goto cleanup;
  }


  rc = MQTTDeserialize_connack((unsigned char*)&sessionPresent, &connack_rc, clientData->session.rx.buf, sizeof(clientData->session.rx.buf));
  if (rc != 1)
  {
    LE_ERROR("MQTTDeserialize_connack() failed(%d)", rc);
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

  LE_DEBUG("session present(%u) connection ACK(%u)", sessionPresent, connack_rc);
  clientData->session.isConnected = (connack_rc == MQTT_CLIENT_CONNECT_SUCCESS);

  if (clientData->session.isConnected)
  {
    mqttClient_SendConnStateEvent(true, 0, rc);

    LE_INFO("subscribe('%s')", clientData->subscribeTopic);
    rc = mqttClient_subscribe(clientData, clientData->subscribeTopic, 0, mqttClient_onIncomingMessage);
    if (rc)
    {
      LE_ERROR("mqttClient_subscribe() failed(%d)", rc);
      goto cleanup;
    }
  }
  else
  {
    LE_ERROR("response('%s')", mqttClient_connectionRsp(connack_rc));
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

cleanup:
  return rc;
}

static int mqttClient_processSubAck(mqttClient_t* clientData)
{
  int count = 0;
  int grantedQoS = -1;
  int rc = LE_OK;
  unsigned short packetId = 0;

  LE_DEBUG("---> SUBACK");
  LE_ASSERT(clientData);

  rc = le_timer_Stop(clientData->session.cmdTimer);
  if (rc)
  {
    LE_ERROR("le_timer_Stop() failed(%d)", rc);
    goto cleanup;
  }

  rc = MQTTDeserialize_suback(&packetId, 1, &count, &grantedQoS, clientData->session.rx.buf, sizeof(clientData->session.rx.buf));
  if (rc != 1)
  {
    LE_ERROR("MQTTDeserialize_suback() failed");
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }
  else if (clientData->session.nextPacketId != packetId)
  {
    LE_ERROR("invalid packet ID(%u != %u)", clientData->session.nextPacketId, packetId);
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

  rc = LE_OK;

  LE_DEBUG("returned count(%u) granted QoS(0x%02x)", count, grantedQoS);
  if ((count == 0) && (grantedQoS == 0x80))
  {
    LE_ERROR("no granted QoS");
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

cleanup:
  return rc;
}

static int mqttClient_processUnSubAck(mqttClient_t* clientData)
{
  int32_t rc = LE_OK;
  uint16_t packetId;

  LE_DEBUG("---> UNSUBACK");
  LE_ASSERT(clientData);

  rc = le_timer_Stop(clientData->session.cmdTimer);
  if (rc)
  {
    LE_ERROR("le_timer_Stop() failed(%d)", rc);
    goto cleanup;
  }

  rc = MQTTDeserialize_unsuback(&packetId, clientData->session.rx.buf, sizeof(clientData->session.rx.buf));
  if (rc != 1)
  {
    LE_ERROR("MQTTDeserialize_unsuback() failed");
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }
  else if (clientData->session.nextPacketId != packetId)
  {
    LE_ERROR("invalid packet ID(%u != %u)", clientData->session.nextPacketId, packetId);
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

cleanup:
  return rc;
}

static int mqttClient_processPublish(mqttClient_t* clientData)
{
  MQTTString topicName;
  mqttClient_msg_t msg;
  int len = 0;
  int32_t rc = LE_OK;

  LE_DEBUG("---> PUBLISH");
  LE_ASSERT(clientData);

  if (MQTTDeserialize_publish((unsigned char*)&msg.dup, (int*)&msg.qos, (unsigned char*)&msg.retained, (unsigned short*)&msg.id, &topicName,
          (unsigned char**)&msg.payload, (int*)&msg.payloadLen, clientData->session.rx.buf, sizeof(clientData->session.rx.buf)) != 1)
  {
    LE_ERROR("MQTTDeserialize_publish() failed");
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

  if (msg.qos != MQTT_CLIENT_QOS0)
  {
    if (msg.qos == MQTT_CLIENT_QOS1)
      len = MQTTSerialize_ack(clientData->session.tx.buf, sizeof(clientData->session.tx.buf), PUBACK, 0, msg.id);
    else if (msg.qos == MQTT_CLIENT_QOS2)
      len = MQTTSerialize_ack(clientData->session.tx.buf, sizeof(clientData->session.tx.buf), PUBREC, 0, msg.id);

    if (len <= 0)
    {
      LE_ERROR("MQTTSerialize_ack() failed(%d)", len);
      rc = LE_BAD_PARAMETER;
      goto cleanup;
    }
        
    LE_DEBUG("<--- PUBACK");
    rc = mqttClient_write(clientData, len);
    if (rc)
    {
      LE_ERROR("mqttClient_write() failed(%d)", rc);
      goto cleanup;
    }
  }

  rc = mqttClient_deliverMsg(clientData, &topicName, &msg);
  if (rc)
  {
    LE_ERROR("mqttClient_deliverMsg() failed(%d)", rc);
    goto cleanup;
  }

cleanup:
  return rc;
}

static int mqttClient_processPubComp(mqttClient_t* clientData)
{
  int32_t rc = LE_OK;
  uint16_t packetId;
  uint8_t dup;
  uint8_t type;

  LE_DEBUG("---> PUBCOMP");
  LE_ASSERT(clientData);

  rc = le_timer_Stop(clientData->session.cmdTimer);
  if (rc)
  {
    LE_ERROR("le_timer_Stop() failed(%d)", rc);
    goto cleanup;
  }

  rc = MQTTDeserialize_ack(&type, &dup, &packetId, clientData->session.rx.buf, sizeof(clientData->session.rx.buf));
  if (rc != 1)
  {
    LE_ERROR("MQTTDeserialize_ack() failed");
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }
  else if (clientData->session.nextPacketId != packetId)
  {
    LE_ERROR("invalid packet ID(%u != %u)", clientData->session.nextPacketId, packetId);
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

cleanup:
  return rc;    
}

static int mqttClient_processPubRec(mqttClient_t* clientData)
{
  int32_t rc = LE_OK;
  uint16_t packetId;
  uint8_t dup;
  uint8_t type;

  LE_DEBUG("---> PUBREC");
  LE_ASSERT(clientData);

  if (MQTTDeserialize_ack(&type, &dup, &packetId, clientData->session.rx.buf, sizeof(clientData->session.rx.buf)) != 1)
  {
    LE_ERROR("MQTTDeserialize_ack() failed");
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }
  else if (clientData->session.nextPacketId != packetId)
  {
    LE_ERROR("invalid packet ID(%u != %u)", clientData->session.nextPacketId, packetId);
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

  int len = MQTTSerialize_ack(clientData->session.tx.buf, sizeof(clientData->session.tx.buf), PUBREL, 0, packetId);
  if (len <= 0)
  {
    LE_ERROR("MQTTSerialize_ack() failed(%d)", len);
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

  LE_DEBUG("<--- PUBREL");
  rc = mqttClient_write(clientData, len);
  if (rc) 
  {
    LE_ERROR("mqttClient_write() failed(%d)", rc);
    goto cleanup;
  }

cleanup:
  return rc;
}

static int mqttClient_processPubAck(mqttClient_t* clientData)
{
  int32_t rc = LE_OK;
  uint16_t packetId;
  uint8_t dup;
  uint8_t type;

  LE_DEBUG("---> UNSUBACK");
  LE_ASSERT(clientData);

  rc = le_timer_Stop(clientData->session.cmdTimer);
  if (rc)
  {
    LE_ERROR("le_timer_Stop() failed(%d)", rc);
    goto cleanup;
  }

  rc = MQTTDeserialize_ack(&type, &dup, &packetId, clientData->session.rx.buf, sizeof(clientData->session.rx.buf));
  if (rc != 1)
  {
    LE_ERROR("MQTTDeserialize_ack() failed");
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }
  else if (clientData->session.nextPacketId != packetId)
  {
    LE_ERROR("invalid packet ID(%u != %u)", clientData->session.nextPacketId, packetId);
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

cleanup:
  return rc;
}

static void mqttClient_processPingResp(mqttClient_t* clientData)
{
  LE_DEBUG("---> PINGRESP");
  LE_ASSERT(clientData);
  le_timer_Restart(clientData->session.pingTimer);
}

static void mqttClient_socketFdEventHandler(int sockFd, short events)
{
  mqttClient_t* clientData = le_fdMonitor_GetContextPtr();
  int rc = LE_OK;

  LE_ASSERT(clientData);

  LE_DEBUG("events(0x%08x)", events);
  if (events & POLLOUT)
  {
    le_fdMonitor_Disable(clientData->session.sockFdMonitor, POLLOUT);

    if ((clientData->session.sock != MQTT_CLIENT_INVALID_SOCKET) && !clientData->session.isConnected)
    {
      LE_INFO("connected(%s:%d)", clientData->session.config.brokerUrl, clientData->session.config.portNumber);
      rc = le_timer_Stop(clientData->session.connTimer);
      if (rc)
      {
        LE_ERROR("le_timer_Stop() failed(%d)", rc);
        goto cleanup;
      }

      MQTTPacket_connectData data = MQTTPacket_connectData_initializer;       
      data.willFlag = 0;
      data.MQTTVersion = MQTT_CLIENT_MQTT_VERSION;

      LE_INFO("deviceId('%s'), pwd('%s')", clientData->deviceId, clientData->session.secret);
      if (strlen(clientData->deviceId) > 0)
      {
        data.clientID.cstring = clientData->deviceId;
        data.username.cstring = clientData->deviceId;
        data.password.cstring = clientData->session.secret;
      }

      data.keepAliveInterval = clientData->session.config.keepAlive;
      data.cleansession = 1;

      rc = mqttClient_sendConnect(clientData, &data);
      if (rc)
      {
        LE_ERROR("mqttClient_sendConnect() failed(%d)", rc);
        goto cleanup;
      }
    }
    else if (clientData->session.tx.bytesLeft)
    {
      rc = mqttClient_write(clientData, 0);
      if (rc)
      {
        LE_ERROR("mqttClient_write() failed(%d)", rc);
        goto cleanup;
      }
    }
  }
  else if (events & POLLIN)
  {
    uint16_t packetType = MQTTPacket_read(clientData->session.rx.buf, sizeof(clientData->session.rx.buf), mqttClient_read);    
    LE_DEBUG("packet type(%d)", packetType);
    switch (packetType)
    {
    case CONNACK:
      rc = mqttClient_processConnAck(clientData);
      if (rc)
      {
        LE_ERROR("mqttClient_processConnAck() failed(%d)", rc);
        goto cleanup;
      }

      break;

    case PUBACK:
      rc = mqttClient_processPubAck(clientData);
      if (rc)
      {
        LE_ERROR("mqttClient_processPubAck() failed(%d)", rc);
        goto cleanup;
      }

      break;

    case SUBACK:
      rc = mqttClient_processSubAck(clientData);
      if (rc)
      {
        LE_ERROR("mqttClient_processSubAck() failed(%d)", rc);
        goto cleanup;
      }

      break;

    case UNSUBACK:
      rc = mqttClient_processUnSubAck(clientData);
      if (rc)
      {
        LE_ERROR("mqttClient_processUnSubAck() failed(%d)", rc);
        goto cleanup;
      }

      break;

    case PUBLISH:
      rc = mqttClient_processPublish(clientData);
      if (rc)
      {
        LE_ERROR("mqttClient_processPublish() failed(%d)", rc);
        goto cleanup;
      }

      break;
    
    case PUBREC:
      rc = mqttClient_processPubRec(clientData);
      if (rc)
      {
        LE_ERROR("mqttClient_processPubRec() failed(%d)", rc);
        goto cleanup;
      }
  
      break;
    
    case PUBCOMP:
      rc = mqttClient_processPubComp(clientData);
      if (rc)
      {
        LE_ERROR("mqttClient_processPubComp() failed(%d)", rc);
        goto cleanup;
      }

      break;

    case PINGRESP:
      mqttClient_processPingResp(clientData);
      break;

    default:
      LE_ERROR("unknown packet type(%u)", packetType);
      break;
    }

    clientData->session.cmdRetries = 0;
  }

cleanup:
  return;
}

static void mqttClient_dataConnectionStateHandler(const char* intfName, bool isConnected, void* contextPtr)
{
  mqttClient_t* clientData = (mqttClient_t*)contextPtr;
  int32_t rc = LE_OK;

  LE_ASSERT(clientData);

  LE_DEBUG("interface('%s') connected(%u)", intfName, isConnected);
  if (isConnected)
  {
    if ((clientData->session.sock == MQTT_CLIENT_INVALID_SOCKET) && !clientData->session.isConnected)
    {
      LE_INFO("starting session");           
      rc = mqttClient_startSession(clientData);
      if (rc)
      {        
        LE_ERROR("mqttClient_startSession() failed(%d)", rc);
        goto cleanup;
      }
    }
    else
    {
        LE_INFO("No ongoing MQTT Connection request");
    }
  }
  else
  {
    LE_INFO("disconnected('%s')", intfName);
    mqttClient_disconnectData(clientData);
  }

cleanup:
  if (rc)
  {
    int32_t err = mqttClient_disconnectData(clientData);
    if (err)
    {        
      LE_ERROR("mqttClient_disconnectData() failed(%d)", err);
    }
  }

  return;
}

static int mqttClient_connect(mqttClient_t* clientData)
{
  struct sockaddr_in address;
  struct addrinfo *result = NULL;
  struct addrinfo hints = {0, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, 0, NULL, NULL, NULL};  
  int rc = LE_OK;

  LE_ASSERT(clientData);

  if (clientData->session.sock != MQTT_CLIENT_INVALID_SOCKET)
  {
    LE_WARN("socket already connected");
    goto cleanup;
  }

  rc = getaddrinfo(clientData->session.config.brokerUrl, NULL, &hints, &result);
  if (rc)
  {
    LE_ERROR("getaddrinfo() failed(%d)", rc);
    goto cleanup;
  }
	
  struct addrinfo* res = result;
  while (res)
  {
    if (res->ai_family == AF_INET)
    {
      result = res;
      break;
    }
			
    res = res->ai_next;
  }

  if (result->ai_family == AF_INET)
  {
    address.sin_port = htons(clientData->session.config.portNumber);
    address.sin_family = AF_INET;
    address.sin_addr = ((struct sockaddr_in*)(result->ai_addr))->sin_addr;
  }
  else
  {
    LE_ERROR("find IP('%s') failed", clientData->session.config.brokerUrl);
    rc = LE_FAULT;
    goto cleanup;
  }

  clientData->session.sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (clientData->session.sock == -1)
  {
    LE_ERROR("socket() failed(%d)", errno);
    rc = clientData->session.sock;
    goto cleanup;
  }

  clientData->session.sockFdMonitor = le_fdMonitor_Create(MQTT_CLIENT_SOCKET_MONITOR_NAME, clientData->session.sock, mqttClient_socketFdEventHandler, POLLIN | POLLOUT);
  if (!clientData->session.sockFdMonitor)
  {
    LE_ERROR("le_fdMonitor_Create() failed");
    rc = LE_FAULT;
    goto cleanup;
  }

  le_fdMonitor_SetContextPtr(clientData->session.sockFdMonitor, clientData);

  clientData->session.tx.ptr = clientData->session.tx.buf;
  clientData->session.rx.ptr = clientData->session.rx.buf;

  rc = connect(clientData->session.sock, (struct sockaddr*)&address, sizeof(address));
  if (rc == -1)
  {
    rc = le_timer_Start(clientData->session.connTimer);
    if (rc)
    {
      LE_ERROR("le_timer_Start() failed(%d)", rc);
      goto cleanup;
    }

    if (errno != EINPROGRESS)
    {
      LE_ERROR("connect() failed(%d)", errno);
      rc = LE_FAULT;
      goto cleanup;
    }

    LE_DEBUG("connecting('%s')", clientData->session.config.brokerUrl);
    rc = LE_OK;
    goto cleanup;
  }

  LE_DEBUG("connected('%s')", clientData->session.config.brokerUrl);

cleanup:
  freeaddrinfo(result);

  if (rc && (clientData->session.sock != MQTT_CLIENT_INVALID_SOCKET))
  {
    int err = mqttClient_close(clientData);
    if (err)
    {
      LE_ERROR("mqttClient_close() failed(%d)", err);
      goto cleanup;
    }
  }

  return rc;
}

static int mqttClient_close(mqttClient_t* clientData)
{
  int rc = LE_OK;

  LE_ASSERT(clientData);

  if (clientData->session.sock != MQTT_CLIENT_INVALID_SOCKET)
  {
    le_fdMonitor_Delete(clientData->session.sockFdMonitor);

    rc = close(clientData->session.sock);
    if (rc == -1)
    {
      LE_ERROR("close() failed(%d)", errno);
      rc = LE_FAULT;
      goto cleanup;
    }

    clientData->session.sock = MQTT_CLIENT_INVALID_SOCKET;
  }
  else
  {
    LE_WARN("socket already closed");
  }

cleanup:
  return rc;
}

static int mqttClient_write(mqttClient_t* clientData, int length)
{
  int bytes = length ? length:clientData->session.tx.bytesLeft;
  le_result_t rc = LE_OK;

  LE_ASSERT(clientData);

  clientData->session.cmdLen = length;

  if (clientData->session.sock == MQTT_CLIENT_INVALID_SOCKET)
  {
    rc = mqttClient_connect(clientData);
    if (rc)
    {
      LE_ERROR("mqttClient_connect() failed(%d)", errno);
      rc = LE_IO_ERROR;
      goto cleanup;
    }
  }
  else
  {
    while (bytes > 0)
    {
      mqttClient_dumpBuffer(clientData->session.tx.ptr, bytes);
      int sent = write(clientData->session.sock, clientData->session.tx.ptr, bytes);
      if (sent == -1)
      {
        if (errno == EAGAIN)
        {
          le_fdMonitor_Enable(clientData->session.sockFdMonitor, POLLOUT);
          clientData->session.tx.bytesLeft = bytes;
          LE_WARN("send blocked(%u)", clientData->session.tx.bytesLeft);
          goto cleanup;
        }
        else
        {
          LE_ERROR("write() failed(%d)", errno);
          rc = LE_IO_ERROR;
          goto cleanup;
        }
      }
      else
      {
        clientData->session.tx.ptr += sent;
        bytes -= sent;
      }
    }

    le_timer_Restart(clientData->session.pingTimer);

    clientData->session.tx.ptr = clientData->session.tx.buf;
    clientData->session.rx.ptr = clientData->session.rx.buf;
  }

cleanup:
  return rc;
}

static char mqttClient_isTopicMatched(char* topicFilter, MQTTString* topicName)
{
  char* curf = topicFilter;
  char* curn = topicName->lenstring.data;
  char* curn_end = curn + topicName->lenstring.len;
    
  // assume topic filter and name is in correct format
  // # can only be at end
  // + and # can only be next to separator
  while (*curf && curn < curn_end)
  {
    if (*curn == '/' && *curf != '/')
      break;

    if (*curf != '+' && *curf != '#' && *curf != *curn)
      break;

    if (*curf == '+')
    {   
      // skip until we meet the next separator, or end of string
      char* nextpos = curn + 1;
      while (nextpos < curn_end && *nextpos != '/') nextpos = ++curn + 1;
    }
    else if (*curf == '#')
      curn = curn_end - 1;    // skip until end of string
        
    curf++;
      curn++;
  };
    
  return (curn == curn_end) && (*curf == '\0');
}

static int mqttClient_deliverMsg(mqttClient_t* clientData, MQTTString* topicName, mqttClient_msg_t* message)
{
  int i;
  int rc = LE_OK;

  LE_ASSERT(clientData);
  LE_ASSERT(topicName);
  LE_ASSERT(message);

  for (i = 0; i < MQTT_CLIENT_MAX_MESSAGE_HANDLERS; ++i)
  {
    if ((clientData->msgHndlrs[i].topicFilter != 0) && 
        (MQTTPacket_equals(topicName, (char*)clientData->msgHndlrs[i].topicFilter) || 
         mqttClient_isTopicMatched((char*)clientData->msgHndlrs[i].topicFilter, topicName)))
    {
      if (clientData->msgHndlrs[i].fp != NULL)
      {
        mqttClient_msg_data_t msgData;
        mqttClient_newMsgData(&msgData, topicName, message);
        clientData->msgHndlrs[i].fp(&msgData);
        goto cleanup;
      }
    }
  }
    
  mqttClient_msg_data_t msgData;
  mqttClient_newMsgData(&msgData, topicName, message);
  clientData->defaultMsgHndlr(&msgData);
   
cleanup:    
  return rc;
}

static int mqttClient_startSession(mqttClient_t* clientData)
{
  int rc = LE_OK;

  LE_ASSERT(clientData);

  memcpy(&clientData->session.config, &clientData->config, sizeof(clientData->config));

  clientData->session.connTimer = le_timer_Create(MQTT_CLIENT_CONNECT_TIMER);
  if (!clientData->session.connTimer)
  {
    LE_ERROR("le_timer_Create() failed");
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

  rc = le_timer_SetHandler(clientData->session.connTimer, mqttClient_connExpiryHndlr);
  if (rc)
  {
    LE_ERROR("le_timer_SetHandler() failed(%d)", rc);
    goto cleanup;
  }

  rc = le_timer_SetMsInterval(clientData->session.connTimer, MQTT_CLIENT_CONNECT_TIMEOUT_MS);
  if (rc)
  {
    LE_ERROR("le_timer_SetMsInterval() failed(%d)", rc);
    goto cleanup;
  }  

  rc = le_timer_SetContextPtr(clientData->session.connTimer, clientData);
  if (rc)
  {
    LE_ERROR("le_timer_SetContextPtr() failed(%d)", rc);
    goto cleanup;
  } 

  clientData->session.cmdTimer = le_timer_Create(MQTT_CLIENT_COMMAND_TIMER);
  if (!clientData->session.cmdTimer)
  {
    LE_ERROR("le_timer_Create() failed");
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

  rc = le_timer_SetHandler(clientData->session.cmdTimer, mqttClient_cmdExpiryHndlr);
  if (rc)
  {
    LE_ERROR("le_timer_SetHandler() failed(%d)", rc);
    goto cleanup;
  }

  rc = le_timer_SetMsInterval(clientData->session.cmdTimer, MQTT_CLIENT_CMD_TIMEOUT_MS);
  if (rc)
  {
    LE_ERROR("le_timer_SetMsInterval() failed(%d)", rc);
    goto cleanup;
  }  

  rc = le_timer_SetContextPtr(clientData->session.cmdTimer, clientData);
  if (rc)
  {
    LE_ERROR("le_timer_SetContextPtr() failed(%d)", rc);
    goto cleanup;
  } 

  clientData->session.pingTimer = le_timer_Create(MQTT_CLIENT_PING_TIMER);
  if (!clientData->session.pingTimer)
  {
    LE_ERROR("le_timer_Create() failed");
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

  rc = le_timer_SetHandler(clientData->session.pingTimer, mqttClient_pingExpiryHndlr);
  if (rc)
  {
    LE_ERROR("le_timer_SetHandler() failed(%d)", rc);
    goto cleanup;
  }

  rc = le_timer_SetMsInterval(clientData->session.pingTimer, clientData->session.config.keepAlive * 1000);
  if (rc)
  {
    LE_ERROR("le_timer_SetMsInterval() failed(%d)", rc);
    goto cleanup;
  }  

  rc = le_timer_SetRepeat(clientData->session.pingTimer, 0);
  if (rc)
  {
    LE_ERROR("le_timer_SetRepeat() failed(%d)", rc);
    goto cleanup;
  }  

  rc = le_timer_SetContextPtr(clientData->session.pingTimer, clientData);
  if (rc)
  {
    LE_ERROR("le_timer_SetContextPtr() failed(%d)", rc);
    goto cleanup;
  } 

  LE_INFO("connect(%s:%d)", clientData->session.config.brokerUrl, clientData->session.config.portNumber);
  rc = mqttClient_connect(clientData); 
  if (rc)
  {
    LE_ERROR("mqttClient_connect() failed(%d)", rc);
    goto cleanup;
  }

cleanup:
  if (rc)
  {
    LE_INFO("start session failed");
    mqttClient_SendConnStateEvent(false, rc, -1);
  }

  return rc;
}

static int mqttClient_connectData(mqttClient_t* clientData)
{
  int rc = LE_OK;

  if (clientData->requestRef)
  {
    LE_ERROR("data connection request already exists");
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

  clientData->requestRef = le_data_Request();
  LE_INFO("requesting data connection(%p)", clientData->requestRef);

cleanup:
  return rc;
}

int mqttClient_read(uint8_t* buf, int len)
{
  mqttClient_t* clientData = mqttMain_getClient();
  uint8_t* ptr = (clientData->session.rx.ptr != buf) ? buf:clientData->session.rx.ptr;
  int bytes = 0;
  int ret = LE_OK;

  LE_ASSERT(buf);

  while (bytes < len)
  {
    LE_DEBUG("read(%u)", len - bytes);
    ret = recv(clientData->session.sock, ptr, len - bytes, 0);
    if (ret == -1)
    {
      if (errno != EAGAIN)
      {
        LE_ERROR("recv() failed(%d)", errno);
        bytes = ret;
        goto cleanup;
      }
      else
      {
        if (clientData->session.rx.ptr != buf)
        {
         clientData->session.rx.ptr = ptr;
         clientData->session.rx.bytesLeft = len - bytes;
         LE_WARN("read blocked(%u)", clientData->session.rx.bytesLeft);
        }
        else
        {
          LE_ERROR("read blocked(%u)", len - bytes);
          bytes = ret;
        }

        goto cleanup;
      }
    }
    else if (ret == 0)
    {
      LE_WARN("peer closed connection");
      bytes = ret;
      ret = mqttClient_close(clientData);
      if (ret)
      {
        LE_ERROR("mqttClient_close() failed(%d)", ret);
	goto cleanup;
      }

      goto cleanup;
    }
    else
    {
      mqttClient_dumpBuffer(ptr, ret);
      ptr += ret;
      bytes += ret;
    }
  }

cleanup:
  return bytes;
}

int mqttClient_disconnectData(mqttClient_t* clientData)
{
  int rc = LE_OK;

  if (!clientData->requestRef)
  {
    LE_ERROR("no data connection reference.");
    goto cleanup;
  }
    
  if (clientData->session.isConnected)
  {
    LE_INFO("Dispose MQTT resources.");
    mqttClient_disconnect(clientData);

    rc = mqttClient_close(clientData);
    if (rc)
    {
      LE_ERROR("mqttClient_close() failed(%d)", rc);
      goto cleanup;
    }
  }

  LE_INFO("releasing the data connection.");
  le_data_Release(clientData->requestRef);
  clientData->requestRef = NULL;
  mqttClient_SendConnStateEvent(false, 0, 0);

cleanup:
  return rc;
}

int mqttClient_subscribe(mqttClient_t* clientData, const char* topicFilter, mqttClient_QoS_e qos, mqttClient_msgHndlr_f messageHandler)
{ 
  int rc = LE_OK;  
    
  LE_ASSERT(clientData);
  LE_ASSERT(topicFilter);

  if (!clientData->session.isConnected)
  {
    LE_WARN("not connected");
    goto cleanup;
  }

  MQTTString topic = MQTTString_initializer;
  topic.cstring = (char *)topicFilter;
  int len = MQTTSerialize_subscribe(clientData->session.tx.buf, sizeof(clientData->session.tx.buf), 0, mqttClient_getNextPacketId(clientData), 1, &topic, (int*)&qos);
  if (len <= 0)
  {
    LE_ERROR("MQTTSerialize_subscribe() failed(%d)", len);
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

  int i;
  for (i = 0; i < MQTT_CLIENT_MAX_MESSAGE_HANDLERS; ++i)
  {
    if (!clientData->msgHndlrs[i].topicFilter)
    {
      LE_DEBUG("call msg handler('%s')", topicFilter);
      clientData->msgHndlrs[i].topicFilter = topicFilter;
      clientData->msgHndlrs[i].fp = messageHandler;
      break;
    }
  }

  if (i == MQTT_CLIENT_MAX_MESSAGE_HANDLERS)
  {
    LE_ERROR("no empty msg handlers");
    rc = LE_BAD_PARAMETER;
  }

  rc = mqttClient_write(clientData, len);
  if (rc)
  {
    LE_ERROR("mqttClient_write() failed(%d)", rc); 
    goto cleanup;             
  }

  rc = le_timer_Start(clientData->session.cmdTimer);
  if (rc)
  {
    LE_ERROR("le_timer_Start() failed(%d)", rc);
    goto cleanup;
  }
        
cleanup:
  return rc;
}

int mqttClient_unsubscribe(mqttClient_t* clientData, const char* topicFilter)
{   
  MQTTString topic = MQTTString_initializer;
  int rc = LE_OK;
       
  LE_ASSERT(clientData);

  if (!clientData->session.isConnected)
  {
    LE_WARN("not connected");
    goto cleanup;
  }
    
  topic.cstring = (char *)topicFilter;
  int len = MQTTSerialize_unsubscribe(clientData->session.tx.buf, sizeof(clientData->session.tx.buf), 0, mqttClient_getNextPacketId(clientData), 1, &topic);
  if (len <= 0)
  {
    LE_ERROR("MQTTSerialize_unsubscribe() failed(%d)", len);
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

  rc = mqttClient_write(clientData, len);
  if (rc)
  { 
    LE_ERROR("mqttClient_write() failed(%d)", rc);
    goto cleanup; 
  }
 
  rc = le_timer_Start(clientData->session.cmdTimer);
  if (rc)
  {
    LE_ERROR("le_timer_Start() failed(%d)", rc);
    goto cleanup;
  }

cleanup:
  return rc;
}

int mqttClient_publish(mqttClient_t* clientData, const char* topicName, mqttClient_msg_t* message)
{
  int rc = LE_OK;
  MQTTString topic = MQTTString_initializer;
  topic.cstring = (char *)topicName;
  int len = 0;

  LE_ASSERT(clientData);

  if (!clientData->session.isConnected)
  {
    LE_WARN("not connected");
    goto cleanup;
  }

  if (message->qos == MQTT_CLIENT_QOS1 || message->qos == MQTT_CLIENT_QOS2)
    message->id = mqttClient_getNextPacketId(clientData);

  len = MQTTSerialize_publish(clientData->session.tx.buf, sizeof(clientData->session.tx.buf), 0, message->qos, 
      message->retained, message->id, topic, (unsigned char*)message->payload, message->payloadLen);
  if (len <= 0)
  {
    LE_ERROR("MQTTSerialize_publish() failed(%d)", len);
    rc = LE_BAD_PARAMETER;
    goto cleanup;
  }

  rc = mqttClient_write(clientData, len);
  if (rc)
  {
    LE_ERROR("mqttClient_write() failed(%d)", rc);
    goto cleanup;
  } 
  
cleanup:
  return rc;
}

int mqttClient_disconnect(mqttClient_t* clientData)
{  
  int rc = LE_OK;

  LE_ASSERT(clientData);

  if (le_timer_IsRunning(clientData->session.connTimer))
  {
    rc = le_timer_Stop(clientData->session.connTimer);
    if (rc)
    {
      LE_ERROR("le_timer_Stop() failed(%d)", rc);
      goto cleanup;
    }
  }

  if (le_timer_IsRunning(clientData->session.cmdTimer))
  {
    rc = le_timer_Stop(clientData->session.cmdTimer);
    if (rc)
    {
      LE_ERROR("le_timer_Stop() failed(%d)", rc);
      goto cleanup;
    }
  }

  rc = le_timer_Stop(clientData->session.pingTimer);
  if (rc)
  {
    LE_ERROR("le_timer_Stop() failed(%d)", rc);
    goto cleanup;
  }

  int len = MQTTSerialize_disconnect(clientData->session.tx.buf, sizeof(clientData->session.tx.buf));
  if (len > 0)
  {
    rc = mqttClient_write(clientData, len); 
    if (rc)
    {
      LE_ERROR("mqttClient_write() failed(%d)", rc);
      goto cleanup;
    }           
  }
      
  le_timer_Delete(clientData->session.pingTimer);
  le_timer_Delete(clientData->session.cmdTimer);
  le_timer_Delete(clientData->session.connTimer);

  clientData->session.isConnected = 0;

cleanup:
  return rc;
}

int mqttClient_connectUser(mqttClient_t* clientData, const char* password)
{
  int32_t rc = LE_OK;

  LE_ASSERT(clientData);
  LE_ASSERT(password);

  if (!clientData->session.isConnected)
  {
    LE_DEBUG("pw('%s')", password);
    strcpy(clientData->session.secret, password);

    if (!clientData->dataConnectionState)
    {
      clientData->dataConnectionState = le_data_AddConnectionStateHandler(mqttClient_dataConnectionStateHandler, clientData);
    }

    LE_DEBUG("initiated data connection");
    rc = mqttClient_connectData(clientData);
    if (rc)
    {
      LE_ERROR("mqttClient_connectData() failed(%d)", rc);
      goto cleanup;
    } 
  }
  else
  {    
    LE_INFO("already Connected");
    mqttClient_SendConnStateEvent(false, 1, -1);
  }

cleanup:
  return rc;
}

void mqttClient_init(mqttClient_t* clientData)
{
  LE_ASSERT(clientData);

  memset(clientData, 0, sizeof(mqttClient_t));
  clientData->session.tx.ptr = clientData->session.tx.buf;
  clientData->session.rx.ptr = clientData->session.rx.buf;

  clientData->session.sock = MQTT_CLIENT_INVALID_SOCKET;
  strcpy(clientData->config.brokerUrl, MQTT_CLIENT_URL_AIRVANTAGE_SERVER);
  clientData->config.portNumber = MQTT_CLIENT_PORT_AIRVANTAGE_SERVER;

  clientData->config.keepAlive = MQTT_CLIENT_PING_TIMEOUT_MS;
  clientData->config.QoS = MQTT_CLIENT_DEFAULT_QOS;

  clientData->connStateEvent = le_event_CreateId("MqttConnState", sizeof(mqttClient_connStateData_t));
  clientData->inMsgEvent = le_event_CreateId("MqttInMsg", sizeof(mqttClient_inMsg_t));

  le_info_ConnectService();
  le_info_GetImei(clientData->deviceId, sizeof(clientData->deviceId));
  LE_DEBUG("IMEI('%s')", clientData->deviceId);
  sprintf(clientData->subscribeTopic, "%s%s", clientData->deviceId, MQTT_CLIENT_TOPIC_NAME_SUBSCRIBE);
}

