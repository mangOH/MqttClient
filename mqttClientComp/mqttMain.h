#ifndef __MQTT_MAIN_H_
#define __MQTT_MAIN_H_

#define MQTT_MAIN_TOPIC_NAME_LEN                   128
#define MQTT_MAIN_KEY_NAME_LEN                     128
#define MQTT_MAIN_VALUE_LEN                        128
#define MQTT_MAIN_TIMESTAMP_LEN                    16

typedef struct _mqttConnStateData_t
{
    bool    isConnected;
    int     connectErrorCode;
    int     subErrorCode;
} mqttConnStateData_t;

typedef struct _mqttInMsgData_t
{
    char topicName[MQTT_MAIN_TOPIC_NAME_LEN + 1];
    char keyName[MQTT_MAIN_KEY_NAME_LEN + 1];
    char value[MQTT_MAIN_VALUE_LEN + 1];
    char timestamp[MQTT_MAIN_TIMESTAMP_LEN + 1];
} mqttInMsgData_t;

#endif
