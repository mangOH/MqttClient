#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "json/swir_json.h"

#define DEBUG(M, ...)       fprintf(stderr, "[DEBUG] (%s:%d): " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define DEBUG_IF(A, M, ...) if(!(A)) { DEBUG(M, ##__VA_ARGS__); }
#define ERROR(M, ...)       fprintf(stderr, "[ERROR] (%s:%d): " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define WARN(M, ...)        fprintf(stderr, "[WARN ] (%s:%d): " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define INFO(M, ...)        fprintf(stderr, "[INFO ] (%s:%d): " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define ERROR_IF(A, M, ...) if(!(A)) { ERROR(M, ##__VA_ARGS__); }

#define MQTT_CLIENT_AV_JSON_KEY_MAX_COUNT             10
#define MQTT_CLIENT_AV_JSON_KEY_MAX_LENGTH            32
#define MQTT_CLIENT_KEY_NAME_LEN                      128

void ParseMessage(char* payload)
{
    char* command = NULL;
    char* uid = NULL;
    char* timestamp = NULL;
    char* id = NULL;
    char* param = NULL;
    bool isValid = false;

    uid = swirjson_getValue(payload, -1, "uid");
    if (!uid) {
        ERROR("Failed to find 'uid' field in the incoming MQTT message");
        goto cleanup;
    }

    timestamp = swirjson_getValue(payload, -1, "timestamp");
    if (!timestamp) {
        ERROR("Failed to find 'timestamp' field in the incoming MQTT message");
        goto cleanup;
    }

    command = swirjson_getValue(payload, -1, "command");
    if (command) {
        /* Customer command */
        id = swirjson_getValue(command, -1, "id");
        if (!uid) {
            ERROR("Failed to find command id in the incoming customer command");
            goto cleanup;
        }
        /* The command is valid */
        isValid = true;
        param = swirjson_getValue(command, -1, "params");
        if (param) {
            int i;
            /* Decode the optional parameters */
            for (i = 0; i < MQTT_CLIENT_AV_JSON_KEY_MAX_COUNT; i++) {
                /* Decode key and value from json node */
                char key[MQTT_CLIENT_AV_JSON_KEY_MAX_LENGTH];
                char* value = swirjson_getValue(param, i, key);
                if (value) {
                    DEBUG("AV Customer command: id('%s') key('%s') value('%s') ts('%s')", id, key, value, timestamp);
                    char fullKey[MQTT_CLIENT_KEY_NAME_LEN + 1];
                    sprintf(fullKey, "Full key..: %s.%s", id, key);
                    free(value);
                } else {
                    break;
                }
            }
        }
    } else {
        /* Read or write task */
        command = swirjson_getValue(payload, -1, "write");
        if (command) {
            /* The command is valid */
            isValid = true;
            int i;
            for (i = 0; i < MQTT_CLIENT_AV_JSON_KEY_MAX_COUNT; i++) {
                char key[MQTT_CLIENT_AV_JSON_KEY_MAX_LENGTH];
                char* value = swirjson_getValue(command, i, key);
                if (value) {
                    INFO("AV Write command: key('%s') value('%s') ts('%s')", key, value, timestamp);
                    free(value);
                } else {
                    break;
                }
            }
        } else {
            WARN("Invalid command type, failed to find 'command' or 'write' key words");
        }
    }

    if (isValid) {
        INFO("Message valid");
    } else {
        ERROR("Message invalid");
    }

cleanup:
    if (command) free(command);
    if (id) free(id);
    if (param) free(param);
    if (timestamp) free(timestamp);
    if (uid) free(uid);
}

#define MSG_1 "[" \
    "{" \
        "\"uid\": \"c117c8ce7a8c4d89bd0b58bccf7e1575\"," \
        "\"timestamp\": 1498662247030," \
        "\"write\": [" \
            "{" \
                "\"key1.id1.test\":0" \
            "}" \
        "]" \
    "}" \
"]"

#define MSG_2 "[" \
    "{" \
        "\"uid\": \"c117c8ce7a8c4d89bd0b58bccf7e1575\"," \
        "\"timestamp\": 1498662247030," \
        "\"write\": [" \
            "{" \
                "\"key2.id2.test\": 0" \
            "}" \
        "]" \
    "}" \
"]"

#define MSG_3 "[" \
    "{" \
        "\"uid\": \"c117c8ce7a8c4d89bd0b58bccf7e1575\"," \
        "\"timestamp\": 1498662247030," \
        "\"write\": [" \
            "{" \
                "\"key3.id3.test\"   :   85    " \
            "}" \
        "]" \
    "}" \
"]"

#define MSG_4 "[" \
    "{" \
        "\"uid\": \"c117c8ce7a8c4d89bd0b58bccf7e1575\"," \
        "\"timestamp\": 1498662247030," \
        "\"write\": [" \
            "{" \
                "\"key4.id4.message\"   :  \"The test message \"" \
            "}" \
        "]" \
    "}" \
"]"

/*------------------------------------------------------------------------------*/
/* Main                                                                         */
/*------------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
    DEBUG("%s", MSG_1);
    ParseMessage(MSG_1);

    DEBUG("%s", MSG_2);
    ParseMessage(MSG_2);

    DEBUG("%s", MSG_3);
    ParseMessage(MSG_3);

    DEBUG("%s", MSG_4);
    ParseMessage(MSG_4);

    return 0;
}
