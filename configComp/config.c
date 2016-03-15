//--------------------------------------------------------------------------------------------------
/**
 * @file config.c
 *
 * This component is used to set Broker for mqttClient
 *
 * <hr>
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 *
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include "interfaces.h"


char        szBrokerUrl[256] = {0};
int32_t     nCount = 0;


void handler_CaptureHost(const char * szMqttUrl)
{
    LE_INFO("MQTT host is: %s", szMqttUrl);
    strcpy(szBrokerUrl, szMqttUrl);
    nCount++;
}

//--------------------------------------------------------------------------------------------------
/**
 * Helper.
 *
 */
//--------------------------------------------------------------------------------------------------
static void PrintUsage()
{
    int     idx;
    bool    sandboxed = (getuid() != 0);
    const   char * usagePtr[] =
            {
                "Usage of the 'config' tool is:",
                "   config -b <broker-URL> -p <broker-port> -k <keepalive> -q <QoS(0-2)>",
                "   config (no parameter) to display current settings"
            };

    for (idx = 0; idx < NUM_ARRAY_MEMBERS(usagePtr); idx++)
    {
        if (sandboxed)
        {
            LE_INFO("%s", usagePtr[idx]);
        }
        else
        {
            fprintf(stderr, "%s\n", usagePtr[idx]);
        }
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * App init.
 *
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    if (le_arg_NumArgs() == 0)
    {
        LE_INFO("Calling mqttClient to view MQTT settings");
        mqttApi_ViewConfig();

        exit(EXIT_SUCCESS);
    }
    else
    {
        
        int32_t     nPortNumber = -1;
        int32_t     nKeepAlive = -1;
        int32_t     nQoS = -1;

        #if 1
        //temporary fix, as le_arg_GetStringOption is broken

        int         nPort = -1;
        int         nka = -1;
        int         nQ = -1;

        le_arg_SetStringCallback(handler_CaptureHost, "b", "broker");

        le_arg_SetFlagCallback(PrintUsage, "h", "help");
        
        le_arg_SetIntVar(&nPort, "p", "port");
        le_arg_SetIntVar(&nka, "k", "keepalive");
        le_arg_SetIntVar(&nQ, "q", "qos");

        le_arg_Scan();

        nPortNumber = nPort;
        nKeepAlive = nka;
        nQoS = nQ;

        mqttApi_Config(szBrokerUrl, nPortNumber, nKeepAlive, nQoS);

        exit(EXIT_SUCCESS);

        #else
        char        szBrokerUrl[256] = {0};
        int         nValue;
        int32_t     nCount = 0;

        if (LE_OK == le_arg_GetStringOption((const char **) &szBrokerUrl, "h", NULL))
        {
            LE_INFO("New Broker URL is: %s", szBrokerUrl);
            nCount++;
        }
        if (LE_OK == le_arg_GetIntOption(&nValue, "p", "port"))
        {
            nPortNumber = nValue;
            LE_INFO("New Broker Port Number is: %d", nPortNumber);
            nCount++;
        }

        if (LE_OK == le_arg_GetIntOption(&nValue, "k", "keepalive"))
        {
            nKeepAlive = nValue;
            LE_INFO("Keep Alive is: %d seconds", nKeepAlive);
            nCount++;
        }

        if (LE_OK == le_arg_GetIntOption(&nValue, "q", "qos"))
        {
            nQoS = nValue;
            LE_INFO("QoS [0,1,2] is : %d", nQoS);
            nCount++;
        }

        if (nCount > 0)
        {
            mqttApi_Config(szBrokerUrl, nPortNumber, nKeepAlive, nQoS);

            exit(EXIT_SUCCESS);
        }
        else
        {
            PrintUsage();
            exit(EXIT_FAILURE);
        }
        #endif
    }

}
