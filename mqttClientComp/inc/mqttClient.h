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

#ifndef __MQTT_CLIENT_C_
#define __MQTT_CLIENT_C_

#include "mqtt/mqttPacket.h"

#define MQTT_CLIENT_INVALID_SOCKET                    -1
#define MQTT_CLIENT_SOCKET_MONITOR_NAME               "MQTTSockMonitor"
#define MQTT_CLIENT_CONNECT_TIMER                     "MQTTConnTimer"
#define MQTT_CLIENT_COMMAND_TIMER                     "MQTTCmdTimer"
#define MQTT_CLIENT_PING_TIMER                        "MQTTPingTimer"
#define MQTT_CLIENT_PING_TIMEOUT_MS                   30

#define MQTT_CLIENT_CONNECT_SUCCESS                   0
#define MQTT_CLIENT_MAX_SEND_RETRIES                  10
#define MQTT_CLIENT_MAX_PACKET_ID                     65535
#define MQTT_CLIENT_MAX_MESSAGE_HANDLERS              5

#define MQTT_CLIENT_TOPIC_NAME_LEN                    128
#define MQTT_CLIENT_KEY_NAME_LEN                      128
#define MQTT_CLIENT_VALUE_LEN                         128
#define MQTT_CLIENT_TIMESTAMP_LEN                     16

#define MQTT_CLIENT_DEFAULT_SIZE                      32
#define MQTT_CLIENT_MAX_URL_LENGTH                    256
#define MQTT_CLIENT_MAX_PAYLOAD_SIZE                  2048
#define MQTT_CLIENT_MQTT_VERSION                      3
#define MQTT_CLIENT_CONNECT_TIMEOUT_MS                10000
#define MQTT_CLIENT_CMD_TIMEOUT_MS                    5000
#define MQTT_CLIENT_TOPIC_NAME_PUBLISH                "/messages/json"
#define MQTT_CLIENT_TOPIC_NAME_SUBSCRIBE              "/tasks/json"
#define MQTT_CLIENT_TOPIC_NAME_ACK                    "/acks/json"
#define MQTT_CLIENT_URL_AIRVANTAGE_SERVER             "eu.airvantage.net"
#define MQTT_CLIENT_PORT_AIRVANTAGE_SERVER            1883
#define MQTT_CLIENT_DEFAULT_QOS                       0
#define MQTT_CLIENT_AV_JSON_KEY_MAX_COUNT             10
#define MQTT_CLIENT_AV_JSON_KEY_MAX_LENGTH            32

typedef enum _mqttClient_QoS_e 
{ 
  MQTT_CLIENT_QOS0 = 0, 
  MQTT_CLIENT_QOS1, 
  MQTT_CLIENT_QOS2, 
} mqttClient_QoS_e;

typedef struct _mqttClient_connStateData_t
{
    bool                               isConnected;
    int                                connectErrorCode;
    int                                subErrorCode;
} mqttClient_connStateData_t;

typedef struct _mqttClient_inMsg_t
{
    char                               topicName[MQTT_CLIENT_TOPIC_NAME_LEN + 1];
    char                               keyName[MQTT_CLIENT_KEY_NAME_LEN + 1];
    char                               value[MQTT_CLIENT_VALUE_LEN + 1];
    char                               timestamp[MQTT_CLIENT_TIMESTAMP_LEN + 1];
} mqttClient_inMsg_t;

typedef struct _mqttClient_msg_t
{
  char*                                payload;
  mqttClient_QoS_e                     qos;
  size_t                               payloadLen;
  unsigned short                       id;
  char                                 retained;
  char                                 dup;
} mqttClient_msg_t;

typedef struct _mqttClient_msg_data_t
{
  mqttClient_msg_t*                    message;
  MQTTString*                          topicName;
} mqttClient_msg_data_t;

typedef struct _mqttClient_msg_hndlrs_t
{
  const char*                          topicFilter;
  void                                 (*fp)(mqttClient_msg_data_t*);
} mqttClient_msg_hndlrs_t;

typedef struct _mqttClient_bufferInfo_t 
{
  unsigned char                        buf[MQTT_CLIENT_MAX_PAYLOAD_SIZE];
  uint8_t*                             ptr;
  uint32_t                             bytesLeft;
} mqttClient_bufferInfo_t;

typedef struct _mqttClient_config_t 
{
  char                                 brokerUrl[MQTT_CLIENT_MAX_URL_LENGTH];
  uint32_t                             portNumber;
  uint32_t                             keepAlive;
  int32_t                              QoS;
} mqttClient_config_t;

typedef struct _mqttClient_session_t 
{
  le_fdMonitor_Ref_t                   sockFdMonitor;
  le_timer_Ref_t                       connTimer;
  le_timer_Ref_t                       cmdTimer;
  le_timer_Ref_t                       pingTimer; 
  mqttClient_config_t                  config;
  mqttClient_bufferInfo_t              tx;
  mqttClient_bufferInfo_t              rx;
  char                                 secret[MQTT_CLIENT_DEFAULT_SIZE];
  uint32_t                             cmdLen;
  uint32_t                             cmdRetries;
  uint32_t                             nextPacketId;
  int32_t                              sock;
  uint8_t                              isConnected;
} mqttClient_session_t;

typedef struct _mqttClient_t 
{
  mqttClient_msg_hndlrs_t              msgHndlrs[MQTT_CLIENT_MAX_MESSAGE_HANDLERS];
  le_data_ConnectionStateHandlerRef_t  dataConnectionState;
  le_data_RequestObjRef_t              requestRef;
  le_event_Id_t                        connStateEvent;
  le_event_Id_t                        inMsgEvent;   
  mqttClient_session_t                 session;
  mqttClient_config_t                  config;
  char                                 key[MQTT_CLIENT_DEFAULT_SIZE];
  char                                 value[MQTT_CLIENT_DEFAULT_SIZE];
  char                                 deviceId[MQTT_CLIENT_DEFAULT_SIZE];
  char                                 subscribeTopic[2*MQTT_CLIENT_DEFAULT_SIZE];
  void                                 (*defaultMsgHndlr)(mqttClient_msg_data_t*);
} mqttClient_t;

typedef void (*mqttClient_msgHndlr_f)(mqttClient_msg_data_t*);

int mqttClient_publish(mqttClient_t*, const char*, mqttClient_msg_t*);
int mqttClient_subscribe(mqttClient_t*, const char*, mqttClient_QoS_e, mqttClient_msgHndlr_f);
int mqttClient_unsubscribe(mqttClient_t*, const char*);
int mqttClient_disconnect(mqttClient_t*);

int mqttClient_read(uint8_t*, int);
int mqttClient_disconnectData(mqttClient_t*);
int mqttClient_connectUser(mqttClient_t*, const char*);

mqttClient_t* mqttMain_getClient(void);
void mqttClient_init(mqttClient_t*);

#endif
