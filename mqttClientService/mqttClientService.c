#include "legato.h"
#include "interfaces.h"

#include "MQTTClient.h"
#include "Socket.h"

typedef struct mqtt_Session
{
    MQTTClient client;
    MQTTClient_connectOptions connectOptions;
    mqtt_MessageArrivedHandlerFunc_t messageArrivedHandler;
    void* messageArrivedHandlerContext;
    mqtt_ConnectionLostHandlerFunc_t connectionLostHandler;
    void* connectionLostHandlerContext;
    le_sls_Link_t link;
} mqtt_Session;

static int mqtt_QosEnumToValue(mqtt_Qos_t qos);
static void mqtt_ConnectionLostHandler(void* context, char* cause);
static void connectionLostEventHandler(void* report);
static int mqtt_MessageArrivedHandler(
    void* context, char* topicName, int topicLen, MQTTClient_message* message);
static void messageReceivedEventHandler(void* report);

//--------------------------------------------------------------------------------------------------
/**
 * sessions is a hashmap from le_msg_SessionRef_t to a le_sls_List_t*.  The list contains nodes of
 * type mqtt_Session.  Each node contains the context of a single MQTT session with a broker.
 *
 * @note
 *      The session data is wholly owned by this module, so we should never leak MQTT related
 *      resources as the result of a client terminating unexpectedly.
 */
//--------------------------------------------------------------------------------------------------
static le_hashmap_Ref_t sessions;

//--------------------------------------------------------------------------------------------------
/**
 * Event id that is used to signal that a message has been received from the MQTT broker.  Events
 * must be used because paho spawns a thread for receiving messages and that means that the
 * callback can't call IPC methods because it is from a non-legato thread.
 * mqttClientService thread 
 */
//--------------------------------------------------------------------------------------------------
static le_event_Id_t receiveThreadEventId;

//--------------------------------------------------------------------------------------------------
/**
 * Event id for connection lost events from paho.  The justification for this event is the same as
 * for receiveThreadEventId.
 */
//--------------------------------------------------------------------------------------------------
static le_event_Id_t connectionLostThreadEventId;

//--------------------------------------------------------------------------------------------------
/**
 * Represents a message which has been received from the MQTT broker.
 */
//--------------------------------------------------------------------------------------------------
typedef struct mqtt_Message
{
    mqtt_Session* session;
    char* topic;
    size_t topicLength;
    uint8_t* payload;
    size_t payloadLength;
} mqtt_Message;


//--------------------------------------------------------------------------------------------------
/**
 * Creates an MQTT session object.
 *
 * @return
 *      LE_OK on success or LE_FAULT on failure
 *
 * @note
 *      SSL support has not been tested and will not work until the SSL options are setup correctly
 *      by mqtt_SetConnectOptions().  I believe we will have to bundle certificate files into the
 *      app as part of this exercise.  It seems that the ssl options structure wants a filesystem
 *      path to a single .pem file, but there are 168 .pem files in /etc/ssl/certs.  Which one
 *      should we provide?
 */
//--------------------------------------------------------------------------------------------------
le_result_t mqtt_CreateSession
(
    const char* brokerURI,      ///< [IN] The URI of the MQTT broker to connect to.  Should be in
                                ///  the form protocol://host:port. eg. tcp://1.2.3.4:1883 or
                                ///  ssl://example.com:8883
    const char* clientId,       ///< [IN] Any unique string.  If a client connects to an MQTT
                                ///  broker using the same clientId as an existing session, then
                                ///  the existing session will be terminated.
    mqtt_SessionRef_t* session  ///< [OUT] The created session if the return result is LE_OK
)
{
    *session = calloc(sizeof(mqtt_Session), 1);
    LE_ASSERT(*session);
    const MQTTClient_connectOptions initConnOpts = MQTTClient_connectOptions_initializer;
    memcpy(&(*session)->connectOptions, &initConnOpts, sizeof(MQTTClient_connectOptions));
    const int createResult = MQTTClient_create(
            &(*session)->client,
            brokerURI,
            clientId,
            MQTTCLIENT_PERSISTENCE_NONE,
            NULL);
    if (createResult != MQTTCLIENT_SUCCESS)
    {
        LE_ERROR("Couldn't create MQTT session.  Paho error code: %d", createResult);
        free(*session);
        *session = NULL;
        return LE_FAULT;
    }

    le_msg_SessionRef_t clientSession = mqtt_GetClientSessionRef();
    le_sls_List_t* mqttSessionsForClient = le_hashmap_Get(sessions, clientSession);
    if (mqttSessionsForClient == NULL)
    {
        // There are no MQTT sessions for this client session
        mqttSessionsForClient = calloc(sizeof(le_sls_List_t), 1);
        LE_ASSERT(mqttSessionsForClient);
        *mqttSessionsForClient = LE_SLS_LIST_INIT;
        LE_FATAL_IF(
            le_hashmap_Put(sessions, clientSession, mqttSessionsForClient) != NULL,
            "There should not be an element in the map with this key");
    }
    le_sls_Queue(mqttSessionsForClient, &(*session)->link);

    LE_ASSERT(MQTTClient_setCallbacks(
            (*session)->client,
            (*session),
            &mqtt_ConnectionLostHandler,
            &mqtt_MessageArrivedHandler,
            NULL) == MQTTCLIENT_SUCCESS);

    return LE_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * Destroy the given session.
 *
 * @note
 *      All MQTT sessions associated with the client session will be destroyed automatically when
 *      the client disconnects from the MQTT service.
 */
//--------------------------------------------------------------------------------------------------
void mqtt_DestroySession
(
    mqtt_SessionRef_t session  ///< Session to destroy
)
{
    le_msg_SessionRef_t clientSession = mqtt_GetClientSessionRef();
    le_sls_List_t* mqttSessionsForClient = le_hashmap_Get(sessions, clientSession);
    if (mqttSessionsForClient == NULL)
    {
        LE_KILL_CLIENT("Tried to destroy a sessions, but there are no sessions for this client");
    }

    le_sls_Link_t* prevListLink = NULL;
    le_sls_Link_t* listLink = le_sls_Peek(mqttSessionsForClient);
    while (true)
    {
        if (listLink == NULL)
        {
            LE_KILL_CLIENT("Tried to destroy a session that doesn't exist for this client");
            break;
        }
        mqtt_Session* s = CONTAINER_OF(listLink, mqtt_Session, link);
        if (s == session)
        {
            // s is the session we want to remove
            le_sls_Link_t* removedLink = (prevListLink == NULL) ?
                le_sls_Pop(mqttSessionsForClient) :
                le_sls_RemoveAfter(mqttSessionsForClient, prevListLink);
            LE_ASSERT(removedLink != NULL);
            // s (a.k.a. session) contain removedLink, so no need to do CONTAINER_OF on removedLInk
            MQTTClient_destroy(s->client);
            // It is necessary to cast to char* from const char* in order to free the memory
            // associated with the username and password.
            free((char*)s->connectOptions.username);
            free((char*)s->connectOptions.password);
            free(s);
        }
        prevListLink = listLink;
        listLink = le_sls_PeekNext(mqttSessionsForClient, listLink);
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Set the connections options which will be used during subsequent calls to mqtt_Connect().
 */
//--------------------------------------------------------------------------------------------------
void mqtt_SetConnectOptions
(
    mqtt_SessionRef_t session,  ///< [IN] Session to set connection options in
    uint16_t keepAliveInterval, ///< [IN] How often to send an MQTT PINGREQ packet if no other
                                ///  packets are received
    bool cleanSession,          ///< [IN] When false, restore the previous state on a reconnect
    // TODO: will message
    const char* username,       ///< [IN] username to connect with
    const uint8_t* password,    ///< [IN] password to connect with
    size_t passwordLength,      ///< [IN] length of the password in bytes
    uint16_t connectTimeout,    ///< [IN] connect timeout in seconds
    uint16_t retryInterval      ///< [IN] retry interval in seconds
)
{
    session->connectOptions.keepAliveInterval = keepAliveInterval;
    session->connectOptions.cleansession = cleanSession;

    // username
    free((char*)session->connectOptions.username);
    if (username != NULL)
    {
        session->connectOptions.username = calloc(strlen(username) + 1, 1);
        LE_ASSERT(session->connectOptions.username != NULL);
        strcpy((char*)session->connectOptions.username, username);
    }
    else
    {
        session->connectOptions.username = NULL;
    }

    // password
    free((char*)session->connectOptions.password);
    if (password != NULL)
    {
        // paho uses null terminated strings for passwords, so the password may not contain any
        // embedded null characters.
        for (size_t i = 0; i < passwordLength; i++)
        {
            if (password[i] == 0)
            {
                LE_KILL_CLIENT(
                    "Password contains embedded null characters and this is not currently "
                    "supported by this implementation");
                break;
            }
        }
        session->connectOptions.password = calloc(passwordLength + 1, 1);
        LE_ASSERT(session->connectOptions.password != NULL);
        memcpy((uint8_t*)session->connectOptions.password, password, passwordLength);
        ((uint8_t*)session->connectOptions.password)[passwordLength] = '\0';
    }
    else
    {
        session->connectOptions.password = NULL;
        if (username != NULL)
        {
            LE_KILL_CLIENT("It is illegal to specify a password without a username");
        }
    }

    session->connectOptions.connectTimeout = connectTimeout;
    session->connectOptions.retryInterval = retryInterval;
}

//--------------------------------------------------------------------------------------------------
/**
 * Connect to the MQTT broker using the provided session.
 *
 * @return
 *      - LE_OK on success
 *      - LE_BAD_PARAMETER if the connection options are bad
 *      - LE_FAULT for general failures
 */
//--------------------------------------------------------------------------------------------------
le_result_t mqtt_Connect
(
    mqtt_SessionRef_t session
)
{
    const int connectResult = MQTTClient_connect(session->client, &session->connectOptions);
    le_result_t result;
    switch (connectResult)
    {
        case SOCKET_ERROR:
            LE_WARN("Socket error");
            result = LE_FAULT;
            break;

        case MQTTCLIENT_NULL_PARAMETER:
        case MQTTCLIENT_BAD_STRUCTURE:
        case MQTTCLIENT_BAD_UTF8_STRING:
            result = LE_BAD_PARAMETER;
            break;

        case MQTTCLIENT_SUCCESS:
            result = LE_OK;
            break;

         default:
            LE_WARN("Paho connect returned (%d)", connectResult);
            result = LE_FAULT;
            break;
     }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Disconnect a currently connected session
 *
 * @return
 *      - LE_OK on success
 *      - LE_FAULT on failure
 *
 * @note
 *      TODO: If the connection is lost right as disconnect is called, I think that this function
 *      will return LE_FAULT and the client will not know why.
 */
//--------------------------------------------------------------------------------------------------
le_result_t mqtt_Disconnect
(
    mqtt_SessionRef_t session
)
{
    const int waitBeforeDisconnectMs = 0;
    const int disconnectResult = MQTTClient_disconnect(session->client, waitBeforeDisconnectMs);
    le_result_t result;
    switch (disconnectResult)
    {
        case MQTTCLIENT_SUCCESS:
            result = LE_OK;
            break;

        case MQTTCLIENT_FAILURE:
            result = LE_FAULT;
            break;

        case MQTTCLIENT_DISCONNECTED:
            result = LE_FAULT;
            LE_WARN("Already disconnected");
            break;

        default:
            LE_WARN("Paho disconnect returned (%d)", disconnectResult);
            result = LE_FAULT;
            break;
    }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Publish the supplied payload to the MQTT broker on the given topic.
 *
 * @return
 *      LE_OK on success or LE_FAULT on failure
 */
//--------------------------------------------------------------------------------------------------
le_result_t mqtt_Publish
(
    mqtt_SessionRef_t session,
    const char* topic,
    const uint8_t* payload,
    size_t payloadLen,
    mqtt_Qos_t qos,
    bool retain
)
{
    MQTTClient_deliveryToken* dtNotUsed = NULL;
    const int publishResult = MQTTClient_publish(
        session->client, topic, payloadLen, (void*)payload, mqtt_QosEnumToValue(qos), retain, dtNotUsed);
    le_result_t result = LE_OK;
    if (publishResult != MQTTCLIENT_SUCCESS)
    {
        LE_WARN("Publish failed with error code (%d)", publishResult);
        result = LE_FAULT;
    }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Subscribe to the given topic pattern.  Topics look like UNIX filesystem paths.  Eg.
 * "/bedroom/sensors/motion".  Patterns may include special wildcard characters "+" and "#" to match
 * one or multiple levels of a topic.  For example. "/#/motion" will match topics
 * "/bedroom/sensors/motion" and "/car/data/motion", but not "/bedroom/sensors/motion/enabled".
 *
 * @return
 *      LE_OK on success or LE_FAULT on failure
 */
//--------------------------------------------------------------------------------------------------
le_result_t mqtt_Subscribe
(
    mqtt_SessionRef_t session,
    const char* topicPattern,
    mqtt_Qos_t qos
)
{
    const int subscribeResult = MQTTClient_subscribe(session->client, topicPattern, qos);
    le_result_t result = LE_OK;
    if (subscribeResult != MQTTCLIENT_SUCCESS)
    {
        LE_WARN("Subscribe failed with error code (%d)", subscribeResult);
        result = LE_FAULT;
    }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Unsubscribe from the given topic pattern.
 *
 * @return
 *      LE_OK on success or LE_FAULT on failure.
 */
//--------------------------------------------------------------------------------------------------
le_result_t mqtt_Unsubscribe
(
    mqtt_SessionRef_t session,
    const char* topicPattern
)
{
    const int unsubscribeResult = MQTTClient_unsubscribe(session->client, topicPattern);
    le_result_t result = LE_OK;
    if (unsubscribeResult != MQTTCLIENT_SUCCESS)
    {
        LE_WARN("Unsubscribe failed with error code (%d)", unsubscribeResult);
        result = LE_FAULT;
    }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Set the connection lost handler for the session.
 *
 * @return
 *      A handle which allows the connection lost handler to be removed.
 *
 * @note
 *      Only one handler may be registered
 */
//--------------------------------------------------------------------------------------------------
mqtt_ConnectionLostHandlerRef_t mqtt_AddConnectionLostHandler
(
    mqtt_SessionRef_t session,
    mqtt_ConnectionLostHandlerFunc_t handler,
    void* context
)
{
    if (session->connectionLostHandler != NULL)
    {
        LE_KILL_CLIENT("You may only register one connection lost handler");
    }
    else
    {
        session->connectionLostHandler = handler;
        session->connectionLostHandlerContext = context;
    }

    return (mqtt_ConnectionLostHandlerRef_t)session;
}

//--------------------------------------------------------------------------------------------------
/**
 * Deregister the connection lost handler for the session.
 */
//--------------------------------------------------------------------------------------------------
void mqtt_RemoveConnectionLostHandler
(
    mqtt_ConnectionLostHandlerRef_t handler
)
{
    mqtt_Session* session = (mqtt_Session*)handler;
    session->connectionLostHandler = NULL;
    session->connectionLostHandlerContext = NULL;
}

//--------------------------------------------------------------------------------------------------
/**
 * Set the message arrived handler for the session.
 *
 * @return
 *      A handle which can be used to deregister the message arrived handler.
 *
 * @note
 *      Only one message arrived handler may be registered for a session.
 */
//--------------------------------------------------------------------------------------------------
mqtt_MessageArrivedHandlerRef_t mqtt_AddMessageArrivedHandler
(
    mqtt_SessionRef_t session,
    mqtt_MessageArrivedHandlerFunc_t handler,
    void* context
)
{
    if (session->messageArrivedHandler != NULL)
    {
        LE_KILL_CLIENT("You may only register one message arrived handler");
    }
    else
    {
        session->messageArrivedHandler = handler;
        session->messageArrivedHandlerContext = context;
    }

    return (mqtt_MessageArrivedHandlerRef_t)session;
}

//--------------------------------------------------------------------------------------------------
/**
 * Deregister the message arrived handler for the session.
 */
//--------------------------------------------------------------------------------------------------
void mqtt_RemoveMessageArrivedHandler
(
    mqtt_MessageArrivedHandlerRef_t handler
)
{
    mqtt_Session* session = (mqtt_Session*)handler;
    session->messageArrivedHandler = NULL;
    session->messageArrivedHandlerContext = NULL;
}

//--------------------------------------------------------------------------------------------------
/**
 * Extracts the topic and payload from a message reference.
 */
//--------------------------------------------------------------------------------------------------
void mqtt_GetMessage
(
    mqtt_MessageRef_t message, ///< [IN] Message to extract the data from
    char* topic,               ///< [OUT] Topic of the message
    size_t topicLength,        ///< [IN] Length of the buffer allocated to topic
    uint8_t* payload,          ///< [OUT] Payload of the message
    size_t* payloadLength      ///< [IN/OUT] As input, the size of the payload buffer.  As output,
                               ///  the number of bytes of the payload buffer that are valid.
)
{
    if (message->topicLength > topicLength)
    {
        LE_KILL_CLIENT("topic buffer is too small");
        goto done;
    }
    memcpy(topic, message->topic, message->topicLength);

    if (message->payloadLength > *payloadLength)
    {
        LE_KILL_CLIENT("payload buffer is too small");
        goto done;
    }
    memcpy(payload, message->payload, message->payloadLength);
    *payloadLength = message->payloadLength;

done:
    free(message->topic);
    free(message->payload);
    free(message);
}

//--------------------------------------------------------------------------------------------------
/**
 * Gets the correct quality of service integer value as defined by the MQTT specification from the
 * enum type.
 *
 * @return
 *      The QoS value as defined by the MQTT specification
 */
//--------------------------------------------------------------------------------------------------
static int mqtt_QosEnumToValue
(
    mqtt_Qos_t qos  ///< The QoS enum value to convert.
)
{
    int result = 0;
    switch (qos)
    {
        case MQTT_QOS0_TRANSMIT_ONCE:
            result = 0;
            break;

        case MQTT_QOS1_RECEIVE_AT_LEAST_ONCE:
            result = 1;
            break;

        case MQTT_QOS2_RECEIVE_EXACTLY_ONCE:
            result = 2;
            break;

        default:
            LE_KILL_CLIENT("Invalid QoS setting (%d)", qos);
            result = 0;
            break;
    }
    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * This is the connection lost callback function that is supplied to the paho library.  The
 * function generates an event rather than calling the client supplied callback because this
 * function will be called on a non-Legato thread.
 */
//--------------------------------------------------------------------------------------------------
static void mqtt_ConnectionLostHandler
(
    void* context, ///< context parameter contains the session for which the connection was lost
    char* cause    ///< paho library doesn't currently populate this
)
{
    mqtt_Session* session = context;
    le_event_Report(connectionLostThreadEventId, &session, sizeof(mqtt_Session*));
}

//--------------------------------------------------------------------------------------------------
/**
 * The event handler for the connection lost event that is generated by mqtt_ConnectionLostHandler.
 * This function calls the handler supplied by the client.
 */
//--------------------------------------------------------------------------------------------------
static void connectionLostEventHandler(void* report)
{
    mqtt_Session* session = *((mqtt_Session**)report);
    if (session->connectionLostHandler != NULL)
    {
        session->connectionLostHandler(session->connectionLostHandlerContext);
    }
    else
    {
        LE_WARN("Connection was lost, but no handler is registered to receive the notification");
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * This is the message arrived callback function that is supplied to the paho library.  The
 * function generates an event rather than calling the client supplied callback because this
 * function will be called on a non-Legato thread.
 */
//--------------------------------------------------------------------------------------------------
static int mqtt_MessageArrivedHandler
(
    void* context,
    char* topicName,
    int topicLen,
    MQTTClient_message* message
)
{
    // TODO: this isn't really safe because the session could possibly be destroyed by
    // mqtt_DestroySession() before this function is called.
    mqtt_Session* session = context;

    // NOTE: We are dynamically allocating memory that will only be free *if* the client calls 
    // mqtt_GetMessage to get this message.  This should be fixed once legato allows handlers to
    // take arrays as parameters.
    mqtt_Message* storedMsg = calloc(sizeof(mqtt_Message), 1);
    LE_ASSERT(storedMsg);

    storedMsg->session = session;

    // When topicLen is 0 it means that the topic contains embedded nulls and can't be treated as a
    // normal C string
    storedMsg->topicLength = (topicLen == 0) ? (strlen(topicName) + 1) : topicLen;
    storedMsg->topic = calloc(storedMsg->topicLength, 1);
    LE_ASSERT(storedMsg->topic);
    memcpy(storedMsg->topic, topicName, storedMsg->topicLength);

    storedMsg->payloadLength = message->payloadlen;
    storedMsg->payload = calloc(message->payloadlen, 1);
    LE_ASSERT(storedMsg->payload);
    memcpy(storedMsg->payload, message->payload, storedMsg->payloadLength);

    le_event_Report(receiveThreadEventId, &storedMsg, sizeof(mqtt_Message*));

    const bool messageReceivedSuccessfully = true;
    return messageReceivedSuccessfully;
}

//--------------------------------------------------------------------------------------------------
/**
 * The event handler for the message arrived event that is generated by mqtt_MessageArrivedHandler.
 * This function calls the handler supplied by the client.
 */
//--------------------------------------------------------------------------------------------------
static void messageReceivedEventHandler(void* report)
{
    mqtt_Message* storedMsg = *((mqtt_Message**)report);

    if (storedMsg->topicLength > MQTT_MAX_TOPIC_LENGTH ||
        storedMsg->payloadLength > MQTT_MAX_PAYLOAD_LENGTH)
    {
        LE_WARN("Message arrived from broker, but it is too large to deliver using Legato IPC");
    }
    else
    {
        if (storedMsg->session->messageArrivedHandler != NULL)
        {
            storedMsg->session->messageArrivedHandler(
                storedMsg, storedMsg->session->messageArrivedHandlerContext);
        }
        else
        {
            LE_WARN(
                "Message has arrived, but no handler is registered to receive the notification");
            free(storedMsg->topic);
            free(storedMsg->payload);
            free(storedMsg);
        }
    }
}


COMPONENT_INIT
{
    const size_t initialCapacity = 4;
    sessions = le_hashmap_Create(
        "MQTT Sessions",
        initialCapacity,
        le_hashmap_HashVoidPointer,
        le_hashmap_EqualsVoidPointer);

    receiveThreadEventId = le_event_CreateId(
        "MqttClient receive notification", sizeof(mqtt_Message*));
    le_event_AddHandler(
        "MqttClient receive notification", receiveThreadEventId, messageReceivedEventHandler);

    connectionLostThreadEventId = le_event_CreateId(
        "MqttClient connection lost notification", sizeof(mqtt_Message*));
    le_event_AddHandler(
        "MqttClient connection lost notification",
        connectionLostThreadEventId,
        connectionLostEventHandler);
}
