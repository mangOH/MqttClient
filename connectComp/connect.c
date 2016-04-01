//--------------------------------------------------------------------------------------------------
/**
 * @file connect.c
 *
 * This component is used to start/stop mqttClient and to send mqtt messages to AirVantage.
 *
 * <hr>
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 *
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include "interfaces.h"

int         nCount = 0;
char        szPassword[32] = {0};


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
                "Usage of the 'connect' tool is:",
                "   connect -p <password>"
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

void handler_CapturePassword(const char * szPwd)
{
    LE_INFO("Password is: %s", szPwd);
    strcpy(szPassword, szPwd);
    nCount++;
}

//--------------------------------------------------------------------------------------------------
/**
 * App init.
 *
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    //int         nCount = 0;
    //char        szPassword[32] = {0};


    #if 1
    //temporary fix, as le_arg_GetStringOption is broken
    le_arg_SetStringCallback(handler_CapturePassword, "p", "password");
    le_arg_SetFlagCallback(PrintUsage, "h", "help");
    le_arg_Scan();
    #else
    if (LE_OK == le_arg_GetStringOption((const char **) &szPassword, "p", NULL))
    {
        LE_INFO("Password is: %s", szPassword);
        nCount++;
    }
    #endif

    LE_INFO("Start MQTT connection");
    mqtt_Connect(szPassword);

    exit(EXIT_SUCCESS);
}
