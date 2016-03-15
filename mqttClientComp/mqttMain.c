/**
 * This module implements MQTT Client.
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 *
 */

/* Legato Framework */
#include "legato.h"
#include "interfaces.h"

/* data Connection Services (Client) */
#include "le_data_interface.h"

#include "le_info_interface.h"

#include "MQTTClient.h"
#include "json/swir_json.h"

#define     DEFAULT_SIZE                32
#define     MAX_URL_LENGTH              256
#define     MAX_PAYLOAD_SIZE            2048    //Default payload buffer size
#define     MQTT_VERSION                3
#define     TIMEOUT_MS                  1000    //1 second time-out, MQTT client init
#define     TOPIC_NAME_PUBLISH          "/messages/json"
#define     TOPIC_NAME_SUBSCRIBE        "/tasks/json"
#define     TOPIC_NAME_ACK              "/acks/json"
#define     URL_AIRVANTAGE_SERVER       "eu.airvantage.net"
#define     PORT_AIRVANTAGE_SERVER      1883
#define     DEFAULT_KEEP_ALIVE          30      //seconds
#define     DEFAULT_QOS                 0       //[0, 2]
#define     AV_JSON_KEY_MAX_COUNT       10
#define     AV_JSON_KEY_MAX_LENGTH      32

typedef struct
{
    Client          oMqttClient;
    Network         oNetwork;
    char            szBrokerUrl[MAX_URL_LENGTH];
    int32_t         u32PortNumber;
    int32_t         u32KeepAlive;
    int32_t         u32QoS;
    char            szKey[DEFAULT_SIZE];
    char            szValue[DEFAULT_SIZE];
    char            szDeviceId[DEFAULT_SIZE];
    char            szSecret[DEFAULT_SIZE];
    char            szSubscribeTopic[2*DEFAULT_SIZE];
} ST_PROGRAM_CONTEXT;

typedef enum
{
    IDLE,
    CONNECTING,
    DISCONNECTING,
    DATA_CONNECTED,
    DATA_DISCONNECTED,
    MQTT_CONNECTED,
    MQTT_DISCONNECTED
} E_MQTTCLIENT_STATE;

static ST_PROGRAM_CONTEXT   g_stContext;
static E_MQTTCLIENT_STATE   g_eState = IDLE;

static le_data_ConnectionStateHandlerRef_t  g_hDataConnectionState = NULL;

static unsigned char        g_buf[MAX_PAYLOAD_SIZE];
static unsigned char        g_readbuf[MAX_PAYLOAD_SIZE];
//--------------------------------------------------------------------------------------------------
/**
 *  The Data Connection reference
 */
//--------------------------------------------------------------------------------------------------
static le_data_RequestObjRef_t  g_RequestRef = NULL;

//--------------------------------------------------------------------------------------------------
/**
 * Event for sending connection to applications
 */
//--------------------------------------------------------------------------------------------------
static le_event_Id_t MqttConnStateEvent;
static le_event_Id_t MqttInMsgEvent;
//--------------------------------------------------------------------------------------------------
/**
 * Data associated with the above MqttConnStateEvent.
 *
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    bool    bIsConnected;
    int     nConnectErrorCode;
    int     nSubErrorCode;
}
MqttConnStateData_t;

//--------------------------------------------------------------------------------------------------
/**
 * Data associated with the above ConnStateEvent.
 *
 * interfaceName is only valid if isConnected is true.
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    char szTopicName[128+1];
    char szKeyName[128+1];
    char szValue[128+1];
    char szTimestamp[16+1];
}
MqttInMsgData_t;

////////////////////////////////////////////////////////////////////////////////////////////////////
static void SendMqttConnStateEvent
(
    bool        bIsConnected,
    int32_t     nConnectErrorCode,
    int32_t     nSubErrorCode
);
////////////////////////////////////////////////////////////////////////////////////////////////////
static void PrintMessage(char* szTrace, ...)
{
    bool    bIsSandboxed = (getuid() != 0);

    char    szMessage[256];
    va_list args;

    va_start(args, szTrace);
    vsnprintf(szMessage, sizeof(szMessage), szTrace, args);
    va_end(args);

    if (bIsSandboxed)
    {
        LE_INFO("    %s", szMessage);
    }
    else
    {
        fprintf(stderr, "    %s\n", szMessage);
    }
}
//--------------------------------------------------------------------------------------------------
/**
 *  This function will request the data connection
 */
//--------------------------------------------------------------------------------------------------
static void ConnectData
(
    void
)
{
    if (g_RequestRef)
    {
        LE_ERROR("A data connection request already exist.");
        return;
    }

    g_eState = CONNECTING;

    g_RequestRef = le_data_Request();
    LE_INFO("Requesting the data connection: %p.", g_RequestRef);
}

//--------------------------------------------------------------------------------------------------
/**
 *  The opposite of ConnectData, this function will tear down the data connection.
 */
//--------------------------------------------------------------------------------------------------
static void DisconnectData
(
    void
)
{
    if (!g_RequestRef)
    {
        LE_ERROR("Not existing data connection reference.");
        return;
    }

    if (g_stContext.oMqttClient.isconnected)
    {
        LE_INFO("Dispose MQTT resources.");
        MQTTDisconnect(&g_stContext.oMqttClient);
        g_eState = MQTT_DISCONNECTED;
    }

    if (g_eState >= DATA_CONNECTED)
    {
        g_stContext.oNetwork.disconnect(&g_stContext.oNetwork);
    }

    LE_INFO("Releasing the data connection.");
    le_data_Release(g_RequestRef);

    g_RequestRef = NULL;

    g_eState = IDLE;

    SendMqttConnStateEvent(false, 0, 0);
}

//--------------------------------------------------------------------------------------------------
/**
 *  Send MQTT message.
 */
//--------------------------------------------------------------------------------------------------
static int SendMessage
(
    const char* szKey,
    const char* szValue
)
{
    //if (!g_stContext.oMqttClient.isconnected)
    if (g_eState != MQTT_CONNECTED)
    {
        LE_INFO("There is no active MQTT session, please open a session using mqttClient connect");
        return 10;
    }

    char* szPayload = swirjson_szSerialize(szKey, szValue, 0);

    MQTTMessage     msg;

    msg.qos = g_stContext.u32QoS; //QOS0;
    msg.retained = 0;
    msg.dup = 0;
    msg.id = 0;
    msg.payload = szPayload;
    msg.payloadlen = strlen(szPayload);

    char* pTopic = malloc(strlen(TOPIC_NAME_PUBLISH) + strlen(g_stContext.szDeviceId) + 1);
    sprintf(pTopic, "%s%s", g_stContext.szDeviceId, TOPIC_NAME_PUBLISH);
    LE_INFO("Publish on %s\n", pTopic);
    LE_INFO("MQTT message %s\n", szPayload);

    int rc = MQTTPublish(&g_stContext.oMqttClient, pTopic, &msg);
    if (rc == 0)
    {
        LE_INFO("publish OK: %d\n", rc);
    }
    else
    {
        LE_INFO("publish error: %d\n", rc);
    }

    if (pTopic)
    {
        free(pTopic);
    }

    if (szPayload)
    {
        free(szPayload);
    }

    return rc;
}

static void StartTimer
(
    le_timer_Ref_t  timerRef
)
{
    le_result_t     res;

    //start timer
    res = le_timer_Start(timerRef);
    LE_FATAL_IF(res != LE_OK, "Unable to start timer: %d", res);
}

static void timerHandler
(
    le_timer_Ref_t  timerRef
)
{
    if (g_stContext.oMqttClient.isconnected)
    {
        LE_INFO("MQTT yield");
        MQTTYield(&g_stContext.oMqttClient, 1000);
        StartTimer(timerRef);
    }
    else
    {
        LE_INFO("No active MQTT session, now stop Timer");
        le_timer_Stop(timerRef);
    }

}

static int publishAckCmd(const char* szUid, int nAck, char* szMessage)
{
    char* szPayload = (char*) malloc(strlen(szUid)+strlen(szMessage)+48);

    if (nAck == 0)
    {
        sprintf(szPayload, "[{\"uid\": \"%s\", \"status\" : \"OK\"", szUid);
    }
    else
    {
        sprintf(szPayload, "[{\"uid\": \"%s\", \"status\" : \"ERROR\"", szUid);
    }

    if (strlen(szMessage) > 0)
    {
        sprintf(szPayload, "%s, \"message\" : \"%s\"}]", szPayload, szMessage);
    }
    else
    {
        sprintf(szPayload, "%s}]", szPayload);
    }

    LE_INFO("[ACK Message] %s\n", szPayload);

    MQTTMessage     msg;
    msg.qos = g_stContext.u32QoS; //QOS0;
    msg.retained = 0;
    msg.dup = 0;
    msg.id = 0;
    msg.payload = szPayload;
    msg.payloadlen = strlen(szPayload);

    char* pTopic = malloc(strlen(TOPIC_NAME_ACK) + strlen(g_stContext.szDeviceId) + 1);
    sprintf(pTopic, "%s%s", g_stContext.szDeviceId, TOPIC_NAME_ACK);
    LE_INFO("Publish on %s\n", pTopic);

    int rc = MQTTPublish(&g_stContext.oMqttClient, pTopic, &msg);
    if (rc != 0)
    {
        LE_INFO("publish error: %d\n", rc);
    }

    if (pTopic)
    {
        free(pTopic);
    }
    if (szPayload)
    {
        free(szPayload);
    }

    return rc;
}

//--------------------------------------------------------------------------------------------------
/**
 * Send Mqtt incoming message event
 */
//--------------------------------------------------------------------------------------------------
static void SendMqttIncomingMessageEvent
(
    char*       szTopicName,
    char*       szKeyName,
    char*       szValue,
    char*       szTimestamp
)
{
    // Init the event data
    MqttInMsgData_t eventData;
    memset(&eventData, 0, sizeof(eventData));
    strcpy(eventData.szTopicName, szTopicName);
    strcpy(eventData.szKeyName, szKeyName);
    strcpy(eventData.szValue, szValue);
    strcpy(eventData.szTimestamp, szTimestamp);

    LE_DEBUG("Reporting MQTT incoming message: %s, (%s:%s@%s)", eventData.szTopicName, eventData.szKeyName, eventData.szValue, eventData.szTimestamp);

    // Send the event to interested applications
    le_event_Report(MqttInMsgEvent, &eventData, sizeof(eventData));
}

static void onIncomingMessage(MessageData* md)
{
    /*
        This is a callback function (handler), invoked by MQTT client whenever there is an incoming message
        It performs the following actions :
          - deserialize the incoming MQTT JSON-formatted message
          - call convertDataToCSV()
    */

    MQTTMessage* message = md->message;
    char        szTopicName[128] = {0};

    memcpy(szTopicName, md->topicName->lenstring.data, md->topicName->lenstring.len);
    szTopicName[md->topicName->lenstring.len] = 0;
    LE_INFO("\nReceived message from topic: %s", szTopicName);

    LE_INFO("\nIncoming data:\n%.*s%s\n", (int)message->payloadlen, (char*)message->payload, " ");

    char* szPayload = (char *) malloc(message->payloadlen+1);

    memcpy(szPayload, (char*)message->payload, message->payloadlen);
    szPayload[message->payloadlen] = 0;

    //decode JSON payload

    char* pszCommand = swirjson_getValue(szPayload, -1, "command");
    if (pszCommand)
    {
        char*   pszUid = swirjson_getValue(szPayload, -1, "uid");
        char*   pszTimestamp = swirjson_getValue(szPayload, -1, "timestamp");
        char*   pszId = swirjson_getValue(pszCommand, -1, "id");
        char*   pszParam = swirjson_getValue(pszCommand, -1, "params");

        int     i;

        for (i=0; i<AV_JSON_KEY_MAX_COUNT; i++)
        {
            char szKey[AV_JSON_KEY_MAX_LENGTH];

            char * pszValue = swirjson_getValue(pszParam, i, szKey);

            if (pszValue)
            {
                LE_INFO("--> Incoming message from AirVantage: %s.%s = %s @ %s", pszId, szKey, pszValue, pszTimestamp);

                //receiverApi_Notify(pszId, szKey, pszValue, pszTimestamp);
                char szFullKey[128];

                sprintf(szFullKey, "%s.%s", pszId, szKey);
                SendMqttIncomingMessageEvent(szTopicName, szFullKey, pszValue, pszTimestamp);

                free(pszValue);
            }
            else
            {
                break;
            }
        }

        publishAckCmd(pszUid, 0, "");

        free(pszCommand);

        if (pszId)
        {
            free(pszId);
        }
        if (pszParam)
        {
            free(pszParam);
        }
        if (pszTimestamp)
        {
            free(pszTimestamp);
        }
        if (pszUid)
        {
            free(pszUid);
        }
    }

    if (szPayload)
    {
        free(szPayload);
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Send Mqtt connection state event
 */
//--------------------------------------------------------------------------------------------------
static void SendMqttConnStateEvent
(
    bool        bIsConnected,
    int32_t     nConnectErrorCode,
    int32_t     nSubErrorCode
)
{
    // Init the event data
    MqttConnStateData_t eventData;
    eventData.bIsConnected = bIsConnected;
    eventData.nConnectErrorCode = nConnectErrorCode;
    eventData.nSubErrorCode = nSubErrorCode;

    LE_DEBUG("Reporting MQTT state[%i], %d, %d", eventData.bIsConnected, eventData.nConnectErrorCode, eventData.nSubErrorCode);

    // Send the event to interested applications
    le_event_Report(MqttConnStateEvent, &eventData, sizeof(eventData));
}

//--------------------------------------------------------------------------------------------------
/**
 *  Handle MQTT message
 */
//--------------------------------------------------------------------------------------------------
static int InitMqtt
(
    void
)
{
    int     rc = 0;

    NewNetwork(&g_stContext.oNetwork);
    ConnectNetwork(&g_stContext.oNetwork, g_stContext.szBrokerUrl, g_stContext.u32PortNumber);
    MQTTClient(&g_stContext.oMqttClient, &g_stContext.oNetwork, TIMEOUT_MS, g_buf, sizeof(g_buf), g_readbuf, sizeof(g_readbuf));

    LE_INFO("Connecting to connect to tcp://%s:%d\n", g_stContext.szBrokerUrl, g_stContext.u32PortNumber);

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.willFlag = 0;
    data.MQTTVersion = MQTT_VERSION;

    if (strlen(g_stContext.szDeviceId) > 0)
    {
        data.clientID.cstring = g_stContext.szDeviceId;
        data.username.cstring = g_stContext.szDeviceId;
        data.password.cstring = g_stContext.szSecret;
        LE_INFO("   Using deviceID= %s, pwd= %s", g_stContext.szDeviceId, g_stContext.szSecret);
    }

    data.keepAliveInterval = g_stContext.u32KeepAlive;
    data.cleansession = 1;

    rc = MQTTConnect(&g_stContext.oMqttClient, &data);

    LE_INFO("MQTT connection status= %d\n", rc);
    if (g_stContext.oMqttClient.isconnected)
    {
        LE_INFO("MQTT connected");
    }

    if (rc == SUCCESS)
    {
        //connected
        LE_INFO("MQTT connected...");


        LE_INFO("Subscribing to %s\n", g_stContext.szSubscribeTopic);
        rc = MQTTSubscribe(&g_stContext.oMqttClient, g_stContext.szSubscribeTopic, 0, onIncomingMessage);
        LE_INFO("Subscription return code: %d\n", rc);

        if (rc != SUCCESS)
        {
            sleep(2);
            LE_INFO("Reattempt to Subscribe to %s\n", g_stContext.szSubscribeTopic);
            rc = MQTTSubscribe(&g_stContext.oMqttClient, g_stContext.szSubscribeTopic, 0, onIncomingMessage);
            LE_INFO("Subscription return code: %d\n", rc);
        }

        if (rc == SUCCESS)
        {
            g_eState = MQTT_CONNECTED;

            LE_INFO("Starting Timer for Mqtt Yield");
            le_timer_Ref_t timerRef = le_timer_Create("timerApp");
            LE_FATAL_IF(timerRef == NULL, "timerApp timer ref is NULL");

            le_clk_Time_t   interval = { 10, 0 };
            le_result_t     res;

            res = le_timer_SetInterval(timerRef, interval);
            LE_FATAL_IF(res != LE_OK, "set interval to %lu seconds: %d", interval.sec, res);

            res = le_timer_SetRepeat(timerRef, 1);
            LE_FATAL_IF(res != LE_OK, "set repeat to once: %d", res);

            le_timer_SetHandler(timerRef, timerHandler);

            StartTimer(timerRef);

            SendMqttConnStateEvent(true, 0, rc);

            return 0;
        }
        else
        {
            LE_INFO("MQTT disconnect");
            MQTTDisconnect(&g_stContext.oMqttClient);

            SendMqttConnStateEvent(false, rc, -1);

            g_eState = MQTT_DISCONNECTED;
        }
    }
    else
    {
        LE_INFO("MQTT connection Failed");

        SendMqttConnStateEvent(false, rc, -1);

        g_eState = MQTT_DISCONNECTED;
    }

    return 1;
}


//--------------------------------------------------------------------------------------------------
/**
 *  Event callback for data connection state changes.
 */
//--------------------------------------------------------------------------------------------------
static void DcsStateHandler
(
    const char* intfName,
    bool        isConnected,
    void*       contextPtr
)
{
    if (isConnected)
    {
        if (g_eState == CONNECTING)
        {
            g_eState = DATA_CONNECTED;

            #if 0
            if (g_stContext.oMqttClient.isconnected)
            {
                LE_INFO("MQTT session already established");
                return;
            }
            #endif

            LE_INFO("%s connected! Starting MQTT session", intfName);

            if (InitMqtt())
            {
                DisconnectData();
                LE_INFO("Failed to open MQTT session, close data connection");
            }
        }
        else
        {
            LE_INFO("No ongoing MQTT Connection request");
        }
    }
    else
    {
        LE_INFO("%s disconnected!", intfName);

        DisconnectData();
    }
}

static void ViewConfig
(
    void
)
{
    PrintMessage("Current MQTT Setting is: %s:%lu, keepAlive=%lu, QoS=%lu",
                g_stContext.szBrokerUrl,
                (long unsigned int) g_stContext.u32PortNumber,
                (long unsigned int) g_stContext.u32KeepAlive,
                (long unsigned int) g_stContext.u32QoS);
}

static void Config
(
    const char*     szBrokerUrl,
    int32_t         n32PortNumber,
    int32_t         n32KeepAlive,
    int32_t         n32QoS
)
{
    if (strlen(szBrokerUrl) > 0)
    {
        PrintMessage("Previous MQTT Broker URL was: %s", g_stContext.szBrokerUrl);
        strcpy(g_stContext.szBrokerUrl, szBrokerUrl);
        PrintMessage("New MQTT Broker URL is now: %s", g_stContext.szBrokerUrl);
    }

    if (n32PortNumber != -1)
    {
        PrintMessage("Previous MQTT Broker Port was: %lu", (long unsigned int) g_stContext.u32PortNumber);
        g_stContext.u32PortNumber = n32PortNumber;
        PrintMessage("New MQTT Broker is now: %lu", (long unsigned int) g_stContext.u32PortNumber);
    }

    if (n32KeepAlive != -1)
    {
        PrintMessage("Previous Keep Alive was: %lu seconds", (long unsigned int) g_stContext.u32KeepAlive);
        g_stContext.u32KeepAlive = n32KeepAlive;
        PrintMessage("New Keep Alive is now: %lu seconds", (long unsigned int) g_stContext.u32KeepAlive);
    }

    if (n32QoS != -1)
    {
        PrintMessage("Previous QoS was: %lu", (long unsigned int) g_stContext.u32QoS);
        g_stContext.u32QoS = n32QoS;
        PrintMessage("New QoS is now: %lu", (long unsigned int) g_stContext.u32QoS);
    }

    PrintMessage("New settings will be applied at the next connection");
}

void Connect
(
    const char*   szUser,
    const char*   szPassword
)
{
    if (g_eState == MQTT_CONNECTED)
    {
        LE_INFO("\n*** MQTT session already active ***\n");
        SendMqttConnStateEvent(true, 0, 0);
        return;
    }

    if (g_eState == IDLE)
    {
        LE_INFO("Idle, call Connect()");

        if (strlen(szUser) > 0)
        {
            strcpy(g_stContext.szDeviceId, szUser);
            strcpy(g_stContext.szSecret, szPassword);
        }
        else if (strlen(szPassword) > 0)
        {
            strcpy(g_stContext.szSecret, szPassword);
        }

        // register handler for data connection state change
        if (!g_hDataConnectionState)
        {
            g_hDataConnectionState = le_data_AddConnectionStateHandler(DcsStateHandler, NULL);
        }

        LE_INFO("Initiated Data Connection");
        ConnectData();
    }
    else
    {

        LE_INFO("Already Connecting, try later");
        SendMqttConnStateEvent(false, 1, -1);
    }
}

void Disconnect
(
    void
)
{
    DisconnectData();
}

//--------------------------------------------------------------------------------------------------
// APIs.
//--------------------------------------------------------------------------------------------------

void mqttApi_ViewConfig
(
)
{
    LE_INFO("API: Received MQTT View Config request...");
    ViewConfig();
}

void mqttApi_Config
(
    const char*     szBrokerUrl, ///< [IN] URL
    int32_t         n32PortNumber,
    int32_t         n32KeepAlive,
    int32_t         n32QoS
)
{
    LE_INFO("API: Received MQTT Set Broker request...");
    Config(szBrokerUrl, n32PortNumber, n32KeepAlive, n32QoS);
}

void mqttApi_Connect
(
    const char*  szUser, ///< [IN] username
    const char*  szPassword ///< [IN] password
)
{
    LE_INFO("API: Received MQTT Connect request...");
    Connect(szUser, szPassword);
}

void mqttApi_Disconnect
(
    void
)
{
    LE_INFO("API: Received MQTT Disconnect request...");
    Disconnect();
}

void mqttApi_Send
(
    const char*  szKey, ///< [IN] Key
    const char*  szValue, ///< [IN] Value
    int32_t*     pi32ReturnCode ///< [OUT] errCode
)
{
    LE_INFO("API: Received MQTT Sendind request...");
    *pi32ReturnCode = SendMessage(szKey, szValue);
}

//--------------------------------------------------------------------------------------------------
/**
 * The first-layer Session State Handler
 *
 */
//--------------------------------------------------------------------------------------------------
static void FirstLayerSessionStateHandler
(
    void* reportPtr,
    void* secondLayerHandlerFunc
)
{
    MqttConnStateData_t* eventDataPtr = reportPtr;
    mqttApi_SessionStateHandlerFunc_t clientHandlerFunc = secondLayerHandlerFunc;

    clientHandlerFunc(
                      eventDataPtr->bIsConnected,
                      eventDataPtr->nConnectErrorCode,
                      eventDataPtr->nSubErrorCode,
                      le_event_GetContextPtr());
}

//--------------------------------------------------------------------------------------------------
/**
 * This function adds a handler ...
 */
//--------------------------------------------------------------------------------------------------
mqttApi_SessionStateHandlerRef_t mqttApi_AddSessionStateHandler
(
    mqttApi_SessionStateHandlerFunc_t   handlerPtr,
    void*                               contextPtr
)
{
    LE_DEBUG("%p", handlerPtr);
    LE_DEBUG("%p", contextPtr);

    le_event_HandlerRef_t handlerRef = le_event_AddLayeredHandler(
                                                    "MqttConnState",
                                                    MqttConnStateEvent,
                                                    FirstLayerSessionStateHandler,
                                                    (le_event_HandlerFunc_t)handlerPtr);

    le_event_SetContextPtr(handlerRef, contextPtr);

    return (mqttApi_SessionStateHandlerRef_t)(handlerRef);
}


//--------------------------------------------------------------------------------------------------
/**
 * This function removes a handler ...
 */
//--------------------------------------------------------------------------------------------------
void mqttApi_RemoveSessionStateHandler
(
    mqttApi_SessionStateHandlerRef_t addHandlerRef
)
{
    LE_DEBUG("%p", addHandlerRef);

    le_event_RemoveHandler((le_event_HandlerRef_t)addHandlerRef);
}

//--------------------------------------------------------------------------------------------------
/**
 * The first-layer Session State Handler
 *
 */
//--------------------------------------------------------------------------------------------------
static void FirstLayerIncomingMessageHandler
(
    void* reportPtr,
    void* secondLayerHandlerFunc
)
{
    MqttInMsgData_t*                        eventDataPtr = reportPtr;
    mqttApi_IncomingMessageHandlerFunc_t    clientHandlerFunc = secondLayerHandlerFunc;

    clientHandlerFunc(
                      eventDataPtr->szTopicName,
                      eventDataPtr->szKeyName,
                      eventDataPtr->szValue,
                      eventDataPtr->szTimestamp,
                      le_event_GetContextPtr());
}

//--------------------------------------------------------------------------------------------------
/**
 * This function adds a handler ...
 */
//--------------------------------------------------------------------------------------------------
mqttApi_IncomingMessageHandlerRef_t mqttApi_AddIncomingMessageHandler
(
    mqttApi_IncomingMessageHandlerFunc_t   handlerPtr,
    void*                                  contextPtr
)
{
    LE_DEBUG("%p", handlerPtr);
    LE_DEBUG("%p", contextPtr);

    le_event_HandlerRef_t handlerRef = le_event_AddLayeredHandler(
                                                    "MqttIncomingMessage",
                                                    MqttInMsgEvent,
                                                    FirstLayerIncomingMessageHandler,
                                                    (le_event_HandlerFunc_t)handlerPtr);

    le_event_SetContextPtr(handlerRef, contextPtr);

    return (mqttApi_IncomingMessageHandlerRef_t)(handlerRef);
}


//--------------------------------------------------------------------------------------------------
/**
 * This function removes a handler ...
 */
//--------------------------------------------------------------------------------------------------
void mqttApi_RemoveIncomingMessageHandler
(
    mqttApi_IncomingMessageHandlerRef_t addHandlerRef
)
{
    LE_DEBUG("%p", addHandlerRef);

    le_event_RemoveHandler((le_event_HandlerRef_t)addHandlerRef);
}


//--------------------------------------------------------------------------------------------------
/**
 *  Main function.
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    LE_INFO("---------- Launching mqttClient ----------");

    MqttConnStateEvent = le_event_CreateId("Mqtt Conn State", sizeof(MqttConnStateData_t));
    MqttInMsgEvent  = le_event_CreateId("Mqtt InMsg", sizeof(MqttInMsgData_t));


    //Set default mqtt broker to be AirVantage
    strcpy(g_stContext.szBrokerUrl, URL_AIRVANTAGE_SERVER);
    g_stContext.u32PortNumber = PORT_AIRVANTAGE_SERVER;

    g_stContext.u32KeepAlive = DEFAULT_KEEP_ALIVE;
    g_stContext.u32QoS       = DEFAULT_QOS;

    strcpy(g_stContext.szSecret, "");
    strcpy(g_stContext.szKey, "");
    strcpy(g_stContext.szValue, "");

    le_info_ConnectService();
    le_info_GetImei(g_stContext.szDeviceId, sizeof(g_stContext.szDeviceId));
    //strcpy(g_stContext.szDeviceId, "00000000B6AF4AFF"); //default

    sprintf(g_stContext.szSubscribeTopic, "%s%s", g_stContext.szDeviceId, TOPIC_NAME_SUBSCRIBE);

    LE_INFO("mqttClient Launched, IMEI= %s", g_stContext.szDeviceId);

    g_eState = IDLE;
}

