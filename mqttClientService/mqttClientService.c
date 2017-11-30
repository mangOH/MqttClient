#include "legato.h"
#include "interfaces.h"

#include "MQTTClient.h"
#include "Socket.h"

static const char *SslCaCertsPath = "/etc/ssl/certs/ca-certificates.crt";

typedef struct mqtt_Session
{
    MQTTClient client;
    MQTTClient_connectOptions connectOptions;
    MQTTClient_SSLOptions sslOptions;
    mqtt_MessageArrivedHandlerFunc_t messageArrivedHandler;
    void* messageArrivedHandlerContext;
    mqtt_ConnectionLostHandlerFunc_t connectionLostHandler;
    void* connectionLostHandlerContext;
    // The legato client session that owns this MQTT session
    le_msg_SessionRef_t clientSession;
} mqtt_Session;

static int mqtt_QosEnumToValue(mqtt_Qos_t qos);
static void mqtt_ConnectionLostHandler(void* context, char* cause);
static void connectionLostEventHandler(void* report);
static int mqtt_MessageArrivedHandler(
    void* context, char* topicName, int topicLen, MQTTClient_message* message);
static void messageReceivedEventHandler(void* report);
static void mqtt_DestroySessionInternal(mqtt_Session* session);

//--------------------------------------------------------------------------------------------------
/**
 * Safe ref map for mqtt_Session objects returned to clients.
 */
//--------------------------------------------------------------------------------------------------
static le_ref_MapRef_t SessionRefMap;

//--------------------------------------------------------------------------------------------------
/**
 * Event id that is used to signal that a message has been received from the MQTT broker.  Events
 * must be used because paho spawns a thread for receiving messages and that means that the
 * callback can't call IPC methods because it is from a non-legato thread.
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
    // Safe reference to mqtt_Session
    mqtt_SessionRef_t session;
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
    mqtt_Session* s = calloc(sizeof(mqtt_Session), 1);
    LE_ASSERT(s);
    const MQTTClient_connectOptions initConnOpts = MQTTClient_connectOptions_initializer;
    memcpy(&(s->connectOptions), &initConnOpts, sizeof(initConnOpts));

    const MQTTClient_SSLOptions initSslOpts = MQTTClient_SSLOptions_initializer;
    memcpy(&(s->sslOptions), &initSslOpts, sizeof(initSslOpts));
    s->sslOptions.trustStore = SslCaCertsPath;

    const int createResult = MQTTClient_create(
            &(s->client),
            brokerURI,
            clientId,
            MQTTCLIENT_PERSISTENCE_NONE,
            NULL);
    if (createResult != MQTTCLIENT_SUCCESS)
    {
        LE_ERROR("Couldn't create MQTT session.  Paho error code: %d", createResult);
        free(s);
        return LE_FAULT;
    }

    le_msg_SessionRef_t clientSession = mqtt_GetClientSessionRef();
    s->clientSession = clientSession;

    *session = le_ref_CreateRef(SessionRefMap, s);

    LE_ASSERT(MQTTClient_setCallbacks(
            s->client,
            *session,
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
    mqtt_Session* s = le_ref_Lookup(SessionRefMap, session);
    if (s == NULL)
    {
        LE_KILL_CLIENT("Session doesn't exist");
        return;
    }
    if (s->clientSession != mqtt_GetClientSessionRef())
    {
        LE_KILL_CLIENT("Session doesn't belong to this client");
        return;
    }

    mqtt_DestroySessionInternal(s);
    le_ref_DeleteRef(SessionRefMap, session);
}

static void mqtt_DestroySessionInternal(mqtt_Session* session)
{
    MQTTClient_destroy(session->client);
    // It is necessary to cast to char* from const char* in order to free the memory
    // associated with the username and password.
    free((char*)session->connectOptions.username);
    free((char*)session->connectOptions.password);
    free(session);
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
    mqtt_Session* s = le_ref_Lookup(SessionRefMap, session);
    if (s == NULL)
    {
        LE_KILL_CLIENT("Session doesn't exist");
        return;
    }
    if (s->clientSession != mqtt_GetClientSessionRef())
    {
        LE_KILL_CLIENT("Session doesn't belong to this client");
        return;
    }

    s->connectOptions.keepAliveInterval = keepAliveInterval;
    s->connectOptions.cleansession = cleanSession;

    // username
    free((char*)s->connectOptions.username);
    if (username != NULL)
    {
        s->connectOptions.username = calloc(strlen(username) + 1, 1);
        LE_ASSERT(s->connectOptions.username != NULL);
        strcpy((char*)s->connectOptions.username, username);
    }
    else
    {
        s->connectOptions.username = NULL;
    }

    // password
    free((char*)s->connectOptions.password);
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
        s->connectOptions.password = calloc(passwordLength + 1, 1);
        LE_ASSERT(s->connectOptions.password != NULL);
        memcpy((uint8_t*)s->connectOptions.password, password, passwordLength);
        ((uint8_t*)s->connectOptions.password)[passwordLength] = '\0';
    }
    else
    {
        s->connectOptions.password = NULL;
        if (username != NULL)
        {
            LE_KILL_CLIENT("It is illegal to specify a password without a username");
        }
    }

    s->connectOptions.connectTimeout = connectTimeout;
    s->connectOptions.retryInterval = retryInterval;
    s->connectOptions.ssl = &s->sslOptions;
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
    mqtt_Session* s = le_ref_Lookup(SessionRefMap, session);
    if (s == NULL)
    {
        LE_KILL_CLIENT("Session doesn't exist");
        return LE_FAULT;
    }
    if (s->clientSession != mqtt_GetClientSessionRef())
    {
        LE_KILL_CLIENT("Session doesn't belong to this client");
        return LE_FAULT;
    }

    const int connectResult = MQTTClient_connect(s->client, &s->connectOptions);
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
    mqtt_Session* s = le_ref_Lookup(SessionRefMap, session);
    if (s == NULL)
    {
        LE_KILL_CLIENT("Session doesn't exist");
        return LE_FAULT;
    }
    if (s->clientSession != mqtt_GetClientSessionRef())
    {
        LE_KILL_CLIENT("Session doesn't belong to this client");
        return LE_FAULT;
    }

    const int waitBeforeDisconnectMs = 0;
    const int disconnectResult = MQTTClient_disconnect(s->client, waitBeforeDisconnectMs);
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
    mqtt_Session* s = le_ref_Lookup(SessionRefMap, session);
    if (s == NULL)
    {
        LE_KILL_CLIENT("Session doesn't exist");
        return LE_FAULT;
    }
    if (s->clientSession != mqtt_GetClientSessionRef())
    {
        LE_KILL_CLIENT("Session doesn't belong to this client");
        return LE_FAULT;
    }

    MQTTClient_deliveryToken* dtNotUsed = NULL;
    const int publishResult = MQTTClient_publish(
        s->client, topic, payloadLen, (void*)payload, mqtt_QosEnumToValue(qos), retain, dtNotUsed);
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
    mqtt_Session* s = le_ref_Lookup(SessionRefMap, session);
    if (s == NULL)
    {
        LE_KILL_CLIENT("Session doesn't exist");
        return LE_FAULT;
    }
    if (s->clientSession != mqtt_GetClientSessionRef())
    {
        LE_KILL_CLIENT("Session doesn't belong to this client");
        return LE_FAULT;
    }

    const int subscribeResult = MQTTClient_subscribe(s->client, topicPattern, qos);
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
    mqtt_Session* s = le_ref_Lookup(SessionRefMap, session);
    if (s == NULL)
    {
        LE_KILL_CLIENT("Session doesn't exist");
        return LE_FAULT;
    }
    if (s->clientSession != mqtt_GetClientSessionRef())
    {
        LE_KILL_CLIENT("Session doesn't belong to this client");
        return LE_FAULT;
    }

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
    mqtt_Session* s = le_ref_Lookup(SessionRefMap, session);
    if (s == NULL)
    {
        LE_KILL_CLIENT("Session doesn't exist");
        return NULL;
    }
    if (s->clientSession != mqtt_GetClientSessionRef())
    {
        LE_KILL_CLIENT("Session doesn't belong to this client");
        return NULL;
    }

    if (s->connectionLostHandler != NULL)
    {
        LE_KILL_CLIENT("You may only register one connection lost handler");
        return NULL;
    }
    else
    {
        s->connectionLostHandler = handler;
        s->connectionLostHandlerContext = context;
    }

    return (mqtt_ConnectionLostHandlerRef_t)s;
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
    mqtt_Session* s = le_ref_Lookup(SessionRefMap, handler);
    if (s == NULL)
    {
        LE_KILL_CLIENT("Session doesn't exist");
        return;
    }
    if (s->clientSession != mqtt_GetClientSessionRef())
    {
        LE_KILL_CLIENT("Session doesn't belong to this client");
        return;
    }

    s->connectionLostHandler = NULL;
    s->connectionLostHandlerContext = NULL;
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
    mqtt_Session* s = le_ref_Lookup(SessionRefMap, session);
    if (s == NULL)
    {
        LE_KILL_CLIENT("Session doesn't exist");
        return NULL;
    }
    if (s->clientSession != mqtt_GetClientSessionRef())
    {
        LE_KILL_CLIENT("Session doesn't belong to this client");
        return NULL;
    }

    if (s->messageArrivedHandler != NULL)
    {
        LE_KILL_CLIENT("You may only register one message arrived handler per session");
        return NULL;
    }
    else
    {
        s->messageArrivedHandler = handler;
        s->messageArrivedHandlerContext = context;
    }

    return (mqtt_MessageArrivedHandlerRef_t)s;
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
    mqtt_Session* s = le_ref_Lookup(SessionRefMap, handler);
    if (s == NULL)
    {
        LE_KILL_CLIENT("Session doesn't exist");
        return;
    }
    if (s->clientSession != mqtt_GetClientSessionRef())
    {
        LE_KILL_CLIENT("Session doesn't belong to this client");
        return;
    }

    s->messageArrivedHandler = NULL;
    s->messageArrivedHandlerContext = NULL;
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
    le_event_Report(connectionLostThreadEventId, &context, sizeof(void*));
}

//--------------------------------------------------------------------------------------------------
/**
 * The event handler for the connection lost event that is generated by mqtt_ConnectionLostHandler.
 * This function calls the handler supplied by the client.
 */
//--------------------------------------------------------------------------------------------------
static void connectionLostEventHandler(void* report)
{
    mqtt_Session* s = le_ref_Lookup(SessionRefMap, report);
    if (s == NULL)
    {
        LE_KILL_CLIENT("Session doesn't exist");
        return;
    }

    if (s->connectionLostHandler != NULL)
    {
        s->connectionLostHandler(s->connectionLostHandlerContext);
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
    mqtt_Session* s = le_ref_Lookup(SessionRefMap, context);
    if (s == NULL)
    {
        LE_WARN("Session doesn't exist");
        return true;
    }

    mqtt_Message* storedMsg = calloc(sizeof(mqtt_Message), 1);
    LE_ASSERT(storedMsg);

    LE_INFO("MessageArrivedHandler called for topic=%s. Storing session=0x%p", topicName, context);
    storedMsg->session = context;

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

    mqtt_Session* s = le_ref_Lookup(SessionRefMap, storedMsg->session);
    if (s == NULL)
    {
        LE_WARN("Session lookup failed for session=0x%p", storedMsg->session);
        return;
    }

    if (s->messageArrivedHandler != NULL)
    {
        if (storedMsg->topicLength <= MQTT_MAX_TOPIC_LENGTH &&
            storedMsg->payloadLength <= MQTT_MAX_PAYLOAD_LENGTH)
        {
            s->messageArrivedHandler(
                storedMsg->topic,
                storedMsg->payload,
                storedMsg->payloadLength,
                s->messageArrivedHandlerContext);
        }
        else
        {
            LE_WARN(
                "Message arrived from broker, but it is too large to deliver using Legato IPC - "
                "topicLength=%zu, payloadLength=%zu",
                storedMsg->topicLength,
                storedMsg->payloadLength);
        }
    }
    else
    {
        LE_WARN(
            "Message has arrived, but no handler is registered to receive the notification");
    }

    free(storedMsg->topic);
    free(storedMsg->payload);
    free(storedMsg);
}

static void DestroyAllOwnedSessions(le_msg_SessionRef_t sessionRef, void* context)
{
    le_ref_IterRef_t it = le_ref_GetIterator(SessionRefMap);
    le_result_t iterRes = le_ref_NextNode(it);
    while (iterRes == LE_OK)
    {
        mqtt_Session* s = le_ref_GetValue(it);
        LE_ASSERT(s != NULL);
        mqtt_SessionRef_t sRef = (mqtt_SessionRef_t)(le_ref_GetSafeRef(it));
        LE_ASSERT(sRef != NULL);
        // Advance the interator before deletion to prevent invalidation
        iterRes = le_ref_NextNode(it);
        if (s->clientSession == sessionRef)
        {
            mqtt_DestroySessionInternal(s);
            le_ref_DeleteRef(SessionRefMap, sRef);
        }
    }
}


COMPONENT_INIT
{
    SessionRefMap = le_ref_CreateMap("MQTT sessions", 16);

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

    le_msg_AddServiceCloseHandler(mqtt_GetServiceRef(), DestroyAllOwnedSessions, NULL);

    MQTTClient_init_options initOptions = MQTTClient_init_options_initializer;
    initOptions.do_openssl_init = 1;
    MQTTClient_global_init(&initOptions);
}
