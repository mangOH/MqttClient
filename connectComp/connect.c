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
char        szUser[32] = {0};
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
                "   connect -u <username> -p <password>",
                "   username is optional. IMEI will be used if it is not provided"
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


void handler_CaptureUsername(const char * szUsername)
{
    LE_INFO("Username is: %s", szUsername);
    strcpy(szUser, szUsername);
    nCount++;
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
    //char        szUser[32] = {0};
    //char        szPassword[32] = {0};
    

    #if 1
    //temporary fix, as le_arg_GetStringOption is broken
    le_arg_SetStringCallback(handler_CaptureUsername, "u", "username");
    le_arg_SetStringCallback(handler_CapturePassword, "p", "password");
    le_arg_SetFlagCallback(PrintUsage, "h", "help");
    le_arg_Scan();
    #else
    if (LE_OK == le_arg_GetStringOption((const char **) &szUser, "u", NULL))
    {
        LE_INFO("Username is: %s", szUser);
        nCount++;
    }

    if (LE_OK == le_arg_GetStringOption((const char **) &szPassword, "p", NULL))
    {
        LE_INFO("Password is: %s", szPassword);
        nCount++;
    }
    #endif

    LE_INFO("Calling mqttClient to start MQTT connection");
    mqttApi_Connect(szUser, szPassword);

    exit(EXIT_SUCCESS);
}
